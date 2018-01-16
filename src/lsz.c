/*
  lsz - send files with x/y/zmodem
  Copyright (C) until 1988 Chuck Forsberg (Omen Technology INC)
  Copyright (C) 1994 Matt Porter, Michael D. Black
  Copyright (C) 1996, 1997 Uwe Ohse
  Copyright (C) 2018 Michael L. Gran

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2, or (at your option)
  any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
  02111-1307, USA.

  originally written by Chuck Forsberg
*/
#include "zglobal.h"

/* char *getenv(); */

#define SS_NORMAL 0
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <setjmp.h>
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <time.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>


#include <sys/mman.h>
size_t mm_size;
void *mm_addr=NULL;
#include "timing.h"
#include "long-options.h"
#include "xstrtoul.h"
#include "log.h"

static unsigned Baudrate=2400;	/* Default, should be set by first mode() call */
static unsigned Txwindow;	/* Control the size of the transmitted window */
static unsigned Txwspac;	/* Spacing between zcrcq requests */
static unsigned Txwcnt;	/* Counter used to space ack requests */
static size_t Lrxpos;		/* Receiver's last reported offset */
static int errors;
static int under_rsh=FALSE;
static  int turbo_escape;
static int no_unixmode;

static int Canseek=1; /* 1: can; 0: only rewind, -1: neither */

static int zsendfile (zm_t *zm, struct zm_fileinfo *zi, const char *buf, size_t blen);
static int getnak (zm_t *zm);
static int wctxpn (zm_t *zm, struct zm_fileinfo *);
static int wcs (zm_t *zm, const char *oname, const char *remotename);
static size_t zfilbuf (struct zm_fileinfo *zi);
static size_t filbuf (char *buf, size_t count);
static int getzrxinit (zm_t *zm);
static int calc_blklen (long total_sent);
static int sendzsinit (zm_t *zm);
static int wctx (zm_t *zm, struct zm_fileinfo *);
static int zsendfdata (zm_t *zm, struct zm_fileinfo *);
static int getinsync (zm_t *zm, struct zm_fileinfo *, int flag);
static void countem (int argc, char **argv);
static void chkinvok (const char *s);
static void usage (int exitcode, const char *what);
static void saybibi (zm_t *zm);
static int wcsend (zm_t *zm, int argc, char *argp[]);
static int wcputsec (zm_t *zm, char *buf, int sectnum, size_t cseclen);
static void usage1 (int exitcode);

#define ZM_SEND_DATA(x,y,z)						\
	do { if (zm->crc32t) {zm_send_data32(zm,x,y,z); } else {zm_send_data(zm,x,y,z);}} while(0)
#define DATAADR (mm_addr ? ((char *)mm_addr)+zi->bytes_sent : txbuf)

static int Filesleft;
static long Totalleft;
static size_t buffersize=16384;
static int use_mmap=1;

/*
 * Attention string to be executed by receiver to interrupt streaming data
 *  when an error is detected.  A pause (0336) may be needed before the
 *  ^C (03) or after it.
 */
static char Myattn[] = { 0 };

static FILE *input_f;

#define MAX_BLOCK 8192
static char txbuf[MAX_BLOCK];

static long vpos = 0;			/* Number of bytes read from file */

static char Lastrx;
static char Crcflg;
static int Verbose=LOG_ERROR;
static int Restricted=0;	/* restricted; no /.. or ../ in filenames */
static int Quiet=0;		/* overrides logic that would otherwise set verbose */
static int Fullname=0;		/* transmit full pathname */
static int Unlinkafter=0;	/* Unlink file after it is sent */
static int firstsec;
static int errcnt=0;		/* number of files unreadable */
static size_t blklen=128;		/* length of transmitted records */
static int Optiong;		/* Let it rip no wait for sector ACK's */
static int Totsecs;		/* total number of sectors this file */
static int Filcnt=0;		/* count of number of files opened */
static int Lfseen=0;
static unsigned Rxbuflen = 16384;	/* Receiver's max buffer length */
static unsigned Tframlen = 0;	/* Override for tx frame length */
static unsigned blkopt=0;		/* Override value for zmodem blklen */
static int Rxflags = 0;
static int Rxflags2 = 0;
static size_t bytcnt;
static int Wantfcs32 = TRUE;	/* want to send 32 bit FCS */
static char Lzconv;	/* Local ZMODEM file conversion request */
static char Lzmanag;	/* Local ZMODEM file management request */
static int Lskipnocor;
static char Lztrans;
static int Exitcode;
static size_t Lastsync;		/* Last offset to which we got a ZRPOS */
static int Beenhereb4;		/* How many times we've been ZRPOS'd same place */

static int no_timeout=FALSE;
static size_t max_blklen=1024;
static size_t start_blklen=0;
static time_t stop_time=0;
static int tcp_flag=0;
static char *tcp_server_address=0;
static int tcp_socket=-1;
static int hyperterm=0;

static int error_count;
#define OVERHEAD 18
#define OVER_ERR 20

#define MK_STRING(x) #x


static jmp_buf intrjmp;	/* For the interrupt on RX CAN */

static long min_bps;
static long min_bps_time;

static int io_mode_fd=0;
static int zrqinits_sent=0;
static int play_with_sigint=0;

/* called by signal interrupt or terminate to clean things up */
void
bibi (int n)
{
	canit(STDOUT_FILENO);
	fflush (stdout);
	io_mode (io_mode_fd, 0);
	if (n == 99)
		log_fatal(_("io_mode(,2) in rbsb.c not implemented"));
	else
		log_fatal(_("caught signal %d; exiting"), n);
	if (n == SIGQUIT)
		abort ();
	exit (128 + n);
}

/* Called when ZMODEM gets an interrupt (^C) */
static void
onintr(int n LRZSZ_ATTRIB_UNUSED)
{
	signal(SIGINT, SIG_IGN);
	longjmp(intrjmp, -1);
}

static int Zctlesc;	/* Encode control characters */
const char *program_name = "sz";
static int Zrwindow = 1400;	/* RX window size (controls garbage count) */

static struct option const long_options[] =
{
  {"append", no_argument, NULL, '+'},
  {"try-8k", no_argument, NULL, '8'},
  {"start-8k", no_argument, NULL, '9'},
  {"try-4k", no_argument, NULL, '4'},
  {"start-4k", no_argument, NULL, '5'},
  {"binary", no_argument, NULL, 'b'},
  {"bufsize", required_argument, NULL, 'B'},
  {"full-path", no_argument, NULL, 'f'},
  {"escape", no_argument, NULL, 'e'},
  {"rename", no_argument, NULL, 'E'},
  {"help", no_argument, NULL, 'h'},
  {"crc-check", no_argument, NULL, 'H'},
  {"packetlen", required_argument, NULL, 'L'},
  {"framelen", required_argument, NULL, 'l'},
  {"min-bps", required_argument, NULL, 'm'},
  {"min-bps-time", required_argument, NULL, 'M'},
  {"newer", no_argument, NULL, 'n'},
  {"newer-or-longer", no_argument, NULL, 'N'},
  {"16-bit-crc", no_argument, NULL, 'o'},
  {"disable-timeouts", no_argument, NULL, 'O'},
  {"disable-timeout", no_argument, NULL, 'O'}, /* i can't get it right */
  {"protect", no_argument, NULL, 'p'},
  {"resume", no_argument, NULL, 'r'},
  {"restricted", no_argument, NULL, 'R'},
  {"quiet", no_argument, NULL, 'q'},
  {"stop-at", required_argument, NULL, 's'},
  {"timeout", required_argument, NULL, 't'},
  {"turbo", no_argument, NULL, 'T'},
  {"unlink", no_argument, NULL, 'u'},
  {"unrestrict", no_argument, NULL, 'U'},
  {"verbose", no_argument, NULL, 'v'},
  {"windowsize", required_argument, NULL, 'w'},
  {"zmodem", no_argument, NULL, 'Z'},
  {"overwrite", no_argument, NULL, 'y'},
  {"overwrite-or-skip", no_argument, NULL, 'Y'},

  {"delay-startup", required_argument, NULL, 4},
  {"tcp", no_argument, NULL, 5},
  {"tcp-server", no_argument, NULL, 6},
  {"tcp-client", required_argument, NULL, 7},
  {"no-unixmode", no_argument, NULL, 8},
  {"hyperterm", no_argument, &hyperterm, 1},
  {NULL, 0, NULL, 0}
};

static void
show_version(void)
{
	display ("%s (%s) %s", program_name, PACKAGE, VERSION);
}


int
main(int argc, char **argv)
{
	char *cp;
	int npats;
	int dm;
	int i;
	int stdin_files;
	char **patts;
	int c;
	unsigned int startup_delay=0;
	int Znulls = 0;
	int Rxtimeout = 0;
	zm_t *zm;
	if (((cp = getenv("ZNULLS")) != NULL) && *cp)
		Znulls = atoi(cp);
	if (((cp=getenv("SHELL"))!=NULL) && (strstr(cp, "rsh") || strstr(cp, "rksh")
		|| strstr(cp, "rbash") || strstr(cp,"rshell")))
	{
		under_rsh=TRUE;
		Restricted=1;
	}
	if ((cp=getenv("ZMODEM_RESTRICTED"))!=NULL)
		Restricted=1;
	from_cu();
	chkinvok(argv[0]);


	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

	parse_long_options (argc, argv, show_version, usage1);

	Rxtimeout = 600;

	while ((c = getopt_long (argc, argv,
		"2+48abB:C:c:dfeEghHi:kL:l:m:M:NnOopRrqsSt:TUuvw:XYy",
		long_options, (int *) 0))!=EOF)
	{
		unsigned long int tmp;
		char *tmpptr;
		enum strtol_error s_err;

		switch (c)
		{
		case 0:
			break;
		case '+': Lzmanag = ZF1_ZMAPND; break;
		case '8':
			if (max_blklen==8192)
				start_blklen=8192;
			else
				max_blklen=8192;
			break;
		case '9': /* this is a longopt .. */
			start_blklen=8192;
			max_blklen=8192;
			break;
		case '4':
			if (max_blklen==4096)
				start_blklen=4096;
			else
				max_blklen=4096;
			break;
		case '5': /* this is a longopt .. */
			start_blklen=4096;
			max_blklen=4096;
			break;
		case 'b': Lzconv = ZCBIN; break;
		case 'B':
			if (0==strcmp(optarg,"auto"))
				buffersize= (size_t) -1;
			else
				buffersize=strtol(optarg,NULL,10);
			use_mmap=0;
			break;
		case 'f': Fullname=TRUE; break;
		case 'e': Zctlesc = 1; break;
		case 'E': Lzmanag = ZF1_ZMCHNG; break;
		case 'h': usage(0,NULL); break;
		case 'H': Lzmanag = ZF1_ZMCRC; break;
		case 'L':
			s_err = xstrtoul (optarg, NULL, 0, &tmp, "ck");
			blkopt = tmp;
			if (s_err != LONGINT_OK)
				STRTOL_FATAL_ERROR (optarg, _("packetlength"), s_err);
			if (blkopt<24 || blkopt>MAX_BLOCK)
			{
				char meld[256];
				sprintf(meld,
					_("packetlength out of range 24..%ld"),
					(long) MAX_BLOCK);
				usage(2,meld);
			}
			break;
		case 'l':
			s_err = xstrtoul (optarg, NULL, 0, &tmp, "ck");
			Tframlen = tmp;
			if (s_err != LONGINT_OK)
				STRTOL_FATAL_ERROR (optarg, _("framelength"), s_err);
			if (Tframlen<32 || Tframlen>MAX_BLOCK)
			{
				char meld[256];
				sprintf(meld,
					_("framelength out of range 32..%ld"),
					(long) MAX_BLOCK);
				usage(2,meld);
			}
			break;
        case 'm':
			s_err = xstrtoul (optarg, &tmpptr, 0, &tmp, "km");
			min_bps = tmp;
			if (s_err != LONGINT_OK)
				STRTOL_FATAL_ERROR (optarg, _("min_bps"), s_err);
			if (min_bps<0)
				usage(2,_("min_bps must be >= 0"));
			break;
        case 'M':
			s_err = xstrtoul (optarg, NULL, 0, &tmp, NULL);
			min_bps_time = tmp;
			if (s_err != LONGINT_OK)
				STRTOL_FATAL_ERROR (optarg, _("min_bps_time"), s_err);
			if (min_bps_time<=1)
				usage(2,_("min_bps_time must be > 1"));
			break;
		case 'N': Lzmanag = ZF1_ZMNEWL;  break;
		case 'n': Lzmanag = ZF1_ZMNEW;  break;
		case 'o': Wantfcs32 = FALSE; break;
		case 'O': no_timeout = TRUE; break;
		case 'p': Lzmanag = ZF1_ZMPROT;  break;
		case 'r':
			if (Lzconv == ZCRESUM)
				Lzmanag = ZF1_ZMCRC;
			else
				Lzconv = ZCRESUM;
			break;
		case 'R': Restricted = TRUE; break;
		case 'q': Quiet=TRUE; Verbose=LOG_FATAL; break;
		case 's':
			if (isdigit((unsigned char) (*optarg))) {
				struct tm *tm;
				time_t t;
				int hh,mm;
				char *nex;

				hh = strtoul (optarg, &nex, 10);
				if (hh>23)
					usage(2,_("hour to large (0..23)"));
				if (*nex!=':')
					usage(2, _("unparsable stop time"));
				nex++;
				mm = strtoul (optarg, &nex, 10);
				if (mm>59)
					usage(2,_("minute to large (0..59)"));

				t=time(NULL);
				tm=localtime(&t);
				tm->tm_hour=hh;
				tm->tm_min=hh;
				stop_time=mktime(tm);
				if (stop_time<t)
					stop_time+=86400L; /* one day more */
				if (stop_time - t <10)
					usage(2,_("stop time to small"));
			} else {
				s_err = xstrtoul (optarg, NULL, 0, &tmp, NULL);
				stop_time = tmp + time(0);
				if (s_err != LONGINT_OK)
					STRTOL_FATAL_ERROR (optarg, _("stop-at"), s_err);
				if (tmp<10)
					usage(2,_("stop time to small"));
			}
			break;
		case 'T': turbo_escape=1; break;
		case 't':
			s_err = xstrtoul (optarg, NULL, 0, &tmp, NULL);
			Rxtimeout = tmp;
			if (s_err != LONGINT_OK)
				STRTOL_FATAL_ERROR (optarg, _("timeout"), s_err);
			if (Rxtimeout<10 || Rxtimeout>1000)
				usage(2,_("timeout out of range 10..1000"));
			break;
		case 'u': ++Unlinkafter; break;
		case 'U':
			if (!under_rsh)
				Restricted=0;
			else {
				log_fatal(_("security violation: can't do that under restricted shell"));
				exit(1);
			}
			break;
		case 'v': Verbose=LOG_INFO; break;
		case 'w':
			s_err = xstrtoul (optarg, NULL, 0, &tmp, NULL);
			Txwindow = tmp;
			if (s_err != LONGINT_OK)
				STRTOL_FATAL_ERROR (optarg, _("window size"), s_err);
			if (Txwindow < 256)
				Txwindow = 256;
			Txwindow = (Txwindow/64) * 64;
			Txwspac = Txwindow/4;
			if (blkopt > Txwspac
			 || (!blkopt && Txwspac < MAX_BLOCK))
				blkopt = Txwspac;
			break;
		case 'Y':
			Lskipnocor = TRUE;
			/* **** FALLL THROUGH TO **** */
		case 'y':
			Lzmanag = ZF1_ZMCLOB; break;
		case 2:
			break;
		case 4:
			s_err = xstrtoul (optarg, NULL, 0, &tmp, NULL);
			startup_delay = tmp;
			if (s_err != LONGINT_OK)
				STRTOL_FATAL_ERROR (optarg, _("startup delay"), s_err);
			break;
		case 5:
			tcp_flag=1;
			break;
		case 6:
			tcp_flag=2;
			break;
		case 7:
			tcp_flag=3;
			tcp_server_address=(char *)strdup(optarg);
			if (!tcp_server_address) {
				log_fatal(_("out of memory"));
				exit(1);
			}
			break;
		case 8: no_unixmode=1; break;
		default:
			usage (2,NULL);
			break;
		}
	}

	log_set_level(Verbose);
	if (getuid()!=geteuid()) {
		log_fatal(_("this program was never intended to be used setuid"));
		exit(1);
	}
	zm = zm_init(Rxtimeout,
		     Znulls,
		     0, 	/* eflag */
		     Baudrate,
		     turbo_escape,
		     Zctlesc,
		     Zrwindow);
	log_info("initial protocol is ZMODEM");
	if (start_blklen==0) {
		start_blklen=1024;
		if (Tframlen) {
			start_blklen=max_blklen=Tframlen;
		}
	}

	if (argc<2)
		usage(2,_("need at least one file to send"));

	if (startup_delay)
		sleep(startup_delay);

	/* we want interrupted system calls to fail and not to be restarted. */
	siginterrupt(SIGALRM,1);

	npats = argc - optind;
	patts=&argv[optind];

	if (npats < 1)
		usage(2,_("need at least one file to send"));
	if (Fromcu && !Quiet) {
		if (Verbose == LOG_FATAL)
			Verbose = LOG_INFO;
		log_set_level(Verbose);
	}
	log_debug("%s %s", program_name, VERSION);

	if (tcp_flag==2) {
		char buf[256];
		char hn[256];
		char *p,*q;
		int d;

		/* tell receiver to receive via tcp */
		d=tcp_server(buf);
		p=strchr(buf+1,'<');
		p++;
		q=strchr(p,'>');
		*q=0;
		if (gethostname(hn,sizeof(hn))==-1) {
			log_fatal(_("hostname too long"));
			exit(1);
		}
		display("connect with lrz --tcp-client \"%s:%s\"",hn,p);
		fflush(stdout);
		/* ok, now that this file is sent we can switch to tcp */

		tcp_socket=tcp_accept(d);
		dup2(tcp_socket,0);
		dup2(tcp_socket,1);
	}
	if (tcp_flag==3) {
		char buf[256];
		char *p;
		p=strchr(tcp_server_address,':');
		if (!p) {
			log_fatal(_("illegal server address"));
			exit(1);
		}
		*p++=0;
		sprintf(buf,"[%s] <%s>\n",tcp_server_address,p);

		display("connecting to %s",buf);
		fflush(stdout);

		/* we need to switch to tcp mode */
		tcp_socket=tcp_connect(buf);
		dup2(tcp_socket,0);
		dup2(tcp_socket,1);
	}


	{
		/* we write max_blocklen (data) + 18 (ZModem protocol overhead)
		 * + escape overhead (about 4 %), so buffer has to be
		 * somewhat larger than max_blklen
		 */
		char *s=malloc(max_blklen+1024);
		if (!s)
		{
			log_error(_("out of memory"));
			exit(1);
		}
		setvbuf(stdout,s,_IOFBF,max_blklen+1024);
	}
	blklen=start_blklen;

	for (i=optind,stdin_files=0;i<argc;i++) {
		if (0==strcmp(argv[i],"-"))
			stdin_files++;
	}

	if (stdin_files>1) {
		usage(1,_("can read only one file from stdin"));
	} else if (stdin_files==1) {
		io_mode_fd=1;
	}
	zm->baudrate = io_mode(io_mode_fd,1);
	readline_setup(io_mode_fd, 128, 256);

	if (signal(SIGINT, bibi) == SIG_IGN)
		signal(SIGINT, SIG_IGN);
	else {
		signal(SIGINT, bibi);
		play_with_sigint=1;
	}
	signal(SIGTERM, bibi);
	signal(SIGPIPE, bibi);
	signal(SIGHUP, bibi);

	display("rz");
	fflush(stdout);
	countem(npats, patts);
	
	/* throw away any input already received. This doesn't harm
	 * as we invite the receiver to send it's data again, and
	 * might be useful if the receiver has already died or
	 * if there is dirt left if the line
	 */
	struct timeval t;
	unsigned char throwaway;
	fd_set f;

	purgeline(io_mode_fd);

	t.tv_sec = 0;
	t.tv_usec = 0;

	FD_ZERO(&f);
	FD_SET(io_mode_fd,&f);

	while (select(1,&f,NULL,NULL,&t)) {
		if (0==read(io_mode_fd,&throwaway,1)) /* EOF ... */
			break;
	}

	purgeline(io_mode_fd);
	zm_store_header(0L);
	zm_send_hex_header(zm, ZRQINIT, Txhdr);
	zrqinits_sent++;
	if (tcp_flag==1) {
		Totalleft+=256; /* tcp never needs more */
		Filesleft++;
	}
	fflush(stdout);

	if (wcsend(zm, npats, patts)==ERROR) {
		Exitcode=0200;
		canit(STDOUT_FILENO);
	}
	fflush(stdout);
	io_mode(io_mode_fd, 0);
	if (Exitcode)
		dm=Exitcode;
	else if (errcnt)
		dm=1;
	else
		dm=0;
	if (dm)
		log_info(_("Transfer incomplete"));
	else
		log_info(_("Transfer complete"));
	exit(dm);
	/*NOTREACHED*/
}

static int
send_pseudo(zm_t *zm, const char *name, const char *data)
{
	char *tmp;
	const char *p;
	int ret=0; /* ok */
	size_t plen;
	int fd;
	int lfd;

	p = getenv ("TMPDIR");
	if (!p)
		p = getenv ("TMP");
	if (!p)
		p = "/tmp";
	tmp=malloc(PATH_MAX+1);
	if (!tmp) {
		log_fatal(_("out of memory"));
		exit(1);
	}

	plen=strlen(p);
	memcpy(tmp,p,plen);
	tmp[plen++]='/';

	lfd=0;
	do {
		if (lfd++==10) {
			free(tmp);
			log_info (_ ("send_pseudo %s: cannot open tmpfile %s: %s"),
					 name, tmp, strerror (errno));
			return 1;
		}
		sprintf(tmp+plen,"%s.%lu.%d",name,(unsigned long) getpid(),lfd);
		fd=open(tmp,O_WRONLY|O_CREAT|O_EXCL,0700);
		/* is O_EXCL guaranted to not follow symlinks?
		 * I don`t know ... so be careful
		 */
		if (fd!=-1) {
			struct stat st;
			if (0!=lstat(tmp,&st)) {
				log_info (_ ("send_pseudo %s: cannot lstat tmpfile %s: %s"),
						 name, tmp, strerror (errno));
				unlink(tmp);
				close(fd);
				fd=-1;
			} else {
				if (S_ISLNK(st.st_mode)) {
					log_info (_ ("send_pseudo %s: avoiding symlink trap"),name);
					unlink(tmp);
					close(fd);
					fd=-1;
				}
			}
		}
	} while (fd==-1);
	if (write(fd,data,strlen(data))!=(signed long) strlen(data)
		|| close(fd)!=0) {
		log_info (_ ("send_pseudo %s: cannot write to tmpfile %s: %s"),
				 name, tmp, strerror (errno));
		free(tmp);
		return 1;
	}

	if (wcs (zm, tmp,name) == ERROR) {
		log_info (_ ("send_pseudo %s: failed"),name);
		ret=1;
	}
	unlink (tmp);
	free(tmp);
	return ret;
}

static int
wcsend (zm_t *zm, int argc, char *argp[])
{
	int n;

	Crcflg = FALSE;
	firstsec = TRUE;
	bytcnt = (size_t) -1;

	if (tcp_flag==1) {
		char buf[256];
		int d;

		/* tell receiver to receive via tcp */
		d=tcp_server(buf);
		if (send_pseudo(zm, "/$tcp$.t",buf)) {
			log_fatal(_("tcp protocol init failed"));
			exit(1);
		}
		/* ok, now that this file is sent we can switch to tcp */

		tcp_socket=tcp_accept(d);
		dup2(tcp_socket,0);
		dup2(tcp_socket,1);
	}

	for (n = 0; n < argc; ++n) {
		Totsecs = 0;
		if (wcs (zm, argp[n],NULL) == ERROR)
			return ERROR;
	}
	Totsecs = 0;
	if (Filcnt == 0) {			/* bitch if we couldn't open ANY files */
		canit(STDOUT_FILENO);
		log_info (_ ("Can't open any requested files."));
		return ERROR;
	}
	if (zm->zmodem_requested)
		saybibi (zm);
	else {
		struct zm_fileinfo zi;
		char pa[PATH_MAX+1];
		*pa='\0';
		zi.fname = pa;
		zi.modtime = 0;
		zi.mode = 0;
		zi.bytes_total = 0;
		zi.bytes_sent = 0;
		zi.bytes_received = 0;
		zi.bytes_skipped = 0;
		wctxpn (zm, &zi);
	}
	return OK;
}

static int
wcs(zm_t *zm, const char *oname, const char *remotename)
{
	struct stat f;
	char name[PATH_MAX+1];
	struct zm_fileinfo zi;
	int dont_mmap_this=0;
	if (Restricted) {
		/* restrict pathnames to current tree or uucppublic */
		if ( strstr(oname, "../")
#ifdef PUBDIR
		 || (oname[0]== '/' && strncmp(oname, MK_STRING(PUBDIR),
		 	strlen(MK_STRING(PUBDIR))))
#endif
		) {
			canit(STDOUT_FILENO);
			log_fatal(_("security violation: not allowed to upload from %s"),oname);
			exit(1);
		}
	}

	if (0==strcmp(oname,"-")) {
		char *p=getenv("ONAME");
		if (p) {
			strcpy(name, p);
		} else {
			sprintf(name, "s%lu.lsz", (unsigned long) getpid());
		}
		input_f=stdin;
		dont_mmap_this=1;
	} else if ((input_f=fopen(oname, "r"))==NULL) {
		int e=errno;
		log_error(_("cannot open %s: %s"), oname, strerror(e));
		++errcnt;
		return OK;	/* pass over it, there may be others */
	} else {
		strcpy(name, oname);
	}
	if (!use_mmap || dont_mmap_this)
	{
		static char *s=NULL;
		static size_t last_length=0;
		struct stat st;
		if (fstat(fileno(input_f),&st)==-1)
			st.st_size=1024*1024;
		if (buffersize==(size_t) -1 && s) {
			if ((size_t) st.st_size > last_length) {
				free(s);
				s=NULL;
				last_length=0;
			}
		}
		if (!s && buffersize) {
			last_length=16384;
			if (buffersize==(size_t) -1) {
				if (st.st_size>0)
					last_length=st.st_size;
			} else
				last_length=buffersize;
			/* buffer whole pages */
			last_length=(last_length+4095)&0xfffff000;
			s=malloc(last_length);
			if (!s) {
				log_fatal(_("out of memory"));
				exit(1);
			}
		}
		if (s) {
			setvbuf(input_f,s,_IOFBF,last_length);
		}
	}
	vpos = 0;
	/* Check for directory or block special files */
	fstat(fileno(input_f), &f);
	if (S_ISDIR(f.st_mode) || S_ISBLK(f.st_mode)) {
		log_error(_("is not a file: %s"), name);
		fclose(input_f);
		return OK;
	}

	if (remotename) {
		/* disqualify const */
		union {
			const char *c;
			char *s;
		} cheat;
		cheat.c=remotename;
		zi.fname=cheat.s;
	} else
		zi.fname=name;
	zi.modtime=f.st_mtime;
	zi.mode=f.st_mode;
	zi.bytes_total= (S_ISFIFO(f.st_mode)) ? DEFBYTL : f.st_size;
	zi.bytes_sent=0;
	zi.bytes_received=0;
	zi.bytes_skipped=0;
	zi.eof_seen=0;
	timing(1,NULL);

	++Filcnt;
	switch (wctxpn(zm, &zi)) {
	case ERROR:
		return ERROR;
	case ZSKIP:
		log_error(_("skipped: %s"), name);
		return OK;
	}
	if (!zm->zmodem_requested && wctx(zm, &zi)==ERROR)
	{
		return ERROR;
	}
	if (Unlinkafter)
		unlink(oname);

	long bps;
	double d=timing(0,NULL);
	if (d==0) /* can happen if timing() uses time() */
		d=0.5;
	bps=zi.bytes_sent/d;
	log_debug(_("Bytes Sent:%7ld   BPS:%-8ld"),
		  (long) zi.bytes_sent,bps);
	return 0;
}

/*
 * generate and transmit pathname block consisting of
 *  pathname (null terminated),
 *  file length, mode time and file mode in octal
 *  as provided by the Unix fstat call.
 *  N.B.: modifies the passed name, may extend it!
 */
static int
wctxpn(zm_t *zm, struct zm_fileinfo *zi)
{
	register char *p, *q;
	char name2[PATH_MAX+1];
	struct stat f;

	if (!zm->zmodem_requested)
		if (getnak(zm)) {
			log_debug("getnak failed");
			return ERROR;
		}

	q = (char *) 0;

	for (p=zi->fname, q=txbuf ; *p; )
		if ((*q++ = *p++) == '/' && !Fullname)
			q = txbuf;
	*q++ = 0;
	p=q;
	while (q < (txbuf + MAX_BLOCK))
		*q++ = 0;
	if ((input_f!=stdin) && *zi->fname && (fstat(fileno(input_f), &f)!= -1)) {
		if (hyperterm) {
			sprintf(p, "%lu", (long) f.st_size);
		} else {
			/* note that we may lose some information here
			 * in case mode_t is wider than an int. But i believe
			 * sending %lo instead of %o _could_ break compatability
			 */
			sprintf(p, "%lu %lo %o 0 %d %ld", (long) f.st_size,
				f.st_mtime,
				(unsigned int)((no_unixmode) ? 0 : f.st_mode),
				Filesleft, Totalleft);
		}
	}
	log_info(_("Sending: %s"),txbuf);
	Totalleft -= f.st_size;
	if (--Filesleft <= 0)
		Totalleft = 0;
	if (Totalleft < 0)
		Totalleft = 0;

	/* force 1k blocks if name won't fit in 128 byte block */
	if (txbuf[125])
		blklen=1024;
	else {		/* A little goodie for IMP/KMD */
		txbuf[127] = (f.st_size + 127) >>7;
		txbuf[126] = (f.st_size + 127) >>15;
	}
	if (zm->zmodem_requested)
		return zsendfile(zm, zi,txbuf, 1+strlen(p)+(p-txbuf));
	if (wcputsec(zm, txbuf, 0, 128)==ERROR) {
		log_debug("wcputsec failed");
		return ERROR;
	}
	return OK;
}

static int
getnak(zm_t *zm)
{
	int firstch;
	int tries=0;

	Lastrx = 0;
	for (;;) {
		tries++;
		switch (firstch = READLINE_PF(100)) {
		case ZPAD:
			if (getzrxinit(zm))
				return ERROR;
			return FALSE;
		case TIMEOUT:
			/* 30 seconds are enough */
			if (tries==3) {
				log_error(_("Timeout on pathname"));
				return TRUE;
			}
			/* don't send a second ZRQINIT _directly_ after the
			 * first one. Never send more then 4 ZRQINIT, because
			 * omen rz stops if it saw 5 of them */
			if ((zrqinits_sent>1 || tries>1) && zrqinits_sent<4) {
				/* if we already sent a ZRQINIT we are using zmodem
				 * protocol and may send further ZRQINITs
				 */
				zm_store_header(0L);
				zm_send_hex_header(zm, ZRQINIT, Txhdr);
				zrqinits_sent++;
			}
			continue;
		case WANTG:
			io_mode(io_mode_fd, 2);	/* Set cbreak, XON/XOFF, etc. */
			Optiong = TRUE;
			blklen=1024;
		case WANTCRC:
			Crcflg = TRUE;
		case NAK:
			return FALSE;
		case CAN:
			if ((firstch = READLINE_PF(20)) == CAN && Lastrx == CAN)
				return TRUE;
		default:
			break;
		}
		Lastrx = firstch;
	}
}


static int
wctx(zm_t *zm, struct zm_fileinfo *zi)
{
	register size_t thisblklen;
	register int sectnum, attempts, firstch;

	firstsec=TRUE;  thisblklen = blklen;
	log_debug("wctx:file length=%ld", (long) zi->bytes_total);

	while ((firstch=READLINE_PF(zm->rxtimeout))!=NAK && firstch != WANTCRC
	  && firstch != WANTG && firstch!=TIMEOUT && firstch!=CAN)
		;
	if (firstch==CAN) {
		log_error(_("Receiver Cancelled"));
		return ERROR;
	}
	if (firstch==WANTCRC)
		Crcflg=TRUE;
	if (firstch==WANTG)
		Crcflg=TRUE;
	sectnum=0;
	for (;;) {
		if (zi->bytes_total <= (zi->bytes_sent + 896L))
			thisblklen = 128;
		if ( !filbuf(txbuf, thisblklen))
			break;
		if (wcputsec(zm, txbuf, ++sectnum, thisblklen)==ERROR)
			return ERROR;
		zi->bytes_sent += thisblklen;
	}
	fclose(input_f);
	attempts=0;
	do {
		purgeline(io_mode_fd);
		putchar(EOT);
		fflush(stdout);
		++attempts;
	} while ((firstch=(READLINE_PF(zm->rxtimeout)) != ACK) && attempts < RETRYMAX);
	if (attempts == RETRYMAX) {
		log_error(_("No ACK on EOT"));
		return ERROR;
	}
	else
		return OK;
}

static int
wcputsec(zm_t *zm, char *buf, int sectnum, size_t cseclen)
{
	int checksum, wcj;
	char *cp;
	unsigned oldcrc;
	int firstch;
	int attempts;

	firstch=0;	/* part of logic to detect CAN CAN */

	log_debug(_("Zmodem sectors/kbytes sent: %3d/%2dk"), Totsecs, Totsecs/8 );
	for (attempts=0; attempts <= RETRYMAX; attempts++) {
		Lastrx= firstch;
		putchar(cseclen==1024?STX:SOH);
		putchar(sectnum & 0xFF);
		/* FIXME: clarify the following line - mlg */
		putchar((-sectnum -1) & 0xFF);
		oldcrc=checksum=0;
		for (wcj=cseclen,cp=buf; --wcj>=0; ) {
			putchar(*cp);
			oldcrc=updcrc((0377& *cp), oldcrc);
			checksum += *cp++;
		}
		if (Crcflg) {
			oldcrc=updcrc(0,updcrc(0,oldcrc));
			putchar(((int)oldcrc>>8) & 0xFF);
			putchar(((int)oldcrc) & 0xFF);
		}
		else
			putchar(checksum & 0xFF);

		fflush(stdout);
		if (Optiong) {
			firstsec = FALSE; return OK;
		}
		firstch = READLINE_PF(zm->rxtimeout);
gotnak:
		switch (firstch) {
		case CAN:
			if(Lastrx == CAN) {
cancan:
				log_error(_("Cancelled"));  return ERROR;
			}
			break;
		case TIMEOUT:
			log_error(_("Timeout on sector ACK")); continue;
		case WANTCRC:
			if (firstsec)
				Crcflg = TRUE;
		case NAK:
			log_error(_("NAK on sector")); continue;
		case ACK:
			firstsec=FALSE;
			Totsecs += (cseclen>>7);
			return OK;
		case ERROR:
			log_error(_("Got burst for sector ACK")); break;
		default:
			log_error(_("Got %02x for sector ACK"), firstch); break;
		}
		for (;;) {
			Lastrx = firstch;
			if ((firstch = READLINE_PF(zm->rxtimeout)) == TIMEOUT)
				break;
			if (firstch == NAK || firstch == WANTCRC)
				goto gotnak;
			if (firstch == CAN && Lastrx == CAN)
				goto cancan;
		}
	}
	log_error(_("Retry Count Exceeded"));
	return ERROR;
}

/* fill buf with count chars padding with ^Z for CPM */
static size_t
filbuf(char *buf, size_t count)
{
	int c;
	size_t m;

	m = read(fileno(input_f), buf, count);
	if (m <= 0)
		return 0;
	while (m < count)
		buf[m++] = 032;
	return count;
	m=count;
	if (Lfseen) {
		*buf++ = 012; --m; Lfseen = 0;
	}
	while ((c=getc(input_f))!=EOF) {
		if (c == 012) {
			*buf++ = 015;
			if (--m == 0) {
				Lfseen = TRUE; break;
			}
		}
		*buf++ =c;
		if (--m == 0)
			break;
	}
	if (m==count)
		return 0;
	else
		while (m--!=0)
			*buf++ = CPMEOF;
	return count;
}

/* Fill buffer with blklen chars */
static size_t
zfilbuf (struct zm_fileinfo *zi)
{
	size_t n;

	n = fread (txbuf, 1, blklen, input_f);
	if (n < blklen)
		zi->eof_seen = 1;
	else {
		/* save one empty paket in case file ends ob blklen boundary */
		int c = getc(input_f);

		if (c != EOF || !feof(input_f))
			ungetc(c, input_f);
		else
			zi->eof_seen = 1;
	}
	return n;
}

static void
usage1 (int exitcode)
{
	usage (exitcode, NULL);
}

static void
usage(int exitcode, const char *what)
{
	if (exitcode)
	{
		if (what)
			log_info("%s: %s",program_name,what);
		log_info (_("Try `%s --help' for more information."), program_name);
		exit(exitcode);
	}

	display(_("%s version %s"), program_name, VERSION);

	display(_("Usage: %s [options] file ..."), program_name);
	display(_("Send file(s) with ZMODEM protocol"));
	display(_(
		"    (Z) = option applies to ZMODEM only\n"
		));
	/* splitted into two halves for really bad compilers */
	display(_(
"  -+, --append                append to existing destination file (Z)\n"
"  -4, --try-4k                go up to 4K blocksize\n"
"      --start-4k              start with 4K blocksize (doesn't try 8)\n"
"  -8, --try-8k                go up to 8K blocksize\n"
"      --start-8k              start with 8K blocksize\n"
"  -b, --binary                binary transfer\n"
"  -B, --bufsize N             buffer N bytes (N==auto: buffer whole file)\n"
"      --delay-startup N       sleep N seconds before doing anything\n"
"  -e, --escape                escape all control characters (Z)\n"
"  -E, --rename                force receiver to rename files it already has\n"
"  -f, --full-path             send full pathname (Y/Z)\n"
"  -h, --help                  print this usage message\n"
"  -L, --packetlen N           limit subpacket length to N bytes (Z)\n"
"  -l, --framelen N            limit frame length to N bytes (l>=L) (Z)\n"
"  -m, --min-bps N             stop transmission if BPS below N\n"
"  -M, --min-bps-time N          for at least N seconds (default: 120)\n"
		));
	display(_(
"  -n, --newer                 send file if source newer (Z)\n"
"  -N, --newer-or-longer       send file if source newer or longer (Z)\n"
"  -o, --16-bit-crc            use 16 bit CRC instead of 32 bit CRC (Z)\n"
"  -O, --disable-timeouts      disable timeout code, wait forever\n"
"  -p, --protect               protect existing destination file (Z)\n"
"  -r, --resume                resume interrupted file transfer (Z)\n"
"  -R, --restricted            restricted, more secure mode\n"
"  -q, --quiet                 quiet (no progress reports)\n"
"  -s, --stop-at {HH:MM|+N}    stop transmission at HH:MM or in N seconds\n"
"      --tcp                   build a TCP connection to transmit files\n"
"      --tcp-server            open socket, wait for connection\n"
"  -u, --unlink                unlink file after transmission\n"
"  -U, --unrestrict            turn off restricted mode (if allowed to)\n"
"  -v, --verbose               be verbose, provide debugging information\n"
"  -w, --windowsize N          Window is N bytes (Z)\n"
"  -y, --overwrite             overwrite existing files\n"
"  -Y, --overwrite-or-skip     overwrite existing files, else skip\n"
"\n"
"short options use the same arguments as the long ones\n"
	));
	exit(exitcode);
}

/*
 * Get the receiver's init parameters
 */
static int
getzrxinit(zm_t *zm)
{
	static int dont_send_zrqinit=1;
	int old_timeout=zm->rxtimeout;
	int n;
	struct stat f;
	size_t rxpos;
	int timeouts=0;

	zm->rxtimeout=100; /* 10 seconds */
	/* XXX purgeline(io_mode_fd); this makes _real_ trouble. why? -- uwe */

	for (n=10; --n>=0; ) {
		/* we might need to send another zrqinit in case the first is
		 * lost. But *not* if getting here for the first time - in
		 * this case we might just get a ZRINIT for our first ZRQINIT.
		 * Never send more then 4 ZRQINIT, because
		 * omen rz stops if it saw 5 of them.
		 */
		if (zrqinits_sent<4 && n!=10 && !dont_send_zrqinit) {
			zrqinits_sent++;
			zm_store_header(0L);
			zm_send_hex_header(zm, ZRQINIT, Txhdr);
		}
		dont_send_zrqinit=0;

		switch (zm_get_header(zm, Rxhdr, &rxpos)) {
		case ZCHALLENGE:	/* Echo receiver's challenge numbr */
			zm_store_header(rxpos);
			zm_send_hex_header(zm, ZACK, Txhdr);
			continue;
		case ZRINIT:
			Rxflags = 0377 & Rxhdr[ZF0];
			Rxflags2 = 0377 & Rxhdr[ZF1];
			zm->txfcs32 = (Wantfcs32 && (Rxflags & CANFC32));
			{
				int old=zm->zctlesc;
				zm->zctlesc |= Rxflags & TESCCTL;
				/* update table - was initialised to not escape */
				if (zm->zctlesc && !old)
					zm_update_table(zm);
			}
			Rxbuflen = (0377 & Rxhdr[ZP0])+((0377 & Rxhdr[ZP1])<<8);
			if ( !(Rxflags & CANFDX))
				Txwindow = 0;
			log_debug("Rxbuflen=%d Tframlen=%d", Rxbuflen, Tframlen);
			if ( play_with_sigint)
				signal(SIGINT, SIG_IGN);
			io_mode(io_mode_fd,2);	/* Set cbreak, XON/XOFF, etc. */
			/* Override to force shorter frame length */
			if (Tframlen && Rxbuflen > Tframlen)
				Rxbuflen = Tframlen;
			if ( !Rxbuflen)
				Rxbuflen = 1024;
			log_debug("Rxbuflen=%d", Rxbuflen);

			/* If using a pipe for testing set lower buf len */
			fstat(0, &f);
			if (! (S_ISCHR(f.st_mode))) {
				Rxbuflen = MAX_BLOCK;
			}
			/*
			 * If input is not a regular file, force ACK's to
			 *  prevent running beyond the buffer limits
			 */
			fstat(fileno(input_f), &f);
			if (!(S_ISREG(f.st_mode))) {
				Canseek = -1;
				/* return ERROR; */
			}
			/* Set initial subpacket length */
			if (blklen < 1024) {	/* Command line override? */
				if (zm->baudrate > 300)
					blklen = 256;
				if (zm->baudrate > 1200)
					blklen = 512;
				if (zm->baudrate > 2400)
					blklen = 1024;
			}
			if (Rxbuflen && blklen>Rxbuflen)
				blklen = Rxbuflen;
			if (blkopt && blklen > blkopt)
				blklen = blkopt;
			log_debug("Rxbuflen=%d blklen=%d", Rxbuflen, blklen);
			log_debug("Txwindow = %u Txwspac = %d", Txwindow, Txwspac);
			zm->rxtimeout=old_timeout;
			return (sendzsinit(zm));
		case ZCAN:
		case TIMEOUT:
			if (timeouts++==0)
				continue; /* force one other ZRQINIT to be sent */
			return ERROR;
		case ZRQINIT:
			if (Rxhdr[ZF0] == ZCOMMAND)
				continue;
		default:
			zm_send_hex_header(zm, ZNAK, Txhdr);
			continue;
		}
	}
	return ERROR;
}

/* Send send-init information */
static int
sendzsinit(zm_t *zm)
{
	int c;

	if (Myattn[0] == '\0' && (!zm->zctlesc || (Rxflags & TESCCTL)))
		return OK;
	errors = 0;
	for (;;) {
		zm_store_header(0L);
		if (zm->zctlesc) {
			Txhdr[ZF0] |= TESCCTL; zm_send_hex_header(zm, ZSINIT, Txhdr);
		}
		else
			zm_send_binary_header(zm, ZSINIT, Txhdr);
		ZM_SEND_DATA(Myattn, 1+strlen(Myattn), ZCRCW);
		c = zm_get_header(zm, Rxhdr, NULL);
		switch (c) {
		case ZCAN:
			return ERROR;
		case ZACK:
			return OK;
		default:
			if (++errors > 19)
				return ERROR;
			continue;
		}
	}
}

/* Send file name and related info */
static int
zsendfile(zm_t *zm, struct zm_fileinfo *zi, const char *buf, size_t blen)
{
	int c;
	unsigned long crc;
	size_t rxpos;

	/* we are going to send a ZFILE. There cannot be much useful
	 * stuff in the line right now (*except* ZCAN?).
	 */

	for (;;) {
		Txhdr[ZF0] = Lzconv;	/* file conversion request */
		Txhdr[ZF1] = Lzmanag;	/* file management request */
		if (Lskipnocor)
			Txhdr[ZF1] |= ZF1_ZMSKNOLOC;
		Txhdr[ZF2] = Lztrans;	/* file transport request */
		Txhdr[ZF3] = 0;
		zm_send_binary_header(zm, ZFILE, Txhdr);
		ZM_SEND_DATA(buf, blen, ZCRCW);
again:
		c = zm_get_header(zm, Rxhdr, &rxpos);
		switch (c) {
		case ZRINIT:
			while ((c = READLINE_PF(50)) > 0)
				if (c == ZPAD) {
					goto again;
				}
			/* **** FALL THRU TO **** */
		default:
			continue;
		case ZRQINIT:  /* remote site is sender! */
			log_info(_("got ZRQINIT"));
			return ERROR;
		case ZCAN:
			log_info(_("got ZCAN"));
			return ERROR;
		case TIMEOUT:
			return ERROR;
		case ZABORT:
			return ERROR;
		case ZFIN:
			return ERROR;
		case ZCRC:
			crc = 0xFFFFFFFFL;
			if (use_mmap && !mm_addr)
			{
				struct stat st;
				if (fstat (fileno (input_f), &st) == 0) {
					mm_size = st.st_size;
					mm_addr = mmap (0, mm_size, PROT_READ,
									MAP_SHARED, fileno (input_f), 0);
					if ((caddr_t) mm_addr == (caddr_t) - 1)
						mm_addr = NULL;
					else {
						fclose (input_f);
						input_f = NULL;
					}
				}
			}
			if (mm_addr) {
				size_t i;
				size_t count;
				char *p=mm_addr;
				count=(rxpos < mm_size && rxpos > 0)? rxpos: mm_size;
				for (i=0;i<count;i++,p++) {
					crc = UPDC32(*p, crc);
				}
				crc = ~crc;
			} else
			if (Canseek >= 0) {
				if (rxpos==0) {
					struct stat st;
					if (0==fstat(fileno(input_f),&st)) {
						rxpos=st.st_size;
					} else
						rxpos=-1;
				}
				while (rxpos-- && ((c = getc(input_f)) != EOF))
					crc = UPDC32(c, crc);
				crc = ~crc;
				clearerr(input_f);	/* Clear EOF */
				fseek(input_f, 0L, 0);
			}
			zm_store_header(crc);
			zm_send_binary_header(zm, ZCRC, Txhdr);
			goto again;
		case ZSKIP:
			if (input_f)
				fclose(input_f);
			else if (mm_addr) {
				munmap(mm_addr,mm_size);
				mm_addr=NULL;
			}

			log_debug("receiver skipped");
			return c;
		case ZRPOS:
			/*
			 * Suppress zcrcw request otherwise triggered by
			 * lastsync==bytcnt
			 */
			if (!mm_addr)
			if (rxpos && fseek(input_f, (long) rxpos, 0)) {
				int er=errno;
				log_debug("fseek failed: %s", strerror(er));
				return ERROR;
			}
			if (rxpos)
				zi->bytes_skipped=rxpos;
			bytcnt = zi->bytes_sent = rxpos;
			Lastsync = rxpos -1;
	 		return zsendfdata(zm, zi);
		}
	}
}

/* Send the data in the file */
static int
zsendfdata (zm_t *zm, struct zm_fileinfo *zi)
{
	static int c;
	static int junkcount;				/* Counts garbage chars received by TX */
	static size_t last_txpos = 0;
	static long last_bps = 0;
	static long not_printed = 0;
	static long total_sent = 0;
	static time_t low_bps=0;

	if (use_mmap && !mm_addr)
	{
		struct stat st;
		if (fstat (fileno (input_f), &st) == 0) {
			mm_size = st.st_size;
			mm_addr = mmap (0, mm_size, PROT_READ,
							MAP_SHARED, fileno (input_f), 0);
			if ((caddr_t) mm_addr == (caddr_t) - 1)
				mm_addr = NULL;
			else {
				fclose (input_f);
				input_f = NULL;
			}
		}
	}

	if (play_with_sigint)
		signal (SIGINT, onintr);

	Lrxpos = 0;
	junkcount = 0;
	Beenhereb4 = 0;
  somemore:
	if (setjmp (intrjmp)) {
	  if (play_with_sigint)
		  signal (SIGINT, onintr);
	  waitack:
		junkcount = 0;
		c = getinsync (zm, zi, 0);
	  gotack:
		switch (c) {
		default:
			if (input_f)
				fclose (input_f);
			return ERROR;
		case ZCAN:
			if (input_f)
				fclose (input_f);
			return ERROR;
		case ZSKIP:
			if (input_f)
				fclose (input_f);
			return c;
		case ZACK:
		case ZRPOS:
			break;
		case ZRINIT:
			return OK;
		}
		/*
		 * If the reverse channel can be tested for data,
		 *  this logic may be used to detect error packets
		 *  sent by the receiver, in place of setjmp/longjmp
		 *  rdchk(fdes) returns non 0 if a character is available
		 */
		while (rdchk (io_mode_fd)) {
			switch (READLINE_PF (1))
			{
			case CAN:
			case ZPAD:
				c = getinsync (zm, zi, 1);
				goto gotack;
			case XOFF:			/* Wait a while for an XON */
			case XOFF | 0200:
				READLINE_PF (100);
			}
		}
	}

	Txwcnt = 0;
	zm_store_header (zi->bytes_sent);
	zm_send_binary_header (zm, ZDATA, Txhdr);

	do {
		size_t n;
		int e;
		unsigned old = blklen;
		blklen = calc_blklen (total_sent);
		total_sent += blklen + OVERHEAD;
		if (blklen != old)
			log_trace (_("blklen now %d\n"), blklen);
		if (mm_addr) {
			if (zi->bytes_sent + blklen < mm_size)
				n = blklen;
			else {
				n = mm_size - zi->bytes_sent;
				zi->eof_seen = 1;
			}
		} else
			n = zfilbuf (zi);
		if (zi->eof_seen) {
			e = ZCRCE;
			log_trace("e=ZCRCE/eof seen");
		} else if (junkcount > 3) {
			e = ZCRCW;
			log_trace("e=ZCRCW/junkcount > 3");
		} else if (bytcnt == Lastsync) {
			e = ZCRCW;
			log_trace("e=ZCRCW/bytcnt == Lastsync == %ld",
					(unsigned long) Lastsync);
		} else if (Txwindow && (Txwcnt += n) >= Txwspac) {
			Txwcnt = 0;
			e = ZCRCQ;
			log_trace("e=ZCRCQ/Window");
		} else {
			e = ZCRCG;
			log_trace("e=ZCRCG");
		}
		if ((min_bps || stop_time)
			&& (not_printed > (min_bps ? 3 : 7)
				|| zi->bytes_sent > last_bps / 2 + last_txpos)) {
			int minleft = 0;
			int secleft = 0;
			time_t now;
			last_bps = (zi->bytes_sent / timing (0,&now));
			if (last_bps > 0) {
				minleft = (zi->bytes_total - zi->bytes_sent) / last_bps / 60;
				secleft = ((zi->bytes_total - zi->bytes_sent) / last_bps) % 60;
			}
			if (min_bps) {
				if (low_bps) {
					if (last_bps<min_bps) {
						if (now-low_bps>=min_bps_time) {
							/* too bad */
							log_info(_("zsendfdata: bps rate %ld below min %ld"),
								 last_bps, min_bps);
							return ERROR;
						}
					} else
						low_bps=0;
				} else if (last_bps < min_bps) {
					low_bps=now;
				}
			}
			if (stop_time && now>=stop_time) {
				/* too bad */
				log_info(_("zsendfdata: reached stop time"));
				return ERROR;
			}

			log_debug (_("Bytes Sent:%7ld/%7ld   BPS:%-8ld ETA %02d:%02d  "),
				  (long) zi->bytes_sent, (long) zi->bytes_total,
				  last_bps, minleft, secleft);
			last_txpos = zi->bytes_sent;
		} else
			not_printed++;
		ZM_SEND_DATA (DATAADR, n, e);
		bytcnt = zi->bytes_sent += n;
		if (e == ZCRCW)
			goto waitack;
		/*
		 * If the reverse channel can be tested for data,
		 *  this logic may be used to detect error packets
		 *  sent by the receiver, in place of setjmp/longjmp
		 *  rdchk(fdes) returns non 0 if a character is available
		 */
		fflush (stdout);
		while (rdchk (io_mode_fd)) {
			switch (READLINE_PF (1))
			{
			case CAN:
			case ZPAD:
				c = getinsync (zm, zi, 1);
				if (c == ZACK)
					break;
				/* zcrce - dinna wanna starta ping-pong game */
				ZM_SEND_DATA (txbuf, 0, ZCRCE);
				goto gotack;
			case XOFF:			/* Wait a while for an XON */
			case XOFF | 0200:
				READLINE_PF (100);
			default:
				++junkcount;
			}
		}
		if (Txwindow) {
			size_t tcount = 0;
			while ((tcount = zi->bytes_sent - Lrxpos) >= Txwindow) {
				log_debug ("%ld (%ld,%ld) window >= %u", tcount,
					(long) zi->bytes_sent, (long) Lrxpos,
					Txwindow);
				if (e != ZCRCQ)
					ZM_SEND_DATA (txbuf, 0, e = ZCRCQ);
				c = getinsync (zm, zi, 1);
				if (c != ZACK) {
					ZM_SEND_DATA (txbuf, 0, ZCRCE);
					goto gotack;
				}
			}
			log_debug ("window = %ld", tcount);
		}
	} while (!zi->eof_seen);


	if (play_with_sigint)
		signal (SIGINT, SIG_IGN);

	for (;;) {
		zm_store_header (zi->bytes_sent);
		zm_send_binary_header (zm, ZEOF, Txhdr);
		switch (getinsync (zm, zi, 0)) {
		case ZACK:
			continue;
		case ZRPOS:
			goto somemore;
		case ZRINIT:
			return OK;
		case ZSKIP:
			if (input_f)
				fclose (input_f);
			return c;
		default:
			if (input_f)
				fclose (input_f);
			return ERROR;
		}
	}
}

static int
calc_blklen(long total_sent)
{
	static long total_bytes=0;
	static int calcs_done=0;
	static long last_error_count=0;
	static int last_blklen=0;
	static long last_bytes_per_error=0;
	unsigned long best_bytes=0;
	long best_size=0;
	long this_bytes_per_error;
	long d;
	unsigned int i;
	if (total_bytes==0)
	{
		/* called from countem */
		total_bytes=total_sent;
		return 0;
	}

	/* it's not good to calc blklen too early */
	if (calcs_done++ < 5) {
		if (error_count && start_blklen >1024)
			return last_blklen=1024;
		else
			last_blklen/=2;
		return last_blklen=start_blklen;
	}

	if (!error_count) {
		/* that's fine */
		if (start_blklen==max_blklen)
			return start_blklen;
		this_bytes_per_error=LONG_MAX;
		goto calcit;
	}

	if (error_count!=last_error_count) {
		/* the last block was bad. shorten blocks until one block is
		 * ok. this is because very often many errors come in an
		 * short period */
		if (error_count & 2)
		{
			last_blklen/=2;
			if (last_blklen < 32)
				last_blklen = 32;
			else if (last_blklen > 512)
				last_blklen=512;
			log_trace(_("calc_blklen: reduced to %d due to error\n"),
				 last_blklen);
		}
		last_error_count=error_count;
		last_bytes_per_error=0; /* force recalc */
		return last_blklen;
	}

	this_bytes_per_error=total_sent / error_count;
		/* we do not get told about every error, because
		 * there may be more than one error per failed block.
		 * but one the other hand some errors are reported more
		 * than once: If a modem buffers more than one block we
		 * get at least two ZRPOS for the same position in case
		 * *one* block has to be resent.
		 * so don't do this:
		 * this_bytes_per_error/=2;
		 */
	/* there has to be a margin */
	if (this_bytes_per_error<100)
		this_bytes_per_error=100;

	/* be nice to the poor machine and do the complicated things not
	 * too often
	 */
	if (last_bytes_per_error>this_bytes_per_error)
		d=last_bytes_per_error-this_bytes_per_error;
	else
		d=this_bytes_per_error-last_bytes_per_error;
	if (d<4)
	{
		log_trace(_("calc_blklen: returned old value %d due to low bpe diff\n"),
			 last_blklen);
		log_trace(_("calc_blklen: old %ld, new %ld, d %ld\n"),
			 last_bytes_per_error,this_bytes_per_error,d );
		return last_blklen;
	}
	last_bytes_per_error=this_bytes_per_error;

calcit:
	log_trace(_("calc_blklen: calc total_bytes=%ld, bpe=%ld, ec=%ld\n"),
		 total_bytes,this_bytes_per_error,(long) error_count);
	for (i=32;i<=max_blklen;i*=2) {
		long ok; /* some many ok blocks do we need */
		long failed; /* and that's the number of blocks not transmitted ok */
		unsigned long transmitted;
		ok=total_bytes / i + 1;
		failed=((long) i + OVERHEAD) * ok / this_bytes_per_error;
		transmitted=total_bytes + ok * OVERHEAD
			+ failed * ((long) i+OVERHEAD+OVER_ERR);
		log_trace(_("calc_blklen: blklen %d, ok %ld, failed %ld -> %lu\n"),
			  i,ok,failed,transmitted);
		if (transmitted < best_bytes || !best_bytes)
		{
			best_bytes=transmitted;
			best_size=i;
		}
	}
	if (best_size > 2*last_blklen)
		best_size=2*last_blklen;
	last_blklen=best_size;
	log_trace(_("calc_blklen: returned %d as best\n"),
		  last_blklen);
	return last_blklen;
}

/*
 * Respond to receiver's complaint, get back in sync with receiver
 */
static int
getinsync(zm_t *zm, struct zm_fileinfo *zi, int flag)
{
	int c;
	size_t rxpos;

	for (;;) {
		c = zm_get_header(zm, Rxhdr, &rxpos);
		switch (c) {
		case ZCAN:
		case ZABORT:
		case ZFIN:
		case TIMEOUT:
			return ERROR;
		case ZRPOS:
			/* ************************************* */
			/*  If sending to a buffered modem, you  */
			/*   might send a break at this point to */
			/*   dump the modem's buffer.		 */
			if (input_f)
				clearerr(input_f);	/* In case file EOF seen */
			if (!mm_addr)
			if (fseek(input_f, (long) rxpos, 0))
				return ERROR;
			zi->eof_seen = 0;
			bytcnt = Lrxpos = zi->bytes_sent = rxpos;
			if (Lastsync == rxpos) {
				error_count++;
			}
			Lastsync = rxpos;
			return c;
		case ZACK:
			Lrxpos = rxpos;
			if (flag || zi->bytes_sent == rxpos)
				return ZACK;
			continue;
		case ZRINIT:
		case ZSKIP:
			if (input_f)
				fclose(input_f);
			else if (mm_addr) {
				munmap(mm_addr,mm_size);
				mm_addr=NULL;
			}
			return c;
		case ERROR:
		default:
			error_count++;
			zm_send_binary_header(zm, ZNAK, Txhdr);
			continue;
		}
	}
}


/* Say "bibi" to the receiver, try to do it cleanly */
static void
saybibi(zm_t *zm)
{
	for (;;) {
		zm_store_header(0L);		/* CAF Was zm_send_binary_header - minor change */
		zm_send_hex_header(zm, ZFIN, Txhdr);	/*  to make debugging easier */
		switch (zm_get_header(zm, Rxhdr,NULL)) {
		case ZFIN:
			putchar('O');
			putchar('O');
			fflush(stdout);
		case ZCAN:
		case TIMEOUT:
			return;
		}
	}
}

static void
chkinvok (const char *s)
{
	const char *p;

	p = s;
	while (*p == '-')
		s = ++p;
	while (*p)
		if (*p++ == '/')
			s = p;
	if (*s == 'v') {
		Verbose = LOG_INFO;
		++s;
	}
	program_name = s;
	if (*s == 'l')
		s++;					/* lsz -> sz */
}

static void
countem (int argc, char **argv)
{
	struct stat f;

	for (Totalleft = 0, Filesleft = 0; --argc >= 0; ++argv) {
		f.st_size = -1;
		log_trace ("\nCountem: %03d %s ", argc, *argv);
		if (access (*argv, R_OK) >= 0 && stat (*argv, &f) >= 0) {
			if (!S_ISDIR(f.st_mode) && !S_ISBLK(f.st_mode)) {
				++Filesleft;
				Totalleft += f.st_size;
			}
		} else if (strcmp (*argv, "-") == 0) {
			++Filesleft;
			Totalleft += DEFBYTL;
		}
		log_trace (" %ld", (long) f.st_size);
	}
	log_trace (_("\ncountem: Total %d %ld\n"),
				 Filesleft, Totalleft);
	calc_blklen (Totalleft);
}

/* End of lsz.c */
