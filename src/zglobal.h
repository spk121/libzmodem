#ifndef ZMODEM_GLOBAL_H
#define ZMODEM_GLOBAL_H

/* zglobal.h - prototypes etcetera for lrzsz

  Copyright (C) until 1998 Chuck Forsberg (OMEN Technology Inc)
  Copyright (C) 1994 Matt Porter
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
*/

#include "config.h"
#include <sys/types.h>
#include <stdarg.h>
#include <sys/time.h>
#include <time.h>
#include <sys/select.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>

/* Take care of NLS matters.  */
#if HAVE_LOCALE_H
# include <locale.h>
#endif
#if !HAVE_SETLOCALE
# define setlocale(Category, Locale) /* empty */
#endif

#if ENABLE_NLS
# include <libintl.h>
# define _(Text) gettext (Text)
#else
# define bindtextdomain(Domain, Directory) /* empty */
# define textdomain(Domain) /* empty */
# define _(Text) Text
#endif

#if defined HAVE_UNISTD_H
# include <unistd.h>
#endif

#include <limits.h>

/* Don't include sys/param.h if it already has been.  */
#if defined(HAVE_SYS_PARAM_H) && !defined(PATH_MAX) && !defined(MAXPATHLEN)
# include <sys/param.h>
#endif

#if !defined(LONG_MAX) && defined(HAVE_LIMITS_H)
# include <limits.h>
#endif

#ifdef __GNUC__

#if __GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ > 5)
# define LRZSZ_ATTRIB_SECTION(x) __attribute__((section(#x)))
#endif

#if __GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ > 4)
# define LRZSZ_ATTRIB_CONST  __attribute__((__const__))
#endif

    /* gcc.info sagt, noreturn wäre ab 2.5 verfügbar. HPUX-gcc 2.5.8
     * kann es noch nicht - what's this?
     */
#if __GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ > 5)
# define LRZSZ_ATTRIB_NORET  __attribute__((__noreturn__))
#endif

#if __GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ > 5)
# define LRZSZ_ATTRIB_PRINTF(formatnr,firstargnr)  \
    __attribute__((__format__ (printf,formatnr,firstargnr)))
#endif

#if __GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ > 6)
#define LRZSZ_ATTRIB_UNUSED __attribute__((__unused__))
#endif

#if __GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ >= 7)
# define LRZSZ_ATTRIB_REGPARM(n)  \
    __attribute__((__regparm__ (n)))
#endif
#endif /* __GNUC__ */
#ifndef LRZSZ_ATTRIB_REGPARM
#define LRZSZ_ATTRIB_REGPARM(n)
#endif
#ifndef LRZSZ_ATTRIB_UNUSED
#define LRZSZ_ATTRIB_UNUSED
#endif
#ifndef LRZSZ_ATTRIB_NORET
#define LRZSZ_ATTRIB_NORET
#endif
#ifndef LRZSZ_ATTRIB_CONST
#define LRZSZ_ATTRIB_CONST
#endif
#ifndef LRZSZ_ATTRIB_PRINTF
#define LRZSZ_ATTRIB_PRINTF(x,y)
#endif
#ifndef LRZSZ_ATTRIB_SECTION
#define LRZSZ_ATTRIB_SECTION(n)
#endif
#undef LRZSZ_ATTRIB_SECTION
#define LRZSZ_ATTRIB_SECTION(x)
#undef LRZSZ_ATTRIB_REGPARM
#define LRZSZ_ATTRIB_REGPARM(x)


#define OK 0
#define FALSE 0
#define TRUE 1
#define ERROR (-1)

/* Ward Christensen / CP/M parameters - Don't change these! */
#define ENQ 005
#define CAN ('X'&037)
#define XOFF ('s'&037)
#define XON ('q'&037)
#define SOH 1
#define STX 2
#define EOT 4
#define ACK 6
#define NAK 025
#define CPMEOF 032
#define WANTCRC 0103    /* send C not NAK to get crc not checksum */
#define WANTG 0107  /* Send G not NAK to get nonstop batch xmsn */
#define TIMEOUT (-2)
#define RCDO (-3)
#define WCEOT (-10)

#define RETRYMAX 10

#define UNIXFILE 0xF000  /* The S_IFMT file mask bit for stat */

#define DEFBYTL 2000000000L	/* default rx file size */

enum zm_type_enum {
	ZM_XMODEM,
	ZM_YMODEM,
	ZM_ZMODEM
};

struct zm_fileinfo {
	char *fname;
	time_t modtime;
	mode_t mode;
	size_t bytes_total;
	size_t bytes_sent;
	size_t bytes_received;
	size_t bytes_skipped; /* crash recovery */
	int    eof_seen;
};

#define R_BYTESLEFT(x) ((x)->bytes_total-(x)->bytes_received)

extern enum zm_type_enum protocol;

extern const char *program_name;        /* the name by which we were called */
extern int Verbose;
extern int errors;
extern int no_timeout;
extern int Zctlesc;    /* Encode control characters */
extern int under_rsh;

void bibi (int n);

#define sendline(c) putchar((c) & 0377)
#define xsendline(c) putchar(c)

/* zreadline.c */
extern char *readline_ptr; /* pointer for removing chars from linbuf */
extern int readline_left; /* number of buffered chars left to read */
#define READLINE_PF(timeout) \
    (--readline_left >= 0? (*readline_ptr++ & 0377) : readline_internal(timeout))

int readline_internal (unsigned int timeout);
void readline_purge (void);
void readline_setup (int fd, size_t readnum, 
	size_t buffer_size) LRZSZ_ATTRIB_SECTION(lrzsz_rare);


/* rbsb.c */
extern int Fromcu;
extern int Twostop;
extern int iofd;
extern unsigned Baudrate;

void zperr (const char *fmt, ...);
void zpfatal (const char *fmt, ...);
void vfile (const char *format, ...);
#define vchar(x) putc(x,stderr)
#define vstring(x) fputs(x,stderr)

#ifdef __GNUC__
#if __GNUC__ > 1
#define vstringf(format,args...) fprintf(stderr,format, ##args)
#endif
#endif
#ifndef vstringf
void vstringf (const char *format, ...);
#endif
#define VPRINTF(level,format_args) do {if ((Verbose)>=(level)) \
	vstringf format_args ; } while(0)

/* rbsb.c */
int from_cu (void) LRZSZ_ATTRIB_SECTION(lrzsz_rare);
int rdchk (int fd);
int io_mode (int fd, int n) LRZSZ_ATTRIB_SECTION(lrzsz_rare);
void sendbrk (int fd);
#define flushmo() fflush(stdout)
void purgeline (int fd);
void canit (int fd);


/* crctab.c */
extern unsigned short crctab[256];
#define updcrc(cp, crc) ( crctab[((crc >> 8) & 255)] ^ (crc << 8) ^ cp)
extern long cr3tab[];
#define UPDC32(b, c) (cr3tab[((int)c ^ b) & 0xff] ^ ((c >> 8) & 0x00FFFFFF))

/* zm.c */
#include "zmodem.h"
extern unsigned int Rxtimeout;        /* Tenths of seconds to wait for something */
extern int bytes_per_error;  /* generate one error around every x bytes */

/* Globals used by ZMODEM functions */
extern int Rxframeind;     /* ZBIN ZBIN32, or ZHEX type of frame received */
extern int Rxtype;     /* Type of header received */
extern int Zrwindow;       /* RX window size (controls garbage count) */
/* extern int Rxcount; */       /* Count of data bytes received */
extern char Rxhdr[4];      /* Received header */
extern char Txhdr[4];      /* Transmitted header */
extern long Txpos;     /* Transmitted file position */
extern int Txfcs32;        /* TURE means send binary frames with 32 bit FCS */
extern int Crc32t;     /* Display flag indicating 32 bit CRC being sent */
extern int Crc32;      /* Display flag indicating 32 bit CRC being received */
extern int Znulls;     /* Number of nulls to send at beginning of ZDATA hdr */
extern char Attn[ZATTNLEN+1];  /* Attention string rx sends to tx on err */

extern void zsendline (int c);
extern void zsendline_init (void) LRZSZ_ATTRIB_SECTION(lrzsz_rare);
void zsbhdr (int type, char *hdr);
void zshhdr (int type, char *hdr);
void zsdata (const char *buf, size_t length, int frameend);
void zsda32 (const char *buf, size_t length, int frameend);
int zrdata (char *buf, int length, size_t *received);
int zgethdr (char *hdr, int eflag, size_t *);
void stohdr (size_t pos) LRZSZ_ATTRIB_REGPARM(1);
long rclhdr (char *hdr) LRZSZ_ATTRIB_REGPARM(1);

int tcp_server (char *buf) LRZSZ_ATTRIB_SECTION(lrzsz_rare);
int tcp_connect (char *buf) LRZSZ_ATTRIB_SECTION(lrzsz_rare);
int tcp_accept (int d) LRZSZ_ATTRIB_SECTION(lrzsz_rare);


const char * protname (void) LRZSZ_ATTRIB_SECTION(lrzsz_rare);

#endif
