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
#include <stdbool.h>

#include <sys/mman.h>
#include "timing.h"
#include "log.h"
#include "zmodem.h"
#include "crctab.h"
#include "zm.h"

#define MAX_BLOCK 8192

struct sz_ {
	// state
	char txbuf[MAX_BLOCK];
	FILE *input_f;
	size_t mm_size;
	void *mm_addr;
	size_t lastsync;		/* Last offset to which we got a ZRPOS */
	size_t bytcnt;
	char crcflg;
	int firstsec;
	unsigned txwindow;	/* Control the size of the transmitted window */
	unsigned txwspac;	/* Spacing between zcrcq requests */
	unsigned txwcnt;	/* Counter used to space ack requests */
	size_t lrxpos;		/* Receiver's last reported offset */
	int errors;
	int under_rsh;
	char lastrx;
	long totalleft;
	int canseek; /* 1: can; 0: only rewind, -1: neither */
	size_t blklen;		/* length of transmitted records */
	int totsecs;		/* total number of sectors this file */
	int filcnt;		/* count of number of files opened */
	int lfseen;
	unsigned tframlen;	/* Override for tx frame length */
	unsigned blkopt;		/* Override value for zmodem blklen */
	int rxflags;
	int rxflags2;
	int exitcode;
	time_t stop_time;
	char *tcp_server_address;
	int tcp_socket;
	int error_count;
	jmp_buf intrjmp;	/* For the interrupt on RX CAN */
	int zrqinits_sent;
	int play_with_sigint;

	// parameters
	char lzconv;	/* Local ZMODEM file conversion request */
	char lzmanag;	/* Local ZMODEM file management request */
	int lskipnocor;
	int tcp_flag;
	int no_unixmode;
	int filesleft;
	int restricted;
	int fullname;
	int errcnt;		/* number of files unreadable */
	int optiong;		/* Let it rip no wait for sector ACK's */
	unsigned rxbuflen;	/* Receiver's max buffer length */
	int wantfcs32;	/* want to send 32 bit FCS */
	size_t max_blklen;
	size_t start_blklen;
	long min_bps;
	long min_bps_time;
	int hyperterm;
	int io_mode_fd;

	void (*complete_cb)(const char *filename, int result, size_t size, time_t date);
	bool (*tick_cb)(const char *fname, long bytes_sent, long bytes_total, long last_bps, int min_left, int sec_left);


};

typedef struct sz_ sz_t;

static sz_t*
sz_init(char lzconv, char lzmanag, int lskipnocor, int tcp_flag, unsigned txwindow, unsigned txwspac,
	int under_rsh, int no_unixmode, int canseek, int restricted,
	int fullname, unsigned blkopt, int tframlen, int wantfcs32,
	size_t max_blklen, size_t start_blklen, time_t stop_time,
	long min_bps, long min_bps_time,
	char *tcp_server_address, int tcp_socket, int hyperterm,
	void (*complete_cb)(const char *filename, int result, size_t size, time_t date),
	bool (*tick_cb)(const char *fname, long bytes_sent, long bytes_total, long last_bps, int min_left, int sec_left)
	)
{
	sz_t *sz = malloc(sizeof(sz_t));
	memset(sz, 0, sizeof(sz_t));
	sz->lzconv = lzconv;
	sz->lzmanag = lzmanag;
	sz->lskipnocor = lskipnocor;
	sz->mm_addr = NULL;
	sz->tcp_flag = tcp_flag;
	sz->txwindow = txwindow;
	sz->txwspac = txwspac;
	sz->txwcnt = 0;
	sz->under_rsh = under_rsh;
	sz->no_unixmode = no_unixmode;
	sz->canseek = canseek;
	sz->filesleft = 0;
	sz->restricted = restricted;
	sz->fullname = fullname;
	sz->rxbuflen = 16384;
	sz->blkopt = blkopt;
	sz->tframlen = tframlen;
	sz->wantfcs32 = wantfcs32;
	sz->max_blklen = max_blklen;
	sz->start_blklen = start_blklen;
	sz->stop_time = stop_time;
	sz->min_bps = min_bps;
	sz->min_bps_time = min_bps_time;
	if (tcp_server_address)
		sz->tcp_server_address = strdup(tcp_server_address);
	else
		sz->tcp_server_address = NULL;
	sz->tcp_socket = tcp_socket;
	sz->hyperterm = hyperterm;
	sz->complete_cb = complete_cb;
	sz->tick_cb = tick_cb;
	return sz;
}

static int zsendfile (sz_t *sz, zreadline_t *zr, zm_t *zm, struct zm_fileinfo *zi, const char *buf, size_t blen);
static int getnak (sz_t *sz, zreadline_t *zr, zm_t *zm);
static int wctxpn (sz_t *sz, zreadline_t *zr, zm_t *zm, struct zm_fileinfo *);
static int wcs (sz_t *sz, zreadline_t *zr, zm_t *zm, const char *oname, const char *remotename);
static size_t zfilbuf (sz_t *sz, struct zm_fileinfo *zi);
static size_t filbuf (sz_t *sz, char *buf, size_t count);
static int getzrxinit (sz_t *sz, zreadline_t *zr, zm_t *zm);
static int calc_blklen (sz_t *sz, long total_sent);
static int sendzsinit (sz_t *sz, zreadline_t *zr, zm_t *zm);
static int wctx (sz_t *sz, zreadline_t *zr, zm_t *zm, struct zm_fileinfo *);
static int zsendfdata (sz_t *sz, zreadline_t *zr, zm_t *zm, struct zm_fileinfo *);
static int getinsync (sz_t *sz, zreadline_t *zr, zm_t *zm, struct zm_fileinfo *, int flag);
static void countem (sz_t *sz, int argc, char **argv);
static void saybibi (zreadline_t *zr, zm_t *zm);
static int wcsend (sz_t *sz, zreadline_t *zr, zm_t *zm, int argc, char *argp[]);
static int wcputsec (sz_t *sz, zreadline_t *zr, zm_t *zm, char *buf, int sectnum, size_t cseclen);

#define ZM_SEND_DATA(x,y,z)						\
	do { if (zm->crc32t) {zm_send_data32(zm,x,y,z); } else {zm_send_data(zm,x,y,z);}} while(0)
#define DATAADR (sz->mm_addr ? ((char *)sz->mm_addr)+zi->bytes_sent : sz->txbuf)


/*
 * Attention string to be executed by receiver to interrupt streaming data
 *  when an error is detected.  A pause (0336) may be needed before the
 *  ^C (03) or after it.
 */
static char Myattn[] = { 0 };

// static FILE *input_f;

// static size_t bytcnt;
// static char Lzconv;	/* Local ZMODEM file conversion request */
// static char Lzmanag;	/* Local ZMODEM file management request */
// static int Lskipnocor;
// static size_t Lastsync;		/* Last offset to which we got a ZRPOS */

static int no_timeout=FALSE;

#define OVERHEAD 18
#define OVER_ERR 20

#define MK_STRING(x) #x



/* called by signal interrupt or terminate to clean things up */
static void
bibi (int n)
{
	// canit(zr, STDOUT_FILENO);
	fflush (stdout);
	// FIXME, should be io_mode (sz->io_mode_fd, 0);
	io_mode(0, 0);
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
	// FIXME: how do I work around this?
	// longjmp(sz->intrjmp, -1);
}


const char *program_name = "sz";


size_t zmodem_send(int file_count,
		   const char **file_list,
		   bool (*tick)(long bytes_sent, long bytes_total, long last_bps, int min_left, int sec_left),
		   void (*complete)(const char *filename, int result, size_t size, time_t date),
		   uint64_t min_bps,
		   uint32_t flags)
{
	log_set_level(LOG_ERROR);
	zm_t *zm = zm_init(0, /* fd */
			   128, /* readnum */
			   256, /* bufsize */
			   1, /* no_timeout */
			   600,	/* rxtimeout */
			   0, 	/* znulls */
			   0,	/* eflag */
			   2400, /* baudrate */
			   1,	 /* zctlesc */
			   1400); /* zrwindow */
	sz_t *sz = sz_init(0,	  /* lzconv */
			   0,	  /* lzmanag */
			   0,	  /* lskipnocor */
			   0,	  /* tcp_flag */
			   0,	  /* txwindow */
			   0,	  /* txwspac */
			   0,	  /* under_rsh */
			   0,	  /* no_unixmode */
			   1,	  /* canseek */
			   0,	  /* restricted */
			   0,	  /* fullname */
			   0,	  /* blkopt */
			   0,	  /* tframlen */
			   1,	  /* wantfcs32 */
			   1024,  /* max_blklen */
			   1024,  /* start_blklen */
			   0,	  /* stop_time */
			   0,	  /* min_bps */
			   0,	  /* min_bps_time */
			   0x0,	  /* tcp_server_address */
			   -1,	  /* tcp_socket */
			   0,	  /* hyperterm */
			   complete,  /* file complete callback */
			   tick	      /* tick callback */
		);
	log_info("initial protocol is ZMODEM");
	if (sz->start_blklen==0) {
		sz->start_blklen=1024;
		if (sz->tframlen) {
			sz->start_blklen=sz->max_blklen=sz->tframlen;
		}
	}
	zm->baudrate = io_mode(sz->io_mode_fd,1);

	/* Spec 8.1: "The sending program may send the string "rz\r" to
	   invoke the receiving program from a possible command
	   mode." */
	display("rz\r");
	fflush(stdout);

	/* Spec 8.1: "The sending program may then display a message
	 * intended for human consumption."  That would happen here,
	 * if we did it.  */

	/* throw away any input already received. This doesn't harm
	 * as we invite the receiver to send it's data again, and
	 * might be useful if the receiver has already died or
	 * if there is dirt left if the line
	 */
	struct timeval t;
	unsigned char throwaway;
	fd_set f;

	zreadline_flushline(zm->zr);

	t.tv_sec = 0;
	t.tv_usec = 0;

	FD_ZERO(&f);
	FD_SET(sz->io_mode_fd,&f);

	while (select(1,&f,NULL,NULL,&t)) {
		if (0==read(sz->io_mode_fd,&throwaway,1)) /* EOF ... */
			break;
	}

	zreadline_flushline(zm->zr);
	zm_store_header(0L);

	/* Spec 8.1: "Then the sender may send a ZRQINIT. The ZRQINIT
	   header causes a previously started receive program to send
	   its ZRINIT header without delay." */
	zm_send_hex_header(zm, ZRQINIT, Txhdr);
	sz->zrqinits_sent++;
	if (sz->tcp_flag==1) {
		sz->totalleft+=256; /* tcp never needs more */
		sz->filesleft++;
	}
	fflush(stdout);

	/* This is the main loop.  */
	if (wcsend(sz, zm->zr, zm, file_count, file_list)==ERROR) {
		sz->exitcode=0200;
		zreadline_canit(zm->zr, STDOUT_FILENO);
	}
	fflush(stdout);
	io_mode(sz->io_mode_fd, 0);
	int dm = 0;
	if (sz->exitcode)
		dm=sz->exitcode;
	else if (sz->errcnt)
		dm=1;
	else
		dm=0;
	if (dm)
		log_info(_("Transfer incomplete"));
	else
		log_info(_("Transfer complete"));
	exit(dm);
	/*NOTREACHED*/


	return 0u;
}

static int
send_pseudo(sz_t *sz, zreadline_t *zr,  zm_t *zm, const char *name, const char *data)
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

	if (wcs (sz, zr, zm, tmp,name) == ERROR) {
		log_info (_ ("send_pseudo %s: failed"),name);
		ret=1;
	}
	unlink (tmp);
	free(tmp);
	return ret;
}

/* This routine tries to send multiple files.  The file count and
   filenames are in ARGC and ARGP. */
static int
wcsend (sz_t *sz, zreadline_t *zr, zm_t *zm, int argc, char *argp[])
{
	int n;

	sz->crcflg = FALSE;
	sz->firstsec = TRUE;
	sz->bytcnt = (size_t) -1;

	if (sz->tcp_flag==1) {
		char buf[256];
		int d;

		/* tell receiver to receive via tcp */
		d=tcp_server(buf);
		if (send_pseudo(sz, zr, zm, "/$tcp$.t",buf)) {
			log_fatal(_("tcp protocol init failed"));
			exit(1);
		}
		/* ok, now that this file is sent we can switch to tcp */

		sz->tcp_socket=tcp_accept(d);
		dup2(sz->tcp_socket,0);
		dup2(sz->tcp_socket,1);
	}

	/* Begin the main loop. */
	for (n = 0; n < argc; ++n) {
		sz->totsecs = 0;
		/* The files are transmitted one at a time, here. */
		if (wcs (sz, zr, zm, argp[n],NULL) == ERROR)
			return ERROR;
	}
	sz->totsecs = 0;
	if (sz->filcnt == 0) {			/* bitch if we couldn't open ANY files */
		zreadline_canit(zr, STDOUT_FILENO);
		log_info (_ ("Can't open any requested files."));
		return ERROR;
	}
	if (zm->zmodem_requested)
		/* The session to the receiver is terminated here. */
		saybibi (zr, zm);
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
		wctxpn (sz, zr, zm, &zi);
	}
	return OK;
}


/* This routine should send one file from a list of files.  The
 filename is ONAME. REMOTENAME can be NULL. */
static int
wcs(sz_t *sz, zreadline_t *zr, zm_t *zm, const char *oname, const char *remotename)
{
	struct stat f;
	char name[PATH_MAX+1];
	struct zm_fileinfo zi;
	int dont_mmap_this=0;

	/* First we do many checks to ensure that the filename is
	 * valid and that the user is permitted to send these
	 * files. */
	if (sz->restricted) {
		/* restrict pathnames to current tree or uucppublic */
		if ( strstr(oname, "../")
#ifdef PUBDIR
		 || (oname[0]== '/' && strncmp(oname, MK_STRING(PUBDIR),
		 	strlen(MK_STRING(PUBDIR))))
#endif
		) {
			zreadline_canit(zr, STDOUT_FILENO);
			log_fatal(_("security violation: not allowed to upload from %s"),oname);
			exit(1);
		}
	}

	/* [mlg] I guess it was a feature that a filename of '-'
	 * would mean that the file data could be piped in
	 * and the name of the file to be transmitted was from the
	 * ONAME env var or a temp name was generated. */
	if (0==strcmp(oname,"-")) {
		char *p=getenv("ONAME");
		if (p) {
			strcpy(name, p);
		} else {
			sprintf(name, "s%lu.lsz", (unsigned long) getpid());
		}
		sz->input_f=stdin;
		dont_mmap_this=1;
	} else if ((sz->input_f=fopen(oname, "r"))==NULL) {
		int e=errno;
		log_error(_("cannot open %s: %s"), oname, strerror(e));
		++sz->errcnt;
		return OK;	/* pass over it, there may be others */
	} else {
		strcpy(name, oname);
	}
	/* Check for directory or block special files */
	fstat(fileno(sz->input_f), &f);
	if (S_ISDIR(f.st_mode) || S_ISBLK(f.st_mode)) {
		log_error(_("is not a file: %s"), name);
		fclose(sz->input_f);
		return OK;
	}

	/* Here we finally start filling in information about the
         * file in a ZI structure.  We need this for the ZMODEM
	 * file header when we send it. */
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

	++sz->filcnt;
	/* Now that the file information is validated and is in a ZI
	 * structure, we try to transmit the file. */
	switch (wctxpn(sz, zr, zm, &zi)) {
	case ERROR:
		return ERROR;
	case ZSKIP:
		log_error(_("skipped: %s"), name);
		return OK;
	}
	if (!zm->zmodem_requested && wctx(sz, zr, zm, &zi)==ERROR)
	{
		return ERROR;
	}

	/* Here we make a log message the transmission of a single
	 * file. */
	long bps;
	double d=timing(0,NULL);
	if (d==0) /* can happen if timing() uses time() */
		d=0.5;
	bps=zi.bytes_sent/d;
	log_debug(_("Bytes Sent:%7ld   BPS:%-8ld"),
		  (long) zi.bytes_sent,bps);
	if (sz->complete_cb)
	  sz->complete_cb(zi.fname, 0, zi.bytes_sent, zi.modtime);

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
wctxpn(sz_t *sz, zreadline_t *zr, zm_t *zm, struct zm_fileinfo *zi)
{
	register char *p, *q;
	char name2[PATH_MAX+1];
	struct stat f;

	/* The getnak process is how the sender knows which protocol
	 * is it allowed to use.  Hopefully the receiver allows
	 * ZModem.  If it doesn't, we may fall back to YModem. */
	if (!zm->zmodem_requested)
		if (getnak(sz, zr, zm)) {
			log_debug("getnak failed");
			return ERROR;
		}

	q = (char *) 0;

	for (p=zi->fname, q=sz->txbuf ; *p; )
		if ((*q++ = *p++) == '/' && !sz->fullname)
			q = sz->txbuf;
	*q++ = 0;
	p=q;
	while (q < (sz->txbuf + MAX_BLOCK))
		*q++ = 0;
	if ((sz->input_f!=stdin) && *zi->fname && (fstat(fileno(sz->input_f), &f)!= -1)) {
		if (sz->hyperterm) {
			sprintf(p, "%lu", (long) f.st_size);
		} else {
			/* note that we may lose some information here
			 * in case mode_t is wider than an int. But i believe
			 * sending %lo instead of %o _could_ break compatability
			 */

			/* Spec 8.2: "[the sender will send a] ZCRCW
			 * data subpacket containing the file name,
			 * file length, modification date, and other
			 * information identical to that used by
			 * YMODEM batch." */
			sprintf(p, "%lu %lo %o 0 %d %ld", (long) f.st_size,
				f.st_mtime,
				(unsigned int)((sz->no_unixmode) ? 0 : f.st_mode),
				sz->filesleft, sz->totalleft);
		}
	}
	log_info(_("Sending: %s"),sz->txbuf);
	sz->totalleft -= f.st_size;
	if (--sz->filesleft <= 0)
		sz->totalleft = 0;
	if (sz->totalleft < 0)
		sz->totalleft = 0;

	/* force 1k blocks if name won't fit in 128 byte block */
	if (sz->txbuf[125])
		sz->blklen=1024;
	else {		/* A little goodie for IMP/KMD */
		sz->txbuf[127] = (f.st_size + 127) >>7;
		sz->txbuf[126] = (f.st_size + 127) >>15;
	}

	/* We'll send the file by ZModem, if the getnak process succeeded.  */
	if (zm->zmodem_requested)
		return zsendfile(sz, zr, zm, zi,sz->txbuf, 1+strlen(p)+(p-sz->txbuf));

	/* We'll have to send the file by YModem, I guess.  */
	if (wcputsec(sz, zr, zm, sz->txbuf, 0, 128)==ERROR) {
		log_debug("wcputsec failed");
		return ERROR;
	}
	return OK;
}


/* [mlg] Somewhere in this logic, this procedure tries to force the receiver
 * into ZModem mode? */
static int
getnak(sz_t *sz, zreadline_t *zr, zm_t *zm)
{
	int firstch;
	int tries=0;

	sz->lastrx = 0;
	for (;;) {
		tries++;
		switch (firstch = zreadline_getc(zr, 100)) {

		case ZPAD:
			/* Spec 7.3.1: "A binary header begins with
			 * the sequence ZPAD, ZDLE, ZBIN. */
			/* Spec 7.3.3: "A hex header begins with the
			 * sequence ZPAD ZPAD ZDLE ZHEX." */
			if (getzrxinit(sz, zr, zm))
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
			if ((sz->zrqinits_sent>1 || tries>1)
			    && sz->zrqinits_sent<4) {
				/* if we already sent a ZRQINIT we are
				 * using zmodem protocol and may send
				 * further ZRQINITs
				 */
				zm_store_header(0L);
				zm_send_hex_header(zm, ZRQINIT, Txhdr);
				sz->zrqinits_sent++;
			}
			continue;
		case WANTG:
			/* Set cbreak, XON/XOFF, etc. */
			io_mode(sz->io_mode_fd, 2);
			sz->optiong = TRUE;
			sz->blklen=1024;
		case WANTCRC:
			sz->crcflg = TRUE;
		case NAK:
			/* Spec 8.1: "The sending program awaits a
			 * command from the receiving port to start
			 * file transfers.  If a 'C', 'G', or NAK is
			 * received, and XMODEM or YMODEM file
			 * transfer is indicated.  */
			return FALSE;
		case CAN:
			if ((firstch = zreadline_getc(zr, 20)) == CAN
			    && sz->lastrx == CAN)
				return TRUE;
		default:
			break;
		}
		sz->lastrx = firstch;
	}
}


static int
wctx(sz_t *sz, zreadline_t *zr, zm_t *zm, struct zm_fileinfo *zi)
{
	register size_t thisblklen;
	register int sectnum, attempts, firstch;

	sz->firstsec=TRUE;  thisblklen = sz->blklen;
	log_debug("wctx:file length=%ld", (long) zi->bytes_total);

	while ((firstch=zreadline_getc(zr, zm->rxtimeout))!=NAK && firstch != WANTCRC
	  && firstch != WANTG && firstch!=TIMEOUT && firstch!=CAN)
		;
	if (firstch==CAN) {
		log_error(_("Receiver Cancelled"));
		return ERROR;
	}
	if (firstch==WANTCRC)
		sz->crcflg=TRUE;
	if (firstch==WANTG)
		sz->crcflg=TRUE;
	sectnum=0;
	for (;;) {
		if (zi->bytes_total <= (zi->bytes_sent + 896L))
			thisblklen = 128;
		if ( !filbuf(sz, sz->txbuf, thisblklen))
			break;
		if (wcputsec(sz, zr, zm, sz->txbuf, ++sectnum, thisblklen)==ERROR)
			return ERROR;
		zi->bytes_sent += thisblklen;
	}
	fclose(sz->input_f);
	attempts=0;
	do {
		zreadline_flushline(zr);
		putchar(EOT);
		fflush(stdout);
		++attempts;
	} while ((firstch=(zreadline_getc(zr,zm->rxtimeout)) != ACK) && attempts < RETRYMAX);
	if (attempts == RETRYMAX) {
		log_error(_("No ACK on EOT"));
		return ERROR;
	}
	else
		return OK;
}

static int
wcputsec(sz_t *sz, zreadline_t *zr, zm_t *zm, char *buf, int sectnum, size_t cseclen)
{
	int checksum, wcj;
	char *cp;
	unsigned oldcrc;
	int firstch;
	int attempts;

	firstch=0;	/* part of logic to detect CAN CAN */

	log_debug(_("Zmodem sectors/kbytes sent: %3d/%2dk"), sz->totsecs, sz->totsecs/8 );
	for (attempts=0; attempts <= RETRYMAX; attempts++) {
		sz->lastrx= firstch;
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
		if (sz->crcflg) {
			oldcrc=updcrc(0,updcrc(0,oldcrc));
			putchar(((int)oldcrc>>8) & 0xFF);
			putchar(((int)oldcrc) & 0xFF);
		}
		else
			putchar(checksum & 0xFF);

		fflush(stdout);
		if (sz->optiong) {
			sz->firstsec = FALSE; return OK;
		}
		firstch = zreadline_getc(zr, zm->rxtimeout);
gotnak:
		switch (firstch) {
		case CAN:
			if(sz->lastrx == CAN) {
cancan:
				log_error(_("Cancelled"));  return ERROR;
			}
			break;
		case TIMEOUT:
			log_error(_("Timeout on sector ACK")); continue;
		case WANTCRC:
			if (sz->firstsec)
				sz->crcflg = TRUE;
		case NAK:
			log_error(_("NAK on sector")); continue;
		case ACK:
			sz->firstsec=FALSE;
			sz->totsecs += (cseclen>>7);
			return OK;
		case ERROR:
			log_error(_("Got burst for sector ACK")); break;
		default:
			log_error(_("Got %02x for sector ACK"), firstch); break;
		}
		for (;;) {
			sz->lastrx = firstch;
			if ((firstch = zreadline_getc(zr,zm->rxtimeout)) == TIMEOUT)
				break;
			if (firstch == NAK || firstch == WANTCRC)
				goto gotnak;
			if (firstch == CAN && sz->lastrx == CAN)
				goto cancan;
		}
	}
	log_error(_("Retry Count Exceeded"));
	return ERROR;
}

/* fill buf with count chars padding with ^Z for CPM */
static size_t
filbuf(sz_t *sz, char *buf, size_t count)
{
	int c;
	size_t m;

	m = read(fileno(sz->input_f), buf, count);
	if (m <= 0)
		return 0;
	while (m < count)
		buf[m++] = 032;
	return count;
	m=count;
	if (sz->lfseen) {
		*buf++ = 012; --m; sz->lfseen = 0;
	}
	while ((c=getc(sz->input_f))!=EOF) {
		if (c == 012) {
			*buf++ = 015;
			if (--m == 0) {
				sz->lfseen = TRUE; break;
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
zfilbuf (sz_t *sz, struct zm_fileinfo *zi)
{
	size_t n;

	n = fread (sz->txbuf, 1, sz->blklen, sz->input_f);
	if (n < sz->blklen)
		zi->eof_seen = 1;
	else {
		/* save one empty paket in case file ends ob blklen boundary */
		int c = getc(sz->input_f);

		if (c != EOF || !feof(sz->input_f))
			ungetc(c, sz->input_f);
		else
			zi->eof_seen = 1;
	}
	return n;
}

#if 0
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

#endif

/*
 * Get the receiver's init parameters
 */
static int
getzrxinit(sz_t *sz, zreadline_t *zr, zm_t *zm)
{
	static int dont_send_zrqinit=1;
	int old_timeout=zm->rxtimeout;
	int n;
	struct stat f;
	size_t rxpos;
	int timeouts=0;

	zm->rxtimeout=100; /* 10 seconds */

	for (n=10; --n>=0; ) {
		/* we might need to send another zrqinit in case the first is
		 * lost. But *not* if getting here for the first time - in
		 * this case we might just get a ZRINIT for our first ZRQINIT.
		 * Never send more then 4 ZRQINIT, because
		 * omen rz stops if it saw 5 of them.
		 */
		if (sz->zrqinits_sent<4 && n!=10 && !dont_send_zrqinit) {
			sz->zrqinits_sent++;
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
			sz->rxflags = 0377 & Rxhdr[ZF0];
			sz->rxflags2 = 0377 & Rxhdr[ZF1];
			zm->txfcs32 = (sz->wantfcs32 && (sz->rxflags & CANFC32));
			{
				int old=zm->zctlesc;
				zm->zctlesc |= sz->rxflags & TESCCTL;
				/* update table - was initialised to not escape */
				if (zm->zctlesc && !old)
					zm_update_table(zm);
			}
			sz->rxbuflen = (0377 & Rxhdr[ZP0])+((0377 & Rxhdr[ZP1])<<8);
			if ( !(sz->rxflags & CANFDX))
				sz->txwindow = 0;
			log_debug("Rxbuflen=%d Tframlen=%d", sz->rxbuflen, sz->tframlen);
			if ( sz->play_with_sigint)
				signal(SIGINT, SIG_IGN);
			io_mode(sz->io_mode_fd,2);	/* Set cbreak, XON/XOFF, etc. */
			/* Override to force shorter frame length */
			if (sz->tframlen && sz->rxbuflen > sz->tframlen)
				sz->rxbuflen = sz->tframlen;
			if ( !sz->rxbuflen)
				sz->rxbuflen = 1024;
			log_debug("Rxbuflen=%d", sz->rxbuflen);

			/* If using a pipe for testing set lower buf len */
			fstat(0, &f);
			if (! (S_ISCHR(f.st_mode))) {
				sz->rxbuflen = MAX_BLOCK;
			}
			/*
			 * If input is not a regular file, force ACK's to
			 *  prevent running beyond the buffer limits
			 */
			fstat(fileno(sz->input_f), &f);
			if (!(S_ISREG(f.st_mode))) {
				sz->canseek = -1;
				/* return ERROR; */
			}
			/* Set initial subpacket length */
			if (sz->blklen < 1024) {	/* Command line override? */
				if (zm->baudrate > 300)
					sz->blklen = 256;
				if (zm->baudrate > 1200)
					sz->blklen = 512;
				if (zm->baudrate > 2400)
					sz->blklen = 1024;
			}
			if (sz->rxbuflen && sz->blklen>sz->rxbuflen)
				sz->blklen = sz->rxbuflen;
			if (sz->blkopt && sz->blklen > sz->blkopt)
				sz->blklen = sz->blkopt;
			log_debug("Rxbuflen=%d blklen=%d", sz->rxbuflen, sz->blklen);
			log_debug("Txwindow = %u Txwspac = %d", sz->txwindow, sz->txwspac);
			zm->rxtimeout=old_timeout;
			return (sendzsinit(sz, zr, zm));
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
sendzsinit(sz_t *sz, zreadline_t *zr, zm_t *zm)
{
	int c;

	if (Myattn[0] == '\0' && (!zm->zctlesc || (sz->rxflags & TESCCTL)))
		return OK;
	sz->errors = 0;
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
			if (++sz->errors > 19)
				return ERROR;
			continue;
		}
	}
}

/* Send file name and related info */
static int
zsendfile(sz_t *sz, zreadline_t *zr, zm_t *zm, struct zm_fileinfo *zi, const char *buf, size_t blen)
{
	int c;
	unsigned long crc;
	size_t rxpos;

	/* we are going to send a ZFILE. There cannot be much useful
	 * stuff in the line right now (*except* ZCAN?).
	 */

	for (;;) {
		/* Spec 8.2: "The sender then sends a ZFILE header
		 * with ZMODEM Conversion, Management, and Transport
		 * options followed by a ZCRCW data subpacket
		 * containing the file name, ...." */
		Txhdr[ZF0] = sz->lzconv;	/* file conversion request */
		Txhdr[ZF1] = sz->lzmanag;	/* file management request */
		if (sz->lskipnocor)
			Txhdr[ZF1] |= ZF1_ZMSKNOLOC;
		Txhdr[ZF2] = 0;	/* file transport compression request */
		Txhdr[ZF3] = 0; /* extended options */
		zm_send_binary_header(zm, ZFILE, Txhdr);
		ZM_SEND_DATA(buf, blen, ZCRCW);
again:
		c = zm_get_header(zm, Rxhdr, &rxpos);
		switch (c) {
		case ZRINIT:
			while ((c = zreadline_getc(zr, 50)) > 0)
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
			/* Spec 8.2: "[if] the receiver has a file
			 * with the same name and length, [it] may
			 * respond with a ZCRC header with a byte
			 * count, which requires the sender to perform
			 * a 32-bit CRC on the specified number of
			 * bytes in the file, and transmit the
			 * complement of the CRC is an answering ZCRC
			 * header." */
			crc = 0xFFFFFFFFL;
			if (!sz->mm_addr)
			{
				struct stat st;
				if (fstat (fileno (sz->input_f), &st) == 0) {
					sz->mm_size = st.st_size;
					sz->mm_addr = mmap (0, sz->mm_size, PROT_READ,
									MAP_SHARED, fileno (sz->input_f), 0);
					if ((caddr_t) sz->mm_addr == (caddr_t) - 1)
						sz->mm_addr = NULL;
					else {
						fclose (sz->input_f);
						sz->input_f = NULL;
					}
				}
			}
			if (sz->mm_addr) {
				size_t i;
				size_t count;
				char *p=sz->mm_addr;
				count=(rxpos < sz->mm_size && rxpos > 0)? rxpos: sz->mm_size;
				for (i=0;i<count;i++,p++) {
					crc = UPDC32(*p, crc);
				}
				crc = ~crc;
			} else
			if (sz->canseek >= 0) {
				if (rxpos==0) {
					struct stat st;
					if (0==fstat(fileno(sz->input_f),&st)) {
						rxpos=st.st_size;
					} else
						rxpos=-1;
				}
				while (rxpos-- && ((c = getc(sz->input_f)) != EOF))
					crc = UPDC32(c, crc);
				crc = ~crc;
				clearerr(sz->input_f);	/* Clear EOF */
				fseek(sz->input_f, 0L, 0);
			}
			zm_store_header(crc);
			zm_send_binary_header(zm, ZCRC, Txhdr);
			goto again;
		case ZSKIP:
		/* Spec 8.2: "[after deciding if the file name, file
		 * size, etc are acceptable] The receiver may respond
		 * with a ZSKIP header, which makes the sender proceed
		 * to the next file (if any)." */
			if (sz->input_f)
				fclose(sz->input_f);
			else if (sz->mm_addr) {
				munmap(sz->mm_addr,sz->mm_size);
				sz->mm_addr=NULL;
			}

			log_debug("receiver skipped");
			return c;
		case ZRPOS:
			/* Spec 8.2: "A ZRPOS header from the receiver
			 * initiates transmittion of the file data
			 * starting at the offset in the file
			 * specified by the ZRPOS header.  */
			/*
			 * Suppress zcrcw request otherwise triggered by
			 * lastsync==bytcnt
			 */
			if (!sz->mm_addr)
			if (rxpos && fseek(sz->input_f, (long) rxpos, 0)) {
				int er=errno;
				log_debug("fseek failed: %s", strerror(er));
				return ERROR;
			}
			if (rxpos)
				zi->bytes_skipped=rxpos;
			sz->bytcnt = zi->bytes_sent = rxpos;
			sz->lastsync = rxpos -1;
			/* Spec 8.2: [in response to ZRPOS] the sender
			 * sends a ZDATA binary header (with file
			 * position) followed by one or more data
			 * subpackets."  */
	 		return zsendfdata(sz, zr, zm, zi);
		}
	}
}

/* Send the data in the file */
static int
zsendfdata (sz_t *sz, zreadline_t *zr, zm_t *zm, struct zm_fileinfo *zi)
{
	static int c;
	static int junkcount;				/* Counts garbage chars received by TX */
	static size_t last_txpos = 0;
	static long last_bps = 0;
	static long not_printed = 0;
	static long total_sent = 0;
	static time_t low_bps=0;

	/* memmap that file, if necessary */
	if (!sz->mm_addr)
	{
		struct stat st;
		if (fstat (fileno (sz->input_f), &st) == 0) {
			sz->mm_size = st.st_size;
			sz->mm_addr = mmap (0, sz->mm_size, PROT_READ,
							MAP_SHARED, fileno (sz->input_f), 0);
			if ((caddr_t) sz->mm_addr == (caddr_t) - 1)
				sz->mm_addr = NULL;
			else {
				fclose (sz->input_f);
				sz->input_f = NULL;
			}
		}
	}

	if (sz->play_with_sigint)
		signal (SIGINT, onintr);

	sz->lrxpos = 0;
	junkcount = 0;
  somemore:
	/* Note that this whole next block is a
	 * setjmp block for error recovery.  The
	 * normal code path follows it. */
	if (setjmp (sz->intrjmp)) {
	  if (sz->play_with_sigint)
		  signal (SIGINT, onintr);
	  waitack:
		junkcount = 0;
		c = getinsync (sz, zr, zm, zi, 0);
	  gotack:
		switch (c) {
		default:
			if (sz->input_f)
				fclose (sz->input_f);
			return ERROR;
		case ZCAN:
			if (sz->input_f)
				fclose (sz->input_f);
			return ERROR;
		case ZSKIP:
			if (sz->input_f)
				fclose (sz->input_f);
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
		while (rdchk (sz->io_mode_fd)) {
			switch (zreadline_getc (zr, 1))
			{
			case CAN:
			case ZPAD:
				c = getinsync (sz, zr, zm, zi, 1);
				goto gotack;
			case XOFF:			/* Wait a while for an XON */
			case XOFF | 0200:
				zreadline_getc (zr, 100);
			}
		}
	}

	/* Spec 8.2: "A ZRPOS header from the receiver initiates
	 * transmittion of the file data starting at the offset in the
	 * file specified by the ZRPOS header.  */
	/* Spec 8.2: [in response to ZRPOS] the sender sends a ZDATA
	 * binary header (with file position) followed by one or more
	 * data subpackets."  */

	sz->txwcnt = 0;
	zm_store_header (zi->bytes_sent);
	zm_send_binary_header (zm, ZDATA, Txhdr);

	do {
		size_t n;
		int e;
		unsigned old = sz->blklen;
		sz->blklen = calc_blklen (sz, total_sent);
		total_sent += sz->blklen + OVERHEAD;
		if (sz->blklen != old)
			log_trace (_("blklen now %d\n"), sz->blklen);
		if (sz->mm_addr) {
			if (zi->bytes_sent + sz->blklen < sz->mm_size)
				n = sz->blklen;
			else {
				n = sz->mm_size - zi->bytes_sent;
				zi->eof_seen = 1;
			}
		} else
			n = zfilbuf (sz, zi);
		if (zi->eof_seen) {
			/* Spec 8.2: "In the absence of fatal error,
			 * the sender eventually encounters end of
			 * file.  If the end of file is encountered
			 * within a frame, the frame is closed with a
			 * ZCRCE data subpacket, which does not elicit
			 * a response except in case of error." */
			e = ZCRCE;
			log_trace("e=ZCRCE/eof seen");
		} else if (junkcount > 3) {
			/* Spec 8.2: "ZCRCW data subpackets expect a
			 * response before the next frame is sent." */
			e = ZCRCW;
			log_trace("e=ZCRCW/junkcount > 3");
		} else if (sz->bytcnt == sz->lastsync) {
			/* Spec 8.2: "ZCRCW data subpackets expect a
			 * response before the next frame is sent." */
			e = ZCRCW;
			log_trace("e=ZCRCW/bytcnt == sz->lastsync == %ld",
					(unsigned long) sz->lastsync);
		} else if (sz->txwindow && (sz->txwcnt += n) >= sz->txwspac) {
			/* Spec 8.2: "ZCRCQ data subpackets expect a
			 * ZACK response with the receiver's file
			 * offset if no error, otherwise a ZRPOS
			 * response with the last good file
			 * offset. */
			sz->txwcnt = 0;
			e = ZCRCQ;
			log_trace("e=ZCRCQ/Window");
		} else {
			/* Spec 8.2: "A data subpacket terminated by
			 * ZCRCG ... does not elicit a response unles
			 * an error is detected: more data
			 * subpacket(s) follow immediately."  */
			e = ZCRCG;
			log_trace("e=ZCRCG");
		}
		if ((sz->min_bps || sz->stop_time || sz->tick_cb)
			&& (not_printed > (sz->min_bps ? 3 : 7)
				|| zi->bytes_sent > last_bps / 2 + last_txpos)) {
			int minleft = 0;
			int secleft = 0;
			time_t now;
			last_bps = (zi->bytes_sent / timing (0,&now));
			if (last_bps > 0) {
				minleft = (zi->bytes_total - zi->bytes_sent) / last_bps / 60;
				secleft = ((zi->bytes_total - zi->bytes_sent) / last_bps) % 60;
			}
			if (sz->min_bps) {
				if (low_bps) {
					if (last_bps<sz->min_bps) {
						if (now-low_bps>=sz->min_bps_time) {
							/* too bad */
							log_info(_("zsendfdata: bps rate %ld below min %ld"),
								 last_bps, sz->min_bps);
							return ERROR;
						}
					} else
						low_bps=0;
				} else if (last_bps < sz->min_bps) {
					low_bps=now;
				}
			}
			if (sz->stop_time && now>=sz->stop_time) {
				/* too bad */
				log_info(_("zsendfdata: reached stop time"));
				return ERROR;
			}

			log_debug (_("Bytes Sent:%7ld/%7ld   BPS:%-8ld ETA %02d:%02d  "),
				  (long) zi->bytes_sent, (long) zi->bytes_total,
				  last_bps, minleft, secleft);
			if (sz->tick_cb) {
				bool more = sz->tick_cb(zi->fname, (long) zi->bytes_sent, (long) zi->bytes_total,
							last_bps, minleft, secleft);
				if (!more) {
					log_info(_("zsendfdata: tick callback returns FALSE"));
					return ERROR;
				}
			}
			last_txpos = zi->bytes_sent;
		} else
			not_printed++;
		ZM_SEND_DATA (DATAADR, n, e);
		sz->bytcnt = zi->bytes_sent += n;
		if (e == ZCRCW)
			/* Spec 8.2: "ZCRCW data subpackets expect a
			 * response before the next frame is sent." */
			goto waitack;
		/*
		 * If the reverse channel can be tested for data,
		 *  this logic may be used to detect error packets
		 *  sent by the receiver, in place of setjmp/longjmp
		 *  rdchk(fdes) returns non 0 if a character is available
		 */
		fflush (stdout);
		while (rdchk (sz->io_mode_fd)) {
			switch (zreadline_getc (zr, 1))
			{
			case CAN:
			case ZPAD:
				c = getinsync (sz, zr, zm, zi, 1);
				if (c == ZACK)
					break;
				/* zcrce - dinna wanna starta ping-pong game */
				ZM_SEND_DATA (sz->txbuf, 0, ZCRCE);
				goto gotack;
			case XOFF:			/* Wait a while for an XON */
			case XOFF | 0200:
				zreadline_getc (zr, 100);
			default:
				++junkcount;
			}
		}
		if (sz->txwindow) {
			size_t tcount = 0;
			while ((tcount = zi->bytes_sent - sz->lrxpos) >= sz->txwindow) {
				log_debug ("%ld (%ld,%ld) window >= %u", tcount,
					(long) zi->bytes_sent, (long) sz->lrxpos,
					sz->txwindow);
				if (e != ZCRCQ)
					ZM_SEND_DATA (sz->txbuf, 0, e = ZCRCQ);
				c = getinsync (sz, zr, zm, zi, 1);
				if (c != ZACK) {
					ZM_SEND_DATA (sz->txbuf, 0, ZCRCE);
					goto gotack;
				}
			}
			log_debug ("window = %ld", tcount);
		}
	} while (!zi->eof_seen);


	if (sz->play_with_sigint)
		signal (SIGINT, SIG_IGN);

	for (;;) {
		/* Spec 8.2: [after sending a file] The sender sends a
		 * ZEOF header with the file ending offset equal to
		 * the number of characters in the file. */
		zm_store_header (zi->bytes_sent);
		zm_send_binary_header (zm, ZEOF, Txhdr);
		switch (getinsync (sz, zr, zm, zi, 0)) {
		case ZACK:
			continue;
		case ZRPOS:
			goto somemore;
		case ZRINIT:
			/* If the receiver is satisfied with the file,
			 * it returns ZRINIT. */
			return OK;
		case ZSKIP:
			if (sz->input_f)
				fclose (sz->input_f);
			return c;
		default:
			if (sz->input_f)
				fclose (sz->input_f);
			return ERROR;
		}
	}
}

static int
calc_blklen(sz_t *sz, long total_sent)
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
		if (sz->error_count && sz->start_blklen >1024)
			return last_blklen=1024;
		else
			last_blklen/=2;
		return last_blklen=sz->start_blklen;
	}

	if (!sz->error_count) {
		/* that's fine */
		if (sz->start_blklen==sz->max_blklen)
			return sz->start_blklen;
		this_bytes_per_error=LONG_MAX;
		goto calcit;
	}

	if (sz->error_count!=last_error_count) {
		/* the last block was bad. shorten blocks until one block is
		 * ok. this is because very often many errors come in an
		 * short period */
		if (sz->error_count & 2)
		{
			last_blklen/=2;
			if (last_blklen < 32)
				last_blklen = 32;
			else if (last_blklen > 512)
				last_blklen=512;
			log_trace(_("calc_blklen: reduced to %d due to error\n"),
				 last_blklen);
		}
		last_error_count=sz->error_count;
		last_bytes_per_error=0; /* force recalc */
		return last_blklen;
	}

	this_bytes_per_error=total_sent / sz->error_count;
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
		 total_bytes,this_bytes_per_error,(long) sz->error_count);
	for (i=32;i<=sz->max_blklen;i*=2) {
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
getinsync(sz_t *sz, zreadline_t *zr, zm_t *zm, struct zm_fileinfo *zi, int flag)
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
			if (sz->input_f)
				clearerr(sz->input_f);	/* In case file EOF seen */
			if (!sz->mm_addr)
			if (fseek(sz->input_f, (long) rxpos, 0))
				return ERROR;
			zi->eof_seen = 0;
			sz->bytcnt = sz->lrxpos = zi->bytes_sent = rxpos;
			if (sz->lastsync == rxpos) {
				sz->error_count++;
			}
			sz->lastsync = rxpos;
			return c;
		case ZACK:
			sz->lrxpos = rxpos;
			if (flag || zi->bytes_sent == rxpos)
				return ZACK;
			continue;
		case ZRINIT:
		case ZSKIP:
			if (sz->input_f)
				fclose(sz->input_f);
			else if (sz->mm_addr) {
				munmap(sz->mm_addr,sz->mm_size);
				sz->mm_addr=NULL;
			}
			return c;
		case ERROR:
		default:
			sz->error_count++;
			zm_send_binary_header(zm, ZNAK, Txhdr);
			continue;
		}
	}
}


/* Say "bibi" to the receiver, try to do it cleanly */
static void
saybibi(zreadline_t *zr, zm_t *zm)
{
	for (;;) {
		zm_store_header(0L);		/* CAF Was zm_send_binary_header - minor change */

		/* Spec 8.3: "The sender closes the session with a
		 * ZFIN header.  The receiver acknowledges this with
		 * its own ZFIN header."  */
		zm_send_hex_header(zm, ZFIN, Txhdr);	/*  to make debugging easier */
		switch (zm_get_header(zm, Rxhdr,NULL)) {
		case ZFIN:
			/* Spec 8.3: "When the sender receives the
                         * acknowledging header, it sends two
                         * characters, "OO" (Over and Out) and exits
                         * to the operating system or application that
                         * invoked it." */
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
countem (sz_t *sz, int argc, char **argv)
{
	struct stat f;

	for (sz->totalleft = 0, sz->filesleft = 0; --argc >= 0; ++argv) {
		f.st_size = -1;
		log_trace ("\nCountem: %03d %s ", argc, *argv);
		if (access (*argv, R_OK) >= 0 && stat (*argv, &f) >= 0) {
			if (!S_ISDIR(f.st_mode) && !S_ISBLK(f.st_mode)) {
				++sz->filesleft;
				sz->totalleft += f.st_size;
			}
		} else if (strcmp (*argv, "-") == 0) {
			++sz->filesleft;
			sz->totalleft += DEFBYTL;
		}
		log_trace (" %ld", (long) f.st_size);
	}
	log_trace (_("\ncountem: Total %d %ld\n"),
				 sz->filesleft, sz->totalleft);
	calc_blklen (sz, sz->totalleft);
}

/* End of lsz.c */
