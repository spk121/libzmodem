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
#include "log.h"
#include "zmodem.h"
#include "crctab.h"
#include "zm.h"

#define MAX_BLOCK 8192

const char *program_name;		/* the name by which we were called */

static int no_timeout=FALSE;

struct rz_ {
	zm_t *zm;		/* Zmodem comm primitives' state. */
	// Workspaces
	char tcp_buf[256];	/* Buffer to receive TCP protocol
				 * synchronization strings from
				 * fout */
	char attn[ZATTNLEN+1];  /* Attention string rx sends to tx on err */
	char secbuf[MAX_BLOCK + 1]; /* Workspace to store up to 8k
				     * blocks */
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

	// Constant
	int restricted;	/* restricted; no /.. or ../ in filenames */
	                  /*  restricted > 0 prevents unlinking
			     restricted > 0 prevents overwriting dot files
			     restricted = 2 prevents overwriting files
			     restricted > 0 restrict files to curdir or PUBDIR
			   */
	int topipe;		/* A flag. When true, open the file as a pipe. */
	int makelcpathname;  /* A flag. When true, make received pathname lowercase. */
	int nflag;		/* A flag. Don't really transfer files */
	int rxclob;		/* A flag. Allow clobbering existing file */
	int try_resume; /* A flag. When true, try restarting downloads */
	int junk_path;  /* A flag. When true, ignore the path
			 * component of an incoming filename. */
	int tcp_flag;		/* Not a flag.
				* 0 = don't use TCP
				* 2 = tcp server
				* 3 = tcp client */
	int o_sync;		/* A flag. When true, each write will
				 * be reliably completed before
				 * returning. */
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

	bool (*tick_cb)(const char *fname, long bytes_sent, long bytes_total,
			long last_bps, int min_left, int sec_left);
	void (*complete_cb)(const char *filename, int result, size_t size, time_t date);
	bool (*approver_cb)(const char *filename, size_t size, time_t date);


};

typedef struct rz_ rz_t;

rz_t *rz_init(int fd, size_t readnum, size_t bufsize, int no_timeout,
	      int rxtimeout, int znulls, int eflag, int baudrate, int zctlesc, int zrwindow,
	      int under_rsh, int restricted, char lzmanag,
	      int nflag, int junk_path,
	      unsigned long min_bps, long min_bps_time,
	      time_t stop_time, int try_resume,
	      int makelcpathname, int rxclob,
	      int o_sync, int tcp_flag, int topipe,
	      bool tick_cb(const char *fname, long bytes_sent, long bytes_total,
			   long last_bps, int min_left, int sec_left),
	      void complete_cb(const char *filename, int result, size_t size, time_t date),
	      bool approver_cb(const char *filename, size_t size, time_t date)
);


static int rz_receive_files (rz_t *rz, struct zm_fileinfo *);
static int rz_zmodem_session_startup (rz_t *rz);
static void rz_checkpath (rz_t *rz, const char *name);
static void chkinvok(const char *s, int *ptopipe);
static void report (int sct);
static void uncaps (char *s);
static int IsAnyLower (const char *s);
static int rz_write_string_to_file (rz_t *rz, struct zm_fileinfo *zi, char *buf, size_t n);
static int rz_process_header (rz_t *rz, char *name, struct zm_fileinfo *);
static int rz_receive_sector (rz_t *rz, size_t *Blklen, char *rxbuf, unsigned int maxtime);
static int rz_receive_sectors (rz_t *rz, struct zm_fileinfo *);
static int rz_receive_pathname (rz_t *rz, struct zm_fileinfo *, char *rpn);
static int rz_receive (rz_t *rz);
static int rz_receive_file (rz_t *rz, struct zm_fileinfo *);
static void exec2 (const char *s);
static int rz_closeit (rz_t *rz, struct zm_fileinfo *);
static int sys2 (const char *s);
static void write_modem_escaped_string_to_stdout (const char *s);
static size_t getfree (void);

rz_t*
rz_init(int fd, size_t readnum, size_t bufsize, int no_timeout,
	      int rxtimeout, int znulls, int eflag, int baudrate, int zctlesc, int zrwindow,
	int under_rsh, int restricted, char lzmanag,
	int nflag, int junk_path,
	unsigned long min_bps, long min_bps_time,
	time_t stop_time, int try_resume,
	int makelcpathname, int rxclob, int o_sync, int tcp_flag, int topipe,
	bool tick_cb(const char *fname, long bytes_sent, long bytes_total,
		     long last_bps, int min_left, int sec_left),
	void complete_cb(const char *filename, int result, size_t size, time_t date),
	bool approver_cb(const char *filename, size_t size, time_t date)
	)
{
	rz_t *rz = (rz_t *)malloc(sizeof(rz_t));
	memset (rz, 0, sizeof(rz_t));
	rz->zm = zm_init(fd, readnum, bufsize, no_timeout,
			 rxtimeout, znulls, eflag, baudrate, zctlesc, zrwindow);
	rz->under_rsh = under_rsh;
	rz->restricted = restricted;
	rz->lzmanag = lzmanag;
	rz->zconv = 0;
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
	rz->tick_cb = tick_cb;
	rz->complete_cb = complete_cb;
	rz->approver_cb = approver_cb;
	return rz;

}

/* called by signal interrupt or terminate to clean things up */
static void
bibi(int n)
{
	//FIXME: figure out how to avoid global zmodem_requested
	// if (zmodem_requested)
	// 	write_modem_escaped_string_to_stdout(Attn);
	// canit(zr, STDOUT_FILENO);
	io_mode(0,0);
	log_fatal(_("caught signal %s; exiting"), n);
	exit(128+n);
}

/*
 * Let's receive something already.
 */

size_t zmodem_receive(const char *directory,
		      bool approver_cb(const char *filename, size_t size, time_t date),
		      bool tick_cb(const char *fname, long bytes_sent, long bytes_total, long last_bps, int min_left, int sec_left),
		      void complete_cb(const char *filename, int result, size_t size, time_t date),
		      uint64_t min_bps,
		      uint32_t flags)
{
	log_set_level(LOG_ERROR);
	rz_t *rz = rz_init(0, /* fd */
			   8192, /* readnum */
			   16384, /* bufsize */
			   1, /* no_timeout */
			   100,		 /* rxtimeout */
			   0,		 /* znulls */
			   0,		 /* eflag */
			   2400,	 /* baudrate */
			   0,		 /* zctlesc */
			   1400,	 /* zrwindow */
			   0,		 /* under_rsh */
			   1,		 /* restricted */
			   0,		 /* lzmanag */
			   0,		 /* nflag */
			   0,		 /* junk_path */
			   0,		 /* min_bps */
			   120,		 /* min_bps_tim */
			   0,		 /* stop_time */
			   0,		 /* try_resume */
			   1,		 /* makelcpathname */
			   0,		 /* rxclob */
			   0,		 /* o_sync */
			   0,		 /* tcp_flag */
			   0,		 /* topipe */
			   tick_cb,
			   complete_cb,
			   approver_cb
		);
	rz->zm->baudrate = io_mode(0,1);
	int exitcode = 0;
	if (rz_receive(rz)==ERROR) {
		exitcode=0200;
		zreadline_canit(rz->zm->zr, STDOUT_FILENO);
	}
	io_mode(0,0);
	if (exitcode && !rz->zm->zmodem_requested)
		zreadline_canit(rz->zm->zr, STDOUT_FILENO);
	if (exitcode)
		log_info(_("Transfer incomplete"));
	else
		log_info(_("Transfer complete"));
	exit(exitcode);

	return 0u;
}


static int
rz_receive(rz_t *rz)
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
	c = rz_zmodem_session_startup(rz);
	if (c != 0) {
		if (c == ZCOMPL)
			return OK;
		if (c == ERROR)
			goto fubar;
		c = rz_receive_files(rz, &zi);

		if (c)
			goto fubar;
	} else {
		for (;;) {
			timing(1,NULL);
			if (rz_receive_pathname(rz, &zi, rz->secbuf)== ERROR)
				goto fubar;
			if (rz->secbuf[0]==0)
				return OK;
			if (rz_process_header(rz, rz->secbuf, &zi) == ERROR)
				goto fubar;
			if (rz_receive_sectors(rz, &zi)==ERROR)
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
	zreadline_canit(rz->zm->zr, STDOUT_FILENO);
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
rz_receive_pathname(rz_t *rz, struct zm_fileinfo *zi, char *rpn)
{
	register int c;
	size_t Blklen=0;		/* record length of received packets */

	zreadline_getc(rz->zm->zr, 1);

et_tu:
	rz->firstsec=TRUE;
	zi->eof_seen=FALSE;
	putchar(WANTCRC);
	fflush(stdout);
	zreadline_flushline(rz->zm->zr); /* Do read next time ... */
	while ((c = rz_receive_sector(rz, &Blklen, rpn, 100)) != 0) {
		if (c == WCEOT) {
			log_error( _("Pathname fetch returned EOT"));
			putchar(ACK);
			fflush(stdout);
			zreadline_flushline(rz->zm->zr);	/* Do read next time ... */
			zreadline_getc(rz->zm->zr, 1);
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
rz_receive_sectors(rz_t *rz, struct zm_fileinfo *zi)
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
		zreadline_flushline(rz->zm->zr);	/* Do read next time ... */
		sectcurr=rz_receive_sector(rz, &Blklen, rz->secbuf,
			(unsigned int) ((sectnum&0177) ? 50 : 130));
		report(sectcurr);
		if (sectcurr==((sectnum+1) &0377)) {
			sectnum++;
			if (zi->bytes_total && R_BYTESLEFT(zi) < Blklen)
				Blklen=R_BYTESLEFT(zi);
			zi->bytes_received+=Blklen;
			if (rz_write_string_to_file(rz, zi, rz->secbuf, Blklen)==ERROR)
				return ERROR;
			sendchar=ACK;
		}
		else if (sectcurr==(sectnum&0377)) {
			log_error( _("Received dup Sector"));
			sendchar=ACK;
		}
		else if (sectcurr==WCEOT) {
			if (rz_closeit(rz, zi))
				return ERROR;
			putchar(ACK);
			fflush(stdout);
			zreadline_flushline(rz->zm->zr);	/* Do read next time ... */
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
 * Rz_Receive_Sector fetches a Ward Christensen type sector.
 * Returns sector number encountered or ERROR if valid sector not received,
 * or CAN CAN received
 * or WCEOT if eot sector
 * time is timeout for first char, set to 4 seconds thereafter
 ***************** NO ACK IS SENT IF SECTOR IS RECEIVED OK **************
 *    (Caller must do that when he is good and ready to get next sector)
 */
static int
rz_receive_sector(rz_t *rz, size_t *Blklen, char *rxbuf, unsigned int maxtime)
{
	register int checksum, wcj, firstch;
	register unsigned short oldcrc;
	register char *p;
	int sectcurr;

	rz->lastrx = 0;
	for (rz->errors = 0; rz->errors < RETRYMAX; rz->errors++) {

		if ((firstch=zreadline_getc(rz->zm->zr, maxtime))==STX) {
			*Blklen=1024; goto get2;
		}
		if (firstch==SOH) {
			*Blklen=128;
get2:
			sectcurr=zreadline_getc(rz->zm->zr, 1);
			if ((sectcurr+(oldcrc=zreadline_getc(rz->zm->zr, 1)))==0377) {
				oldcrc=checksum=0;
				for (p=rxbuf,wcj=*Blklen; --wcj>=0; ) {
					if ((firstch=zreadline_getc(rz->zm->zr, 1)) < 0)
						goto bilge;
					oldcrc=updcrc(firstch, oldcrc);
					checksum += (*p++ = firstch);
				}
				if ((firstch=zreadline_getc(rz->zm->zr,1)) < 0)
					goto bilge;
				oldcrc=updcrc(firstch, oldcrc);
				if ((firstch=zreadline_getc(rz->zm->zr,1)) < 0)
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
		else if (firstch==EOT && zreadline_getc(rz->zm->zr,1)==TIMEOUT)
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
			while(cnt-- && zreadline_getc(rz->zm->zr,1)!=TIMEOUT)
				;
		}
		if (rz->firstsec) {
			putchar(WANTCRC);
			fflush(stdout);
			zreadline_flushline(rz->zm->zr);	/* Do read next time ... */
		} else {
			maxtime=40;
			putchar(NAK);
			fflush(stdout);
			zreadline_flushline(rz->zm->zr);	/* Do read next time ... */
		}
	}
	/* try to stop the bubble machine. */
	zreadline_canit(rz->zm->zr, STDOUT_FILENO);
	return ERROR;
}


/*
 * Process incoming file information header
 */
static int
rz_process_header(rz_t *rz, char *name, struct zm_fileinfo *zi)
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
	rz->thisbinary = TRUE;
	if (rz->lzmanag)
		rz->zmanag = rz->lzmanag;

	/*
	 *  Process ZMODEM remote file management requests
	 */
	if (rz->zconv == ZCNL)	/* Remote ASCII override */
		rz->thisbinary = 0;
	if (rz->zconv == ZCBIN)	/* Remote Binary override */
		rz->thisbinary = TRUE;
	if (rz->zconv == ZCBIN && rz->try_resume)
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
			int r = zm_do_crc_check(rz->zm, rz->fout, zi->bytes_total,0);
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


	if (!rz->zm->zmodem_requested && rz->makelcpathname && !IsAnyLower(name_static)
	  && !(zi->mode&UNIXFILE))
		uncaps(name_static);

	if (rz->approver_cb)
		if (!rz->approver_cb(name_static, zi->bytes_total, zi->modtime)) {
			log_info("%s: rejected by approver callback", rz->pathname);
			return ERROR;
		}

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
		rz_checkpath(rz, name_static);
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
					int r = zm_do_crc_check(rz->zm, rz->fout,zi->bytes_total,st.st_size);
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
		if (rz->o_sync) {
			int oldflags;
			oldflags = fcntl (fileno(rz->fout), F_GETFD, 0);
			if (oldflags>=0 && !(oldflags & O_SYNC)) {
				oldflags|=O_SYNC;
				fcntl (fileno(rz->fout), F_SETFD, oldflags); /* errors don't matter */
			}
		}
	}
	zi->bytes_received=zi->bytes_skipped;

	return OK;
}


/*
 * Rz_Write_String_To_File writes the n characters of buf to receive file fout.
 *  If not in binary mode, carriage returns, and all characters
 *  starting with CPMEOF are discarded.
 */
static int
rz_write_string_to_file(rz_t *rz, struct zm_fileinfo *zi, char *buf, size_t n)
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
rz_checkpath(rz_t *rz, const char *name)
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
			zreadline_canit(rz->zm->zr, STDOUT_FILENO);
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
			zreadline_canit(rz->zm->zr, STDOUT_FILENO);
			log_info(_("%s: Security Violation"),program_name);
			bibi(-1);
		}
		if (rz->restricted > 1) {
			if (name[0]=='.' || strstr(name,"/.")) {
				zreadline_canit(rz->zm->zr, STDOUT_FILENO);
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
rz_zmodem_session_startup(rz_t *rz)
{
	register int c, n;
	int zrqinits_received=0;
	size_t bytes_in_block=0;

	/* Spec 8.1: "When the ZMODEM receive program starts, it
	   immediately sends a ZRINIT header to initiate ZMODEM file
	   transfers...  The receive program resends its header at
	   intervals for a suitable period of time (40 seconds
	   total)...."

	   On startup rz->tryzhdrtype is, by default, set to ZRINIT
	*/

	for (n=rz->zm->zmodem_requested?15:5;
		 (--n + zrqinits_received) >=0 && zrqinits_received<10; ) {
		/* Set buffer length (0) and capability flags */

		/* We're going to snd a ZRINIT packet. */  
		zm_set_header_payload_bytes(rz->zm,
#ifdef CANBREAK
					    (rz->zm->zctlesc ?
					     (CANFC32|CANFDX|CANOVIO|CANBRK|TESCCTL)
					     : (CANFC32|CANFDX|CANOVIO|CANBRK)),
#else
					    (rz->zm->zctlesc ?
					     (CANFC32|CANFDX|CANOVIO|TESCCTL)
					     : (CANFC32|CANFDX|CANOVIO)),
#endif
					    0, 0, 0);
		zm_send_hex_header(rz->zm, rz->tryzhdrtype);

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
		switch (zm_get_header(rz->zm, NULL)) {
		case ZRQINIT:

			/* Spec 8.1: "[after sending ZRINIT] if the
			 * receiving program receives a ZRQINIT
			 * header, it resends the ZRINIT header." */

			/* getting one ZRQINIT is totally ok. Normally a ZFILE follows
			 * (and might be in our buffer, so don't flush it). But if we
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
			rz->zconv = rz->zm->Rxhdr[ZF0];
			if (!rz->zconv)
				/* resume with sz -r is impossible (at least with unix sz)
				 * if this is not set */
				rz->zconv=ZCBIN;
			if (rz->zm->Rxhdr[ZF1] & ZF1_ZMSKNOLOC) {
				rz->zm->Rxhdr[ZF1] &= ~(ZF1_ZMSKNOLOC);
				rz->skip_if_not_found=TRUE;
			}
			rz->zmanag = rz->zm->Rxhdr[ZF1];
			rz->ztrans = rz->zm->Rxhdr[ZF2];
			rz->tryzhdrtype = ZRINIT;
			c = zm_receive_data(rz->zm, rz->secbuf, MAX_BLOCK,&bytes_in_block);
			rz->zm->baudrate = io_mode(0,3);
			if (c == GOTCRCW)
				return ZFILE;
			zm_send_hex_header(rz->zm, ZNAK);
			goto again;
		case ZSINIT:
			/* Spec 8.1: "[after receiving the ZRINIT]
			 * then sender may then send an optional
			 * ZSINIT frame to define the receiving
			 * program's Attn sequence, or to specify
			 * complete control character escaping.  If
			 * the ZSINIT header specified ESCCTL or ESC8,
			 * a HEX header is used, and the receiver
			 * activates the specified ESC modes before
			 * reading the following data subpacket.  */

			/* this once was:
			 * Zctlesc = TESCCTL & zm->Rxhdr[ZF0];
			 * trouble: if rz get --escape flag:
			 * - it sends TESCCTL to sz,
			 *   get a ZSINIT _without_ TESCCTL (yeah - sender didn't know),
			 *   overwrites Zctlesc flag ...
			 * - sender receives TESCCTL and uses "|=..."
			 * so: sz escapes, but rz doesn't unescape ... not good.
			 */
			rz->zm->zctlesc |= (TESCCTL & rz->zm->Rxhdr[ZF0]);
			if (zm_receive_data(rz->zm, rz->attn, ZATTNLEN, &bytes_in_block) == GOTCRCW) {
				/* Spec 8.1: "[after receiving a
				 * ZSINIT] the receiver sends a ZACK
				 * header in response, containing
				 * either the serial number of the
				 * receiving program, or 0." */
				zm_set_header_payload(rz->zm, 1L);
				zm_send_hex_header(rz->zm, ZACK);
				goto again;
			}
			zm_send_hex_header(rz->zm, ZNAK);
			goto again;
		case ZFREECNT:
			zm_set_header_payload(rz->zm, getfree());
			zm_send_hex_header(rz->zm, ZACK);
			goto again;
		case ZCOMPL:
			goto again;
		default:
			continue;
		case ZFIN:
			zm_ackbibi(rz->zm);
			return ZCOMPL;
		case ZRINIT:
			/* Spec 8.1: "If [after sending ZRINIT] the
			   receiving program receives a ZRINIT header,
			   it is an echo indicating that the sending
			   program is not operational."  */
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
rz_receive_files(rz_t *rz, struct zm_fileinfo *zi)
{
	register int c;

	for (;;) {
		timing(1,NULL);
		c = rz_receive_file(rz, zi);
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
			switch (rz_zmodem_session_startup(rz)) {
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
rz_receive_file(rz_t *rz, struct zm_fileinfo *zi)
{
	register int c, n;
	long last_rxbytes=0;
	unsigned long last_bps=0;
	long not_printed=0;
	time_t low_bps=0;
	size_t bytes_in_block=0;

	zi->eof_seen=FALSE;

	n = 20;

	if (rz_process_header(rz, rz->secbuf,zi) == ERROR) {
		return (rz->tryzhdrtype = ZSKIP);
	}

	for (;;) {
		zm_set_header_payload(rz->zm, zi->bytes_received);
		zm_send_hex_header(rz->zm, ZRPOS);
		goto skip_oosb;
nxthdr:
		if (anker) {
			oosb_t *akt,*last,*next;
			for (akt=anker,last=NULL;akt;last= akt ? akt : last ,akt=next) {
				if (akt->pos==zi->bytes_received) {
					rz_write_string_to_file(rz, zi, akt->data, akt->len);
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
		c = zm_get_header(rz->zm, NULL);
		switch (c) {
		default:
			log_debug("rz_receive_file: zm_get_header returned %d", c);
			return ERROR;
		case ZNAK:
		case TIMEOUT:
			if ( --n < 0) {
				log_debug("rz_receive_file: zm_get_header returned %d", c);
				return ERROR;
			}
		case ZFILE:
			zm_receive_data(rz->zm, rz->secbuf, MAX_BLOCK,&bytes_in_block);
			continue;
		case ZEOF:
			if (zm_reclaim_receive_header(rz->zm) != (long) zi->bytes_received) {
				/*
				 * Ignore eof if it's at wrong place - force
				 *  a timeout because the eof might have gone
				 *  out before we sent our zrpos.
				 */
				rz->errors = 0;
				goto nxthdr;
			}
			if (rz_closeit(rz, zi)) {
				rz->tryzhdrtype = ZFERR;
				log_debug("rz_receive_file: rz_closeit returned <> 0");
				return ERROR;
			}
			log_debug("rz_receive_file: normal EOF");
			if (rz->complete_cb)
				rz->complete_cb(zi->fname, 0, zi->bytes_sent, zi->modtime);
			return c;
		case ERROR:	/* Too much garbage in header search error */
			if ( --n < 0) {
				log_debug("rz_receive_file: zm_get_header returned %d", c);
				return ERROR;
			}
			write_modem_escaped_string_to_stdout(rz->attn);
			continue;
		case ZSKIP:
			rz_closeit(rz, zi);
			log_debug("rz_receive_file: Sender SKIPPED file");
			return c;
		case ZDATA:
			if (zm_reclaim_receive_header(rz->zm) != (long) zi->bytes_received) {
				oosb_t *neu;
				size_t pos=zm_reclaim_receive_header(rz->zm);
				if ( --n < 0) {
					log_debug("rz_receive_file: out of sync");
					return ERROR;
				}
				switch (c = zm_receive_data(rz->zm, rz->secbuf, MAX_BLOCK,&bytes_in_block))
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
				write_modem_escaped_string_to_stdout(rz->attn);  continue;
			}
moredata:
			if ((rz->min_bps || rz->stop_time || rz->tick_cb)
			    && (not_printed > (rz->min_bps ? 3 : 7)
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
								log_debug(_("rz_receive_file: bps rate %ld below min %ld"),
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
					log_debug(_("rz_receive_file: reached stop time"));
					return ERROR;
				}

				log_info(_("\rBytes received: %7ld/%7ld   BPS:%-6ld ETA %02d:%02d  "),
					 (long) zi->bytes_received, (long) zi->bytes_total,
					 last_bps, minleft, secleft);
				if (rz->tick_cb)
					rz->tick_cb(zi->fname, zi->bytes_received,
						    zi->bytes_total, last_bps, minleft,
						    secleft);
				last_rxbytes=zi->bytes_received;
				not_printed=0;
			} else
				not_printed++;
			switch (c = zm_receive_data(rz->zm, rz->secbuf, MAX_BLOCK,&bytes_in_block))
			{
			case ZCAN:
				log_debug("rz_receive_file: zm_receive_data returned %d", c);
				return ERROR;
			case ERROR:	/* CRC error */
				if ( --n < 0) {
					log_debug("rz_receive_file: zm_get_header returned %d", c);
					return ERROR;
				}
				write_modem_escaped_string_to_stdout(rz->attn);
				continue;
			case TIMEOUT:
				if ( --n < 0) {
					log_debug("rz_receive_file: zm_get_header returned %d", c);
					return ERROR;
				}
				continue;
			case GOTCRCW:
				n = 20;
				rz_write_string_to_file(rz, zi, rz->secbuf, bytes_in_block);
				zi->bytes_received += bytes_in_block;
				zm_set_header_payload(rz->zm, zi->bytes_received);
				zm_send_hex_header(rz->zm, ZACK | 0x80);
				goto nxthdr;
			case GOTCRCQ:
				n = 20;
				rz_write_string_to_file(rz, zi, rz->secbuf, bytes_in_block);
				zi->bytes_received += bytes_in_block;
				zm_set_header_payload(rz->zm, zi->bytes_received);
				zm_send_hex_header(rz->zm, ZACK);
				goto moredata;
			case GOTCRCG:
				n = 20;
				rz_write_string_to_file(rz, zi, rz->secbuf, bytes_in_block);
				zi->bytes_received += bytes_in_block;
				goto moredata;
			case GOTCRCE:
				n = 20;
				rz_write_string_to_file(rz, zi, rz->secbuf, bytes_in_block);
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
write_modem_escaped_string_to_stdout(const char *s)
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
rz_closeit(rz_t *rz, struct zm_fileinfo *zi)
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
