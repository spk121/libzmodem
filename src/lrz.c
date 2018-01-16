/*
  lrz - receive files with x/y/zmodem
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

#define SS_NORMAL 0
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <utime.h>
#include <time.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "timing.h"
#include "long-options.h"
#include "xstrtoul.h"
#include "log.h"

#define MAX_BLOCK 8192

const char *program_name;		/* the name by which we were called */

static int no_timeout=FALSE;

struct rz_ {
	// Dynamic state
	FILE *fout;		/* FP to output file. */
	int lastrx;		/* Either 0, or CAN if last receipt
				 * was sender-cancelled */
	int firstsec;
	int errors;		/* Count of read failures */
	int skip_if_not_found;  /* When true, the operation opens and
				 * appends to an existing file. */
	char *pathname;		/* filename of the file being received */
	int thisbinary;		/* When > 0, current file is to be
				 * received in bin mode */
	int in_tcpsync;		/* True when we receive special file
				 * '$tcp$.t' */
	int tcp_socket;		/* A socket file descriptor */
	char zconv;		/* ZMODEM file conversion request. */
	char zmanag;		/* ZMODEM file management request. */
	char ztrans;		/* SET BUT UNUSED: ZMODEM file transport
				 * request byte */
	int tryzhdrtype;         /* Header type to send corresponding
				  * to Last rx close */
	char tcp_buf[256];	/* Buffer to receive TCP protocol
				 * synchronization strings from
				 * fout */
	char attn[ZATTNLEN+1];  /* Attention string rx sends to tx on err */
	char secbuf[MAX_BLOCK + 1]; /* Workspace to store up to 8k
				     * blocks */

	// Constant
	int restricted;	/* restricted; no /.. or ../ in filenames */
	                  /* restricted > 0 prevents remote commands
			     restricted > 0 prevents unlinking
			     restricted > 0 prevents overwriting dot files
			     restricted = 2 prevents overwriting files
			     restricted > 0 restrict files to curdir or PUBDIR
			   */
	int topipe;		/* A flag. When true, open the file as a pipe. */
	int makelcpathname;  /* A flag. When true, make received pathname lowercase. */
	int nflag;		/* A flag. Don't really transfer files */
	int rxclob;		/* A flag. Allow clobbering existing file */
	int rxbinary;	/* A flag. Receive all files in bin mode */
	int rxascii;	/* A flag. Receive files in ascii (translate) mode */
	int try_resume; /* A flag. When true, try restarting downloads */
	int allow_remote_commands; /* A flag. Run ZCOMMAND blocks as command. */
	int junk_path;  /* A flag. When true, ignore the path
			 * component of an incoming filename. */
	int tcp_flag;		/* Not a flag.
				* 0 = don't use TCP
				* 2 = tcp server
				* 3 = tcp client */
	int o_sync;		/* A flag. When true, each write will
				 * be reliably completed before
				 * returning. */
	long buffersize;	/* The size of the backing store
				 * buffer for 'fout' as set by
				 * setvbuf. -1 is automatic. Code
				 * likes 32k as default.  */
	unsigned long min_bps;	/* When non-zero, sets a minimum allow
				 * transmission rate.  Dropping below
				 * that rate will cancel the
				 * transfer. */
	long min_bps_time;	/* Length of time transmission is
				 * allowed to be below 'min_bps' */
	char lzmanag;		/* Local file management
				   request. ZF1_ZMAPND, ZF1_ZMCHNG,
				   ZF1_ZMCRC, ZF1_ZMNEWL, ZF1_ZMNEW,
				   ZF1_ZMPROT, ZF1_ZMCRC, or 0 */
	time_t stop_time;	/* Zero or seconds in the epoch.  When
				 * non-zero, indicates a shutdown
				 * time. */
	int under_rsh;		/* A flag.  Set to true if we're
				 * running under a restricted
				 * environment. When true, files save
				 * as 'rw' not 'rwx' */
};

typedef struct rz_ rz_t;

rz_t *rz_init(int under_rsh, int restricted, char lzmanag, int rxascii,
	      int rxbinary, long buffersize,
	      int allow_remote_commands, int nflag, int junk_path,
	      unsigned long min_bps, long min_bps_time,
	      time_t stop_time, int try_resume,
	      int makelcpathname, int rxclob,
	      int o_sync, int tcp_flag, int topipe
);

static int rzfiles (rz_t *rz, zm_t *zm, struct zm_fileinfo *);
static int tryz (rz_t *rz, zm_t *zm);
static void checkpath (rz_t *rz, const char *name);
static void chkinvok(const char *s, int *ptopipe);
static void report (int sct);
static void uncaps (char *s);
static int IsAnyLower (const char *s);
static int putsec (rz_t *rz, struct zm_fileinfo *zi, char *buf, size_t n);
static int procheader (rz_t *rz, zm_t *zm, char *name, struct zm_fileinfo *);
static int wcgetsec (rz_t *rz, size_t *Blklen, char *rxbuf, unsigned int maxtime);
static int wcrx (rz_t *rz, struct zm_fileinfo *);
static int wcrxpn (rz_t *rz, struct zm_fileinfo *, char *rpn);
static int wcreceive (rz_t *rz, zm_t *zm);
static int rzfile (rz_t *rz, zm_t *zm, struct zm_fileinfo *);
static void usage (int exitcode, const char *what);
static void usage1 (int exitcode);
static void exec2 (const char *s);
static int closeit (rz_t *rz, struct zm_fileinfo *);
static void ackbibi (zm_t *zm);
static int sys2 (const char *s);
static void zmputs (const char *s);
static size_t getfree (void);

rz_t*
rz_init(int under_rsh, int restricted, char lzmanag, int rxascii,
	int rxbinary, long buffersize,
	int allow_remote_commands, int nflag, int junk_path,
	unsigned long min_bps, long min_bps_time,
	time_t stop_time, int try_resume,
	int makelcpathname, int rxclob, int o_sync, int tcp_flag, int topipe)
{
	rz_t *rz = (rz_t *)malloc(sizeof(rz_t));
	memset (rz, 0, sizeof(rz_t));
	rz->under_rsh = under_rsh;
	rz->restricted = restricted;
	rz->lzmanag = lzmanag;
	rz->zconv = 0;
	rz->rxascii = rxascii;
	rz->rxbinary = rxbinary;
	rz->buffersize = buffersize;
	rz->allow_remote_commands = allow_remote_commands;
	rz->nflag = nflag;
	rz->junk_path = junk_path;
	rz->min_bps = min_bps;
	rz->min_bps_time = min_bps_time;
	rz->stop_time = stop_time;
	rz->try_resume = try_resume;
	rz->makelcpathname = makelcpathname;
	rz->rxclob = rxclob;
	rz->o_sync = o_sync;
	rz->tcp_flag = tcp_flag;
	rz->pathname = NULL;
	memset(rz->tcp_buf, 0, 256);
	rz->in_tcpsync = 0;
	rz->fout = NULL;
	rz->topipe = topipe;
	rz->errors = 0;
	rz->tryzhdrtype=ZRINIT;
	rz->tcp_socket = -1;
	rz->rxclob = FALSE;
	rz->skip_if_not_found = FALSE;
	return rz;

}

/* called by signal interrupt or terminate to clean things up */
void
bibi(int n)
{
	//FIXME: figure out how to avoid global zmodem_requested
	// if (zmodem_requested)
	// 	zmputs(Attn);
	canit(STDOUT_FILENO);
	io_mode(0,0);
	log_fatal(_("caught signal %s; exiting"), n);
	exit(128+n);
}

static struct option const long_options[] =
{
	{"append", no_argument, NULL, '+'},
	{"ascii", no_argument, NULL, 'a'},
	{"binary", no_argument, NULL, 'b'},
	{"bufsize", required_argument, NULL, 'B'},
	{"allow-commands", no_argument, NULL, 'C'},
	{"allow-remote-commands", no_argument, NULL, 'C'},
	{"escape", no_argument, NULL, 'e'},
	{"rename", no_argument, NULL, 'E'},
	{"help", no_argument, NULL, 'h'},
	{"crc-check", no_argument, NULL, 'H'},
	{"junk-path", no_argument, NULL, 'j'},
	{"errors", required_argument, NULL, 3},
	{"disable-timeouts", no_argument, NULL, 'O'},
	{"disable-timeout", no_argument, NULL, 'O'}, /* i can't get it right */
	{"min-bps", required_argument, NULL, 'm'},
	{"min-bps-time", required_argument, NULL, 'M'},
	{"newer", no_argument, NULL, 'n'},
	{"newer-or-longer", no_argument, NULL, 'N'},
	{"protect", no_argument, NULL, 'p'},
	{"resume", no_argument, NULL, 'r'},
	{"restricted", no_argument, NULL, 'R'},
	{"quiet", no_argument, NULL, 'q'},
	{"stop-at", required_argument, NULL, 's'},
	{"timeout", required_argument, NULL, 't'},
	{"keep-uppercase", no_argument, NULL, 'u'},
	{"unrestrict", no_argument, NULL, 'U'},
	{"verbose", no_argument, NULL, 'v'},
	{"windowsize", required_argument, NULL, 'w'},
	{"zmodem", no_argument, NULL, 'Z'},
	{"overwrite", no_argument, NULL, 'y'},
	{"null", no_argument, NULL, 'D'},
	{"delay-startup", required_argument, NULL, 4},
	{"o-sync", no_argument, NULL, 5},
	{"o_sync", no_argument, NULL, 5},
	{"tcp-server", no_argument, NULL, 6},
	{"tcp-client", required_argument, NULL, 7},
	{NULL,0,NULL,0}
};

static void
show_version(void)
{
	display ("%s (%s) %s", program_name, PACKAGE, VERSION);
}

int
main(int argc, char *argv[])
{
	register char *cp;
	int exitcode=0;
	int c;
	unsigned int startup_delay=0;
	int Rxtimeout = 100;	/* Receive timeout in deciseconds. */
	int under_rsh = FALSE;
	int Restricted = 1;
	char Lzmanag = '\0';
	int Rxascii = FALSE;
	int Rxbinary=FALSE;
	long buffersize=32768;
	int allow_remote_commands = FALSE;
	int Nflag = 0;
	int Zctlesc = 0;
	int junk_path = FALSE;
	unsigned long min_bps=0;
	long min_bps_time=120;
	int Quiet=0;
	int Verbose=LOG_ERROR;
	time_t stop_time = 0;
	int try_resume=FALSE;
	int Zrwindow=1400;
	int MakeLCPathname=TRUE;
	int Rxclob=FALSE;
	int o_sync = 0;
	int tcp_flag=0;
	char *tcp_server_address = NULL;
	unsigned Baudrate = 2400;
	program_name = strdup(argv[0]);
	setbuf(stderr, NULL);
	if ((cp=getenv("SHELL")) && (strstr(cp, "rsh") || strstr(cp, "rksh")
		|| strstr(cp,"rbash") || strstr(cp, "rshell")))
		under_rsh=TRUE;
	if ((cp=getenv("ZMODEM_RESTRICTED"))!=NULL)
		Restricted=2;

	/* make temporary and unfinished files */
	umask(0077);

	from_cu();
	int Topipe;
	chkinvok(argv[0], &Topipe);	/* if called as [-]rzCOMMAND set flag */

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

    parse_long_options (argc, argv, show_version, usage1);

	while ((c = getopt_long (argc, argv,
		"a+bB:cCDeEhm:M:OprRqs:St:uUvw:XZy",
		long_options, (int *) 0)) != EOF)
	{
		unsigned long int tmp;
		char *tmpptr;
		enum strtol_error s_err;

		switch (c)
		{
		case 0:
			break;
		case '+': Lzmanag = ZF1_ZMAPND; break;
		case 'a': Rxascii=TRUE;  break;
		case 'b': Rxbinary=TRUE; break;
		case 'B':
			if (strcmp(optarg,"auto")==0)
				buffersize=-1;
			else
				buffersize=strtol(optarg,NULL,10);
			break;
		case 'C': allow_remote_commands=TRUE; break;
		case 'D': Nflag = TRUE; break;
		case 'E': Lzmanag = ZF1_ZMCHNG; break;
		case 'e': Zctlesc = 1; break;
		case 'h': usage(0,NULL); break;
		case 'H': Lzmanag= ZF1_ZMCRC; break;
		case 'j': junk_path=TRUE; break;
		case 'm':
			s_err = xstrtoul (optarg, &tmpptr, 0, &tmp, "km");
			min_bps = tmp;
			if (s_err != LONGINT_OK)
				STRTOL_FATAL_ERROR (optarg, _("min_bps"), s_err);
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
		case 'O': no_timeout=TRUE; break;
		case 'p': Lzmanag = ZF1_ZMPROT;  break;
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
					usage(2, _("unparsable stop time\n"));
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


		case 'r':
			if (try_resume)
				Lzmanag= ZF1_ZMCRC;
			else
				try_resume=TRUE;
			break;
		case 'R': Restricted++;  break;
		case 't':
			s_err = xstrtoul (optarg, NULL, 0, &tmp, NULL);
			Rxtimeout = tmp;
			if (s_err != LONGINT_OK)
				STRTOL_FATAL_ERROR (optarg, _("timeout"), s_err);
			if (Rxtimeout<10 || Rxtimeout>1000)
				usage(2,_("timeout out of range 10..1000"));
			break;
		case 'w':
			s_err = xstrtoul (optarg, NULL, 0, &tmp, NULL);
			Zrwindow = tmp;
			if (s_err != LONGINT_OK)
				STRTOL_FATAL_ERROR (optarg, _("window size"), s_err);
			break;
		case 'u':
			MakeLCPathname=FALSE; break;
		case 'U':
			if (!under_rsh)
				Restricted=0;
			else  {
				log_fatal(_("security violation: can't do that under restricted shell"));
				exit(1);
			}
			break;
		case 'v':
			Verbose=LOG_INFO; break;
		case 'y':
			Rxclob=TRUE; break;
		case 2:
		case 3:
			s_err = xstrtoul (optarg, NULL, 0, &tmp, "km");
			bytes_per_error = tmp;
			if (s_err != LONGINT_OK)
				STRTOL_FATAL_ERROR (optarg, _("bytes_per_error"), s_err);
			if (bytes_per_error<100)
				usage(2,_("bytes-per-error should be >100"));
			break;
        case 4:
			s_err = xstrtoul (optarg, NULL, 0, &tmp, NULL);
			startup_delay = tmp;
			if (s_err != LONGINT_OK)
				STRTOL_FATAL_ERROR (optarg, _("startup delay"), s_err);
			break;
		case 5:
			o_sync=1;
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
		default:
			usage(2,NULL);
		}

	}

	log_set_level(Verbose);

	if (getuid()!=geteuid()) {
		log_fatal(_("this program was never intended to be used setuid"));
		exit(1);
	}
	/* initialize zsendline tab */
	zm_t *zm = zm_init(Rxtimeout,
			   0,	/* Znulls */
			   0, 	/* eflag */
			   Baudrate,
			   0,  /* turbo_escape */
			   Zctlesc,
			   Zrwindow);

	siginterrupt(SIGALRM,1);
	if (startup_delay)
		sleep(startup_delay);

	if (Restricted && allow_remote_commands) {
		allow_remote_commands=FALSE;
	}
	if (Fromcu && !Quiet) {
		if (Verbose == LOG_ERROR)
			Verbose = LOG_INFO;
		log_set_level(Verbose);
	}

	log_debug("%s %s", program_name, VERSION);

	rz_t *rz = rz_init(under_rsh,
			   Restricted,
			   Lzmanag,
			   Rxascii,
			   Rxbinary,
			   buffersize,
			   allow_remote_commands,
			   Nflag,
			   junk_path,
			   min_bps,
			   min_bps_time,
			   stop_time,
			   try_resume,
			   MakeLCPathname,
			   Rxclob,
			   o_sync,
			   tcp_flag,
			   Topipe

		);

	if (rz->tcp_flag==2) {
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

		rz->tcp_socket=tcp_accept(d);
		dup2(rz->tcp_socket,0);
		dup2(rz->tcp_socket,1);
	}
	if (rz->tcp_flag==3) {
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
		rz->tcp_socket=tcp_connect(buf);
		dup2(rz->tcp_socket,0);
		dup2(rz->tcp_socket,1);
	}

	zm->baudrate = io_mode(0,1);
	readline_setup(0, MAX_BLOCK, MAX_BLOCK*2);
	if (signal(SIGINT, bibi) == SIG_IGN)
		signal(SIGINT, SIG_IGN);
	else
		signal(SIGINT, bibi);
	signal(SIGTERM, bibi);
	signal(SIGPIPE, bibi);
	if (wcreceive(rz, zm)==ERROR) {
		exitcode=0200;
		canit(STDOUT_FILENO);
	}
	io_mode(0,0);
	if (exitcode && !zm->zmodem_requested)	/* bellow again with all thy might. */
		canit(STDOUT_FILENO);
	if (exitcode)
		log_info(_("Transfer incomplete"));
	else
		log_info(_("Transfer complete"));
	exit(exitcode);
}

static void
usage1(int exitcode)
{
	usage(exitcode,NULL);
}

static void
usage(int exitcode, const char *what)
{
	if (exitcode)
	{
		if (what)
			log_info("%s: %s",program_name,what);
		log_info( _("Try `%s --help' for more information."), program_name);
		exit(exitcode);
	}

	display(_("%s version %s"), program_name, VERSION);

	display(_("Usage: %s [options]"), program_name);
	display(_("Receive files with ZMODEM protocol"));
	display(_("    (Z) = option applies to ZMODEM only"));
	display(_("  -+, --append                append to existing files"));
	display(_("  -a, --ascii                 ASCII transfer (change CR/LF to LF)"));
	display(_("  -b, --binary                binary transfer"));
	display(_("  -B, --bufsize N             buffer N bytes (N==auto: buffer whole file)"));
	display(_("  -C, --allow-remote-commands allow execution of remote commands (Z)"));
	display(_("  -D, --null                  write all received data to /dev/null"));
	display(_("      --delay-startup N       sleep N seconds before doing anything"));
	display(_("  -e, --escape                Escape control characters (Z)"));
	display(_("  -E, --rename                rename any files already existing"));
	display(_("      --errors N              generate CRC error every N bytes (debugging)"));
	display(_("  -h, --help                  Help, print this usage message"));
	display(_("  -m, --min-bps N             stop transmission if BPS below N"));
	display(_("  -M, --min-bps-time N          for at least N seconds (default: 120)"));
	display(_("  -O, --disable-timeouts      disable timeout code, wait forever for data"));
	display(_("      --o-sync                open output file(s) in synchronous write mode"));
	display(_("  -p, --protect               protect existing files"));
	display(_("  -q, --quiet                 quiet, no progress reports"));
	display(_("  -r, --resume                try to resume interrupted file transfer (Z)"));
	display(_("  -R, --restricted            restricted, more secure mode"));
	display(_("  -s, --stop-at {HH:MM|+N}    stop transmission at HH:MM or in N seconds"));
	display(_("  -t, --timeout N             set timeout to N tenths of a second"));
	display(_("  -u, --keep-uppercase        keep upper case filenames"));
	display(_("  -U, --unrestrict            disable restricted mode (if allowed to)"));
	display(_("  -v, --verbose               be verbose, provide debugging information"));
	display(_("  -w, --windowsize N          Window is N bytes (Z)"));
	display(_("  -y, --overwrite             Yes, clobber existing file if any"));
	display("");
	display(_("short options use the same arguments as the long ones"));
	exit(exitcode);
}

/*
 * Let's receive something already.
 */

static int
wcreceive(rz_t *rz, zm_t *zm)
{
	int c;
	struct zm_fileinfo zi;
	zi.fname=NULL;
	zi.modtime=0;
	zi.mode=0;
	zi.bytes_total=0;
	zi.bytes_sent=0;
	zi.bytes_received=0;
	zi.bytes_skipped=0;
	zi.eof_seen=0;

	log_info(_("%s waiting to receive."), program_name);
	c = 0;
	c = tryz(rz, zm);
	if (c != 0) {
		if (c == ZCOMPL)
			return OK;
		if (c == ERROR)
			goto fubar;
		c = rzfiles(rz, zm, &zi);

		if (c)
			goto fubar;
	} else {
		for (;;) {
			timing(1,NULL);
			if (wcrxpn(rz, &zi,rz->secbuf)== ERROR)
				goto fubar;
			if (rz->secbuf[0]==0)
				return OK;
			if (procheader(rz, zm, rz->secbuf, &zi) == ERROR)
				goto fubar;
			if (wcrx(rz, &zi)==ERROR)
				goto fubar;

			double d;
			long bps;
			d=timing(0,NULL);
			if (d==0)
				d=0.5; /* can happen if timing uses time() */
			bps=(zi.bytes_received-zi.bytes_skipped)/d;

			log_info(
				_("\rBytes received: %7ld/%7ld   BPS:%-6ld"),
				(long) zi.bytes_received, (long) zi.bytes_total, bps);
		}
	}
	return OK;
fubar:
	canit(STDOUT_FILENO);
	if (rz->topipe && rz->fout) {
		pclose(rz->fout);  return ERROR;
	}
	if (rz->fout)
		fclose(rz->fout);

	if (rz->restricted && rz->pathname) {
		unlink(rz->pathname);
		log_info(_("%s: %s removed."), program_name, rz->pathname);
	}
	return ERROR;
}


/*
 * Fetch a pathname from the other end as a C ctyle ASCIZ string.
 * Length is indeterminate as long as less than Blklen
 */
static int
wcrxpn(rz_t *rz, struct zm_fileinfo *zi, char *rpn)
{
	register int c;
	size_t Blklen=0;		/* record length of received packets */

	READLINE_PF(1);

et_tu:
	rz->firstsec=TRUE;
	zi->eof_seen=FALSE;
	putchar(WANTCRC);
	fflush(stdout);
	purgeline(0); /* Do read next time ... */
	while ((c = wcgetsec(rz, &Blklen, rpn, 100)) != 0) {
		if (c == WCEOT) {
			log_error( _("Pathname fetch returned EOT"));
			putchar(ACK);
			fflush(stdout);
			purgeline(0);	/* Do read next time ... */
			READLINE_PF(1);
			goto et_tu;
		}
		return ERROR;
	}
	putchar(ACK);
	fflush(stdout);
	return OK;
}

/*
 * Adapted from CMODEM13.C, written by
 * Jack M. Wierda and Roderick W. Hart
 */
static int
wcrx(rz_t *rz, struct zm_fileinfo *zi)
{
	register int sectnum, sectcurr;
	register char sendchar;
	size_t Blklen;

	rz->firstsec=TRUE;sectnum=0;
	zi->eof_seen=FALSE;
	sendchar=WANTCRC;

	for (;;) {
		putchar(sendchar);	/* send it now, we're ready! */
		fflush(stdout);
		purgeline(0);	/* Do read next time ... */
		sectcurr=wcgetsec(rz, &Blklen, rz->secbuf,
			(unsigned int) ((sectnum&0177) ? 50 : 130));
		report(sectcurr);
		if (sectcurr==((sectnum+1) &0377)) {
			sectnum++;
			if (zi->bytes_total && R_BYTESLEFT(zi) < Blklen)
				Blklen=R_BYTESLEFT(zi);
			zi->bytes_received+=Blklen;
			if (putsec(rz, zi, rz->secbuf, Blklen)==ERROR)
				return ERROR;
			sendchar=ACK;
		}
		else if (sectcurr==(sectnum&0377)) {
			log_error( _("Received dup Sector"));
			sendchar=ACK;
		}
		else if (sectcurr==WCEOT) {
			if (closeit(rz, zi))
				return ERROR;
			putchar(ACK);
			fflush(stdout);
			purgeline(0);	/* Do read next time ... */
			return OK;
		}
		else if (sectcurr==ERROR)
			return ERROR;
		else {
			log_error( _("Sync Error"));
			return ERROR;
		}
	}
}

/*
 * Wcgetsec fetches a Ward Christensen type sector.
 * Returns sector number encountered or ERROR if valid sector not received,
 * or CAN CAN received
 * or WCEOT if eot sector
 * time is timeout for first char, set to 4 seconds thereafter
 ***************** NO ACK IS SENT IF SECTOR IS RECEIVED OK **************
 *    (Caller must do that when he is good and ready to get next sector)
 */
static int
wcgetsec(rz_t *rz, size_t *Blklen, char *rxbuf, unsigned int maxtime)
{
	register int checksum, wcj, firstch;
	register unsigned short oldcrc;
	register char *p;
	int sectcurr;

	rz->lastrx = 0;
	for (rz->errors = 0; rz->errors < RETRYMAX; rz->errors++) {

		if ((firstch=READLINE_PF(maxtime))==STX) {
			*Blklen=1024; goto get2;
		}
		if (firstch==SOH) {
			*Blklen=128;
get2:
			sectcurr=READLINE_PF(1);
			if ((sectcurr+(oldcrc=READLINE_PF(1)))==0377) {
				oldcrc=checksum=0;
				for (p=rxbuf,wcj=*Blklen; --wcj>=0; ) {
					if ((firstch=READLINE_PF(1)) < 0)
						goto bilge;
					oldcrc=updcrc(firstch, oldcrc);
					checksum += (*p++ = firstch);
				}
				if ((firstch=READLINE_PF(1)) < 0)
					goto bilge;
				oldcrc=updcrc(firstch, oldcrc);
				if ((firstch=READLINE_PF(1)) < 0)
					goto bilge;
				oldcrc=updcrc(firstch, oldcrc);
				if (oldcrc & 0xFFFF)
					log_error( _("CRC"));
				else {
					rz->firstsec=FALSE;
					return sectcurr;
				}
			}
			else
				log_error(_("Sector number garbled"));
		}
		/* make sure eot really is eot and not just mixmash */
		else if (firstch==EOT && READLINE_PF(1)==TIMEOUT)
			return WCEOT;
		else if (firstch==CAN) {
			if (rz->lastrx==CAN) {
				log_error( _("Sender Cancelled"));
				return ERROR;
			} else {
				rz->lastrx=CAN;
				continue;
			}
		}
		else if (firstch==TIMEOUT) {
			if (rz->firstsec)
				goto humbug;
bilge:
			log_error( _("TIMEOUT"));
		}
		else
			log_error( _("Got 0%o sector header"), firstch);

humbug:
		rz->lastrx=0;
		{
			int cnt=1000;
			while(cnt-- && READLINE_PF(1)!=TIMEOUT)
				;
		}
		if (rz->firstsec) {
			putchar(WANTCRC);
			fflush(stdout);
			purgeline(0);	/* Do read next time ... */
		} else {
			maxtime=40;
			putchar(NAK);
			fflush(stdout);
			purgeline(0);	/* Do read next time ... */
		}
	}
	/* try to stop the bubble machine. */
	canit(STDOUT_FILENO);
	return ERROR;
}

#define ZCRC_DIFFERS (ERROR+1)
#define ZCRC_EQUAL (ERROR+2)
/*
 * do ZCRC-Check for open file f.
 * check at most check_bytes bytes (crash recovery). if 0 -> whole file.
 * remote file size is remote_bytes.
 */
static int
do_crc_check(zm_t *zm, FILE *f, size_t remote_bytes, size_t check_bytes)
{
	struct stat st;
	unsigned long crc;
	unsigned long rcrc;
	size_t n;
	int c;
	int t1=0,t2=0;
	if (-1==fstat(fileno(f),&st)) {
		return ERROR;
	}
	if (check_bytes==0 && ((size_t) st.st_size)!=remote_bytes)
		return ZCRC_DIFFERS; /* shortcut */

	crc=0xFFFFFFFFL;
	n=check_bytes;
	if (n==0)
		n=st.st_size;
	while (n-- && ((c = getc(f)) != EOF))
		crc = UPDC32(c, crc);
	crc = ~crc;
	clearerr(f);  /* Clear EOF */
	fseek(f, 0L, 0);

	while (t1<3) {
		zm_store_header(check_bytes);
		zm_send_hex_header(zm, ZCRC, Txhdr);
		while(t2<3) {
			size_t tmp;
			c = zm_get_header(zm, Rxhdr, &tmp);
			rcrc=(unsigned long) tmp;
			switch (c) {
			default: /* ignore */
				break;
			case ZFIN:
				return ERROR;
			case ZRINIT:
				return ERROR;
			case ZCAN:
				log_info(_("got ZCAN"));
				return ERROR;
				break;
			case ZCRC:
				if (crc!=rcrc)
					return ZCRC_DIFFERS;
				return ZCRC_EQUAL;
				break;
			}
		}
	}
	return ERROR;
}

/*
 * Process incoming file information header
 */
static int
procheader(rz_t *rz, zm_t *zm, char *name, struct zm_fileinfo *zi)
{
	const char *openmode;
	char *p;
	static char *name_static=NULL;
	char *nameend;

	if (name_static)
		free(name_static);
	if (rz->junk_path) {
		p=strrchr(name,'/');
		if (p) {
			p++;
			if (!*p) {
				/* alert - file name ended in with a / */
				log_info(_("file name ends with a /, skipped: %s"),name);
				return ERROR;
			}
			name=p;
		}
	}
	name_static=malloc(strlen(name)+1);
	if (!name_static) {
		log_fatal(_("out of memory"));
		exit(1);
	}
	strcpy(name_static,name);
	zi->fname=name_static;

	log_debug(_("zmanag=%d, Lzmanag=%d"), rz->zmanag, rz->lzmanag);
	log_debug(_("zconv=%d"),rz->zconv);

	/* set default parameters and overrides */
	openmode = "w";
	rz->thisbinary = (!rz->rxascii) || rz->rxbinary;
	if (rz->lzmanag)
		rz->zmanag = rz->lzmanag;

	/*
	 *  Process ZMODEM remote file management requests
	 */
	if (!rz->rxbinary && rz->zconv == ZCNL)	/* Remote ASCII override */
		rz->thisbinary = 0;
	if (rz->zconv == ZCBIN)	/* Remote Binary override */
		rz->thisbinary = TRUE;
	if (rz->thisbinary && rz->zconv == ZCBIN && rz->try_resume)
		rz->zconv=ZCRESUM;
	if (rz->zmanag == ZF1_ZMAPND && rz->zconv!=ZCRESUM)
		openmode = "a";
	if (rz->skip_if_not_found)
		openmode="r+";

	rz->in_tcpsync=0;
	if (0==strcmp(name,"$tcp$.t"))
		rz->in_tcpsync=1;

	zi->bytes_total = DEFBYTL;
	zi->mode = 0;
	zi->eof_seen = 0;
	zi->modtime = 0;

	nameend = name + 1 + strlen(name);
	if (*nameend) {	/* file coming from Unix or DOS system */
		long modtime;
		long bytes_total;
		int mode;
		sscanf(nameend, "%ld%lo%o", &bytes_total, &modtime, &mode);
		zi->modtime=modtime;
		zi->bytes_total=bytes_total;
		zi->mode=mode;
		if (zi->mode & UNIXFILE)
			++rz->thisbinary;
	}

	/* Check for existing file */
	if (rz->zconv != ZCRESUM && !rz->rxclob && (rz->zmanag&ZF1_ZMMASK) != ZF1_ZMCLOB
		&& (rz->zmanag&ZF1_ZMMASK) != ZF1_ZMAPND
	    && !rz->in_tcpsync
		&& (rz->fout=fopen(name, "r"))) {
		struct stat sta;
		char *tmpname;
		char *ptr;
		int i;
		if (rz->zmanag == ZF1_ZMNEW || rz->zmanag==ZF1_ZMNEWL) {
			if (-1==fstat(fileno(rz->fout),&sta)) {
				log_info(_("file exists, skipped: %s"),name);
				return ERROR;
			}
			if (rz->zmanag == ZF1_ZMNEW) {
				if (sta.st_mtime > zi->modtime) {
					return ERROR; /* skips file */
				}
			} else {
				/* newer-or-longer */
				if (((size_t) sta.st_size) >= zi->bytes_total
					&& sta.st_mtime > zi->modtime) {
					return ERROR; /* skips file */
				}
			}
			fclose(rz->fout);
		} else if (rz->zmanag==ZF1_ZMCRC) {
			int r=do_crc_check(zm, rz->fout,zi->bytes_total,0);
			if (r==ERROR) {
				fclose(rz->fout);
				return ERROR;
			}
			if (r!=ZCRC_DIFFERS) {
				return ERROR; /* skips */
			}
			fclose(rz->fout);
		} else {
			size_t namelen;
			fclose(rz->fout);
			if ((rz->zmanag & ZF1_ZMMASK)!=ZF1_ZMCHNG) {
				log_info(_("file exists, skipped: %s"),name);
				return ERROR;
			}
			/* try to rename */
			namelen=strlen(name);
			tmpname=(char *) malloc(namelen+5);
			memcpy(tmpname,name,namelen);
			ptr=tmpname+namelen;
			*ptr++='.';
			i=0;
			do {
				sprintf(ptr,"%d",i++);
			} while (i<1000 && stat(tmpname,&sta)==0);
			if (i==1000) {
				free (tmpname);
				return ERROR;
			}
			free(name_static);
			name_static=malloc(strlen(tmpname)+1);
			if (!name_static) {
				log_fatal(_("out of memory"));
				exit(1);
			}
			strcpy(name_static,tmpname);
			free(tmpname);
			zi->fname=name_static;
		}
	}

	if (!*nameend) {		/* File coming from CP/M system */
		for (p=name_static; *p; ++p)		/* change / to _ */
			if ( *p == '/')
				*p = '_';

		if ( *--p == '.')		/* zap trailing period */
			*p = 0;
	}

	if (rz->in_tcpsync) {
		rz->fout=tmpfile();
		if (!rz->fout) {
			log_fatal(_("cannot tmpfile() for tcp protocol synchronization: %s"), strerror(errno));
			exit(1);
		}
		zi->bytes_received=0;
		return OK;
	}


	if (!zm->zmodem_requested && rz->makelcpathname && !IsAnyLower(name_static)
	  && !(zi->mode&UNIXFILE))
		uncaps(name_static);
	if (rz->topipe > 0) {
		if (rz->pathname)
			free(rz->pathname);
		rz->pathname=malloc((PATH_MAX)*2);
		if (!rz->pathname) {
			log_fatal(_("out of memory"));
			exit(1);
		}
		sprintf(rz->pathname, "%s %s", program_name+2, name_static);
		log_info("%s: %s %s",
			 _("Topipe"),
			 rz->pathname, rz->thisbinary?"BIN":"ASCII");
		if ((rz->fout=popen(rz->pathname, "w")) == NULL)
			return ERROR;
	} else {
		if (rz->pathname)
			free(rz->pathname);
		rz->pathname=malloc((PATH_MAX)*2);
		if (!rz->pathname) {
			log_fatal(_("out of memory"));
			exit(1);
		}
		strcpy(rz->pathname, name_static);
		/* overwrite the "waiting to receive" line */
		log_info(_("Receiving: %s"), name_static);
		checkpath(rz, name_static);
		if (rz->nflag)
		{
			free(name_static);
			name_static=(char *) strdup("/dev/null");
			if (!name_static)
			{
				log_fatal(_("out of memory"));
				exit(1);
			}
		}
		if (rz->thisbinary && rz->zconv==ZCRESUM) {
			struct stat st;
			rz->fout = fopen(name_static, "r+");
			if (rz->fout && 0==fstat(fileno(rz->fout),&st))
			{
				int can_resume=TRUE;
				if (rz->zmanag==ZF1_ZMCRC) {
					int r=do_crc_check(zm, rz->fout,zi->bytes_total,st.st_size);
					if (r==ERROR) {
						fclose(rz->fout);
						return ZFERR;
					}
					if (r==ZCRC_DIFFERS) {
						can_resume=FALSE;
					}
				}
				if ((unsigned long)st.st_size > zi->bytes_total) {
					can_resume=FALSE;
				}
				/* retransfer whole blocks */
				zi->bytes_skipped = st.st_size & ~(1023);
				if (can_resume) {
					if (fseek(rz->fout, (long) zi->bytes_skipped, SEEK_SET)) {
						fclose(rz->fout);
						return ZFERR;
					}
				}
				else
					zi->bytes_skipped=0; /* resume impossible, file has changed */
				goto buffer_it;
			}
			zi->bytes_skipped=0;
			if (rz->fout)
				fclose(rz->fout);
		}
		rz->fout = fopen(name_static, openmode);
		if ( !rz->fout)
		{
			log_error(_("cannot open %s: %s"), name_static, strerror(errno));
			return ERROR;
		}
	}
buffer_it:
	if (rz->topipe == 0) {
		static char *s=NULL;
		static size_t last_length=0;
		if (rz->o_sync) {
			int oldflags;
			oldflags = fcntl (fileno(rz->fout), F_GETFD, 0);
			if (oldflags>=0 && !(oldflags & O_SYNC)) {
				oldflags|=O_SYNC;
				fcntl (fileno(rz->fout), F_SETFD, oldflags); /* errors don't matter */
			}
		}

		if (rz->buffersize==-1 && s) {
			if (zi->bytes_total>last_length) {
				free(s);
				s=NULL;
				last_length=0;
			}
		}
		if (!s && rz->buffersize) {
			last_length=32768;
			if (rz->buffersize==-1) {
				if (zi->bytes_total>0)
					last_length=zi->bytes_total;
			} else
				last_length = rz->buffersize;
			/* buffer `4096' bytes pages */
			last_length=(last_length+4095)&0xfffff000;
			s=malloc(last_length);
			if (!s) {
				log_fatal(_("out of memory"));
				exit(1);
			}
		}
		if (s) {
			setvbuf(rz->fout,s,_IOFBF,last_length);
		}
	}
	zi->bytes_received=zi->bytes_skipped;

	return OK;
}


/*
 * Putsec writes the n characters of buf to receive file fout.
 *  If not in binary mode, carriage returns, and all characters
 *  starting with CPMEOF are discarded.
 */
static int
putsec(rz_t *rz, struct zm_fileinfo *zi, char *buf, size_t n)
{
	register char *p;

	if (n == 0)
		return OK;
	if (rz->thisbinary) {
		if (fwrite(buf,n,1,rz->fout)!=1)
			return ERROR;
	}
	else {
		if (zi->eof_seen)
			return OK;
		for (p=buf; n>0; ++p,n-- ) {
			if ( *p == '\r')
				continue;
			if (*p == CPMEOF) {
				zi->eof_seen=TRUE;
				return OK;
			}
			putc(*p ,rz->fout);
		}
	}
	return OK;
}

/* make string s lower case */
static void
uncaps(char *s)
{
	for ( ; *s; ++s)
		if (isupper((unsigned char)(*s)))
			*s = tolower(*s);
}
/*
 * IsAnyLower returns TRUE if string s has lower case letters.
 */
static int
IsAnyLower(const char *s)
{
	for ( ; *s; ++s)
		if (islower((unsigned char)(*s)))
			return TRUE;
	return FALSE;
}

static void
report(int sct)
{
	log_debug(_("Blocks received: %d"),sct);
}

/*
 * If called as [-][dir/../]vrzCOMMAND set Verbose to 1
 * If called as [-][dir/../]rzCOMMAND set the pipe flag
 */

static void
chkinvok(const char *s, int *ptopipe)
{
	const char *p;
	*ptopipe = 0;

	p = s;
	while (*p == '-')
		s = ++p;
	while (*p)
		if (*p++ == '/')
			s = p;
	//if (*s == 'v') {
	//	Verbose=LOG_INFO; ++s;
	//}
	program_name = s;
	if (*s == 'l')
		s++; /* lrz -> rz */
	if (s[2])
		*ptopipe = 1;
}

/*
 * Totalitarian Communist pathname processing
 */
static void
checkpath(rz_t *rz, const char *name)
{
	if (rz->restricted) {
		const char *p;
		p=strrchr(name,'/');
		if (p)
			p++;
		else
			p=name;
		/* don't overwrite any file in very restricted mode.
		 * don't overwrite hidden files in restricted mode */
		if ((rz->restricted==2 || *name=='.') && fopen(name, "r") != NULL) {
			canit(STDOUT_FILENO);
			log_info(_("%s: %s exists"),
				program_name, name);
			bibi(-1);
		}
		/* restrict pathnames to current tree or uucppublic */
		if ( strstr(name, "../")
#ifdef PUBDIR
		 || (name[0]== '/' && strncmp(name, PUBDIR,
		 	strlen(PUBDIR)))
#endif
		) {
			canit(STDOUT_FILENO);
			log_info(_("%s: Security Violation"),program_name);
			bibi(-1);
		}
		if (rz->restricted > 1) {
			if (name[0]=='.' || strstr(name,"/.")) {
				canit(STDOUT_FILENO);
				log_info(_("%s: Security Violation"),program_name);
				bibi(-1);
			}
		}
	}
}

/*
 * Initialize for Zmodem receive attempt, try to activate Zmodem sender
 *  Handles ZSINIT frame
 *  Return ZFILE if Zmodem filename received, -1 on error,
 *   ZCOMPL if transaction finished,  else 0
 */
static int
tryz(rz_t *rz, zm_t *zm)
{
	register int c, n;
	register int cmdzack1flg;
	int zrqinits_received=0;
	size_t bytes_in_block=0;

	for (n=zm->zmodem_requested?15:5;
		 (--n + zrqinits_received) >=0 && zrqinits_received<10; ) {
		/* Set buffer length (0) and capability flags */
		zm_store_header(0L);
#ifdef CANBREAK
		Txhdr[ZF0] = CANFC32|CANFDX|CANOVIO|CANBRK;
#else
		Txhdr[ZF0] = CANFC32|CANFDX|CANOVIO;
#endif
		if (zm->zctlesc)
			Txhdr[ZF0] |= TESCCTL; /* TESCCTL == ESCCTL */
		zm_send_hex_header(zm, rz->tryzhdrtype, Txhdr);

		if (rz->tcp_socket==-1 && strlen(rz->tcp_buf) > 0) {
			/* we need to switch to tcp mode */
			rz->tcp_socket=tcp_connect(rz->tcp_buf);
			memset(rz->tcp_buf, 0, sizeof(rz->tcp_buf));
			dup2(rz->tcp_socket,0);
			dup2(rz->tcp_socket,1);
		}
		if (rz->tryzhdrtype == ZSKIP)	/* Don't skip too far */
			rz->tryzhdrtype = ZRINIT;	/* CAF 8-21-87 */
again:
		switch (zm_get_header(zm, Rxhdr, NULL)) {
		case ZRQINIT:
			/* getting one ZRQINIT is totally ok. Normally a ZFILE follows
			 * (and might be in our buffer, so don't purge it). But if we
			 * get more ZRQINITs than the sender has started up before us
			 * and sent ZRQINITs while waiting.
			 */
			zrqinits_received++;
			continue;

		case ZEOF:
			continue;
		case TIMEOUT:
			continue;
		case ZFILE:
			rz->zconv = Rxhdr[ZF0];
			if (!rz->zconv)
				/* resume with sz -r is impossible (at least with unix sz)
				 * if this is not set */
				rz->zconv=ZCBIN;
			if (Rxhdr[ZF1] & ZF1_ZMSKNOLOC) {
				Rxhdr[ZF1] &= ~(ZF1_ZMSKNOLOC);
				rz->skip_if_not_found=TRUE;
			}
			rz->zmanag = Rxhdr[ZF1];
			rz->ztrans = Rxhdr[ZF2];
			rz->tryzhdrtype = ZRINIT;
			c = zm_receive_data(zm, rz->secbuf, MAX_BLOCK,&bytes_in_block);
			zm->baudrate = io_mode(0,3);
			if (c == GOTCRCW)
				return ZFILE;
			zm_send_hex_header(zm, ZNAK, Txhdr);
			goto again;
		case ZSINIT:
			/* this once was:
			 * Zctlesc = TESCCTL & Rxhdr[ZF0];
			 * trouble: if rz get --escape flag:
			 * - it sends TESCCTL to sz,
			 *   get a ZSINIT _without_ TESCCTL (yeah - sender didn't know),
			 *   overwrites Zctlesc flag ...
			 * - sender receives TESCCTL and uses "|=..."
			 * so: sz escapes, but rz doesn't unescape ... not good.
			 */
			zm->zctlesc |= (TESCCTL & Rxhdr[ZF0]);
			if (zm_receive_data(zm, rz->attn, ZATTNLEN, &bytes_in_block) == GOTCRCW) {
				zm_store_header(1L);
				zm_send_hex_header(zm, ZACK, Txhdr);
				goto again;
			}
			zm_send_hex_header(zm, ZNAK, Txhdr);
			goto again;
		case ZFREECNT:
			zm_store_header(getfree());
			zm_send_hex_header(zm, ZACK, Txhdr);
			goto again;
		case ZCOMMAND:
			cmdzack1flg = Rxhdr[ZF0];
			if (zm_receive_data(zm, rz->secbuf, MAX_BLOCK,&bytes_in_block) == GOTCRCW) {
				log_info("%s",  _("remote command execution requested"));
				log_info("%s: %s", program_name, rz->secbuf);
				if (!rz->allow_remote_commands)
				{
					log_info("%s: %s", program_name,
						 _("not executed"));
					zm_send_hex_header(zm, ZCOMPL, Txhdr);
					return ZCOMPL;
				}
				if (cmdzack1flg & ZCACK1)
					zm_store_header(0L);
				else
					zm_store_header((size_t)sys2(rz->secbuf));
				purgeline(0);	/* dump impatient questions */
				do {
					zm_send_hex_header(zm, ZCOMPL, Txhdr);
					if (zm_get_header(zm, Rxhdr, NULL) == ZFIN)
						break;
					rz->errors++;

				} while (rz->errors < 20);

				ackbibi(zm);
				if (cmdzack1flg & ZCACK1)
					exec2(rz->secbuf);
				return ZCOMPL;
			}
			zm_send_hex_header(zm, ZNAK, Txhdr);
			goto again;
		case ZCOMPL:
			goto again;
		default:
			continue;
		case ZFIN:
			ackbibi(zm);
			return ZCOMPL;
		case ZRINIT:
			log_info(_("got ZRINIT"));
			return ERROR;
		case ZCAN:
			log_info(_("got ZCAN"));
			return ERROR;
		}
	}
	return 0;
}


/*
 * Receive 1 or more files with ZMODEM protocol
 */
static int
rzfiles(rz_t *rz, zm_t *zm, struct zm_fileinfo *zi)
{
	register int c;

	for (;;) {
		timing(1,NULL);
		c = rzfile(rz, zm, zi);
		switch (c) {
		case ZEOF:
		{
			double d;
			long bps;
			d=timing(0,NULL);
			if (d==0)
				d=0.5; /* can happen if timing uses time() */
			bps=(zi->bytes_received-zi->bytes_skipped)/d;
			log_info(_("Bytes received: %7ld/%7ld   BPS:%-6ld"),
				(long) zi->bytes_received, (long) zi->bytes_total, bps);
		}
			/* FALL THROUGH */
		case ZSKIP:
			if (c==ZSKIP)
			{
				log_info(_("Skipped"));
			}
			switch (tryz(rz, zm)) {
			case ZCOMPL:
				return OK;
			default:
				return ERROR;
			case ZFILE:
				break;
			}
			continue;
		default:
			return c;
		case ERROR:
			return ERROR;
		}
	}
}

/* "OOSB" means Out Of Sync Block. I once thought that if sz sents
 * blocks a,b,c,d, of which a is ok, b fails, we might want to save
 * c and d. But, alas, i never saw c and d.
 */
typedef struct oosb_t {
	size_t pos;
	size_t len;
	char *data;
	struct oosb_t *next;
} oosb_t;
struct oosb_t *anker=NULL;

/*
 * Receive a file with ZMODEM protocol
 *  Assumes file name frame is in rz->secbuf
 */
static int
rzfile(rz_t *rz, zm_t *zm, struct zm_fileinfo *zi)
{
	register int c, n;
	long last_rxbytes=0;
	unsigned long last_bps=0;
	long not_printed=0;
	time_t low_bps=0;
	size_t bytes_in_block=0;

	zi->eof_seen=FALSE;

	n = 20;

	if (procheader(rz, zm, rz->secbuf,zi) == ERROR) {
		return (rz->tryzhdrtype = ZSKIP);
	}

	for (;;) {
		zm_store_header(zi->bytes_received);
		zm_send_hex_header(zm, ZRPOS, Txhdr);
		goto skip_oosb;
nxthdr:
		if (anker) {
			oosb_t *akt,*last,*next;
			for (akt=anker,last=NULL;akt;last= akt ? akt : last ,akt=next) {
				if (akt->pos==zi->bytes_received) {
					putsec(rz, zi, akt->data, akt->len);
					zi->bytes_received += akt->len;
					log_debug("using saved out-of-sync-paket %lx, len %ld",
						  akt->pos,akt->len);
					goto nxthdr;
				}
				next=akt->next;
				if (akt->pos<zi->bytes_received) {
					log_debug("removing unneeded saved out-of-sync-paket %lx, len %ld",
						  akt->pos,akt->len);
					if (last)
						last->next=akt->next;
					else
						anker=akt->next;
					free(akt->data);
					free(akt);
					akt=NULL;
				}
			}
		}
	skip_oosb:
		c = zm_get_header(zm, Rxhdr, NULL);
		switch (c) {
		default:
			log_debug("rzfile: zm_get_header returned %d", c);
			return ERROR;
		case ZNAK:
		case TIMEOUT:
			if ( --n < 0) {
				log_debug("rzfile: zm_get_header returned %d", c);
				return ERROR;
			}
		case ZFILE:
			zm_receive_data(zm, rz->secbuf, MAX_BLOCK,&bytes_in_block);
			continue;
		case ZEOF:
			if (zm_reclaim_header(Rxhdr) != (long) zi->bytes_received) {
				/*
				 * Ignore eof if it's at wrong place - force
				 *  a timeout because the eof might have gone
				 *  out before we sent our zrpos.
				 */
				rz->errors = 0;
				goto nxthdr;
			}
			if (closeit(rz, zi)) {
				rz->tryzhdrtype = ZFERR;
				log_debug("rzfile: closeit returned <> 0");
				return ERROR;
			}
			log_debug("rzfile: normal EOF");
			return c;
		case ERROR:	/* Too much garbage in header search error */
			if ( --n < 0) {
				log_debug("rzfile: zm_get_header returned %d", c);
				return ERROR;
			}
			zmputs(rz->attn);
			continue;
		case ZSKIP:
			closeit(rz, zi);
			log_debug("rzfile: Sender SKIPPED file");
			return c;
		case ZDATA:
			if (zm_reclaim_header(Rxhdr) != (long) zi->bytes_received) {
				oosb_t *neu;
				size_t pos=zm_reclaim_header(Rxhdr);
				if ( --n < 0) {
					log_debug("rzfile: out of sync");
					return ERROR;
				}
				switch (c = zm_receive_data(zm, rz->secbuf, MAX_BLOCK,&bytes_in_block))
				{
				case GOTCRCW:
				case GOTCRCG:
				case GOTCRCE:
				case GOTCRCQ:
					if (pos>zi->bytes_received) {
						neu=malloc(sizeof(oosb_t));
						if (neu)
							neu->data=malloc(bytes_in_block);
						if (neu && neu->data) {
							log_debug("saving out-of-sync-block %lx, len %lu",pos,
								  (unsigned long) bytes_in_block);
							memcpy(neu->data,rz->secbuf,bytes_in_block);
							neu->pos=pos;
							neu->len=bytes_in_block;
							neu->next=anker;
							anker=neu;
						}
						else if (neu)
							free(neu);
					}
				}
				zmputs(rz->attn);  continue;
			}
moredata:
			if ((rz->min_bps || rz->stop_time) && (not_printed > (rz->min_bps ? 3 : 7)
						       || zi->bytes_received > last_bps / 2 + last_rxbytes)) {
				int minleft =  0;
				int secleft =  0;
				time_t now;
				double d;
				d=timing(0,&now);
				if (d==0)
					d=0.5; /* timing() might use time() */
				last_bps=zi->bytes_received/d;
				if (last_bps > 0) {
					minleft =  (R_BYTESLEFT(zi))/last_bps/60;
					secleft =  ((R_BYTESLEFT(zi))/last_bps)%60;
				}
				if (rz->min_bps) {
					if (low_bps) {
						if (last_bps < rz->min_bps) {
							if (now-low_bps >= rz->min_bps_time) {
								/* too bad */
								log_debug(_("rzfile: bps rate %ld below min %ld"),
									  last_bps, rz->min_bps);
								return ERROR;
							}
						}
						else
							low_bps=0;
					} else if (last_bps< rz->min_bps) {
						low_bps=now;
					}
				}
				if (rz->stop_time && now >= rz->stop_time) {
					/* too bad */
					log_debug(_("rzfile: reached stop time"));
					return ERROR;
				}

				log_info(_("\rBytes received: %7ld/%7ld   BPS:%-6ld ETA %02d:%02d  "),
					 (long) zi->bytes_received, (long) zi->bytes_total,
					 last_bps, minleft, secleft);
				last_rxbytes=zi->bytes_received;
				not_printed=0;
			} else
				not_printed++;
			switch (c = zm_receive_data(zm, rz->secbuf, MAX_BLOCK,&bytes_in_block))
			{
			case ZCAN:
				log_debug("rzfile: zm_receive_data returned %d", c);
				return ERROR;
			case ERROR:	/* CRC error */
				if ( --n < 0) {
					log_debug("rzfile: zm_get_header returned %d", c);
					return ERROR;
				}
				zmputs(rz->attn);
				continue;
			case TIMEOUT:
				if ( --n < 0) {
					log_debug("rzfile: zm_get_header returned %d", c);
					return ERROR;
				}
				continue;
			case GOTCRCW:
				n = 20;
				putsec(rz, zi, rz->secbuf, bytes_in_block);
				zi->bytes_received += bytes_in_block;
				zm_store_header(zi->bytes_received);
				zm_send_hex_header(zm, ZACK | 0x80, Txhdr);
				goto nxthdr;
			case GOTCRCQ:
				n = 20;
				putsec(rz, zi, rz->secbuf, bytes_in_block);
				zi->bytes_received += bytes_in_block;
				zm_store_header(zi->bytes_received);
				zm_send_hex_header(zm, ZACK, Txhdr);
				goto moredata;
			case GOTCRCG:
				n = 20;
				putsec(rz, zi, rz->secbuf, bytes_in_block);
				zi->bytes_received += bytes_in_block;
				goto moredata;
			case GOTCRCE:
				n = 20;
				putsec(rz, zi, rz->secbuf, bytes_in_block);
				zi->bytes_received += bytes_in_block;
				goto nxthdr;
			}
		}
	}
}

/*
 * Send a string to the modem, processing for \336 (sleep 1 sec)
 *   and \335 (break signal)
 */
static void
zmputs(const char *s)
{
	const char *p;

	while (s && *s)
	{
		p=strpbrk(s,"\335\336");
		if (!p)
		{
			write(1,s,strlen(s));
			return;
		}
		if (p!=s)
		{
			write(1,s,(size_t) (p-s));
			s=p;
		}
		if (*p=='\336')
			sleep(1);
		else
			sendbrk(0);
		p++;
	}
}

/*
 * Close the receive dataset, return OK or ERROR
 */
static int
closeit(rz_t *rz, struct zm_fileinfo *zi)
{
	int ret;
	if (rz->topipe) {
		if (pclose(rz->fout)) {
			return ERROR;
		}
		return OK;
	}
	if (rz->in_tcpsync) {
		rewind(rz->fout);
		if (!fgets(rz->tcp_buf, sizeof(rz->tcp_buf), rz->fout)) {
			log_fatal(_("fgets for tcp protocol synchronization failed: %s"), strerror(errno));
			exit(1);
		}
		fclose(rz->fout);
		return OK;
	}
	ret=fclose(rz->fout);
	if (ret) {
		log_error(_("file close error: %s"), strerror(errno));
		/* this may be any sort of error, including random data corruption */

		unlink(rz->pathname);
		return ERROR;
	}
	if (zi->modtime) {
		struct utimbuf timep;
		timep.actime = time(NULL);
		timep.modtime = zi->modtime;
		utime(rz->pathname, &timep);
	}
	if (S_ISREG(zi->mode)) {
		/* we must not make this program executable if running
		 * under rsh, because the user might have uploaded an
		 * unrestricted shell.
		 */
		if (rz->under_rsh)
			chmod(rz->pathname, (00666 & zi->mode));
		else
			chmod(rz->pathname, (07777 & zi->mode));
	}
	return OK;
}

/*
 * Ack a ZFIN packet, let byegones be byegones
 */
static void
ackbibi(zm_t *zm)
{
	int n;

	log_debug("ackbibi:");
	zm_store_header(0L);
	for (n=3; --n>=0; ) {
		purgeline(0);
		zm_send_hex_header(zm, ZFIN, Txhdr);
		switch (READLINE_PF(100)) {
		case 'O':
			READLINE_PF(1);	/* Discard 2nd 'O' */
			log_debug("ackbibi complete");
			return;
		case RCDO:
			return;
		case TIMEOUT:
		default:
			break;
		}
	}
}

/*
 * Strip leading ! if present, do shell escape.
 */
static int
sys2(const char *s)
{
	if (*s == '!')
		++s;
	return system(s);
}

/*
 * Strip leading ! if present, do exec.
 */
static void
exec2(const char *s)
{
	if (*s == '!')
		++s;
	io_mode(0,0);
	execl("/bin/sh", "sh", "-c", s, NULL);
	log_fatal("execl: %s", strerror(errno));
	exit(1);
}

/*
 * Routine to calculate the free bytes on the current file system
 *  ~0 means many free bytes (unknown)
 */
static size_t
getfree(void)
{
	return((size_t) (~0L));	/* many free bytes ... */
}

/* End of lrz.c */
