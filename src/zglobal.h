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
#include <locale.h>
#include <unistd.h>
#include <limits.h>

#if ENABLE_NLS
#include "gettext.h"
# define _(Text) gettext (Text)
#else
# define bindtextdomain(Domain, Directory) /* empty */
# define textdomain(Domain) /* empty */
# define _(Text) Text
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

extern const char *program_name;        /* the name by which we were called */
// extern int Verbose;
// extern int errors;
// extern int no_timeout;
// extern int under_rsh;

void bibi (int n);

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

/* rbsb.c */
int from_cu (void) LRZSZ_ATTRIB_SECTION(lrzsz_rare);
int rdchk (int fd);
int io_mode (int fd, int n) LRZSZ_ATTRIB_SECTION(lrzsz_rare);
void sendbrk (int fd);
void purgeline (int fd);
void canit (int fd);


/* crctab.c */
extern unsigned short crctab[256];
#define updcrc(cp, crc) ( crctab[((crc >> 8) & 255)] ^ (crc << 8) ^ cp)
extern long cr3tab[];
#define UPDC32(b, c) (cr3tab[((int)c ^ b) & 0xff] ^ ((c >> 8) & 0x00FFFFFF))

/* zm.c */
#include "zmodem.h"
extern int bytes_per_error;  /* generate one error around every x bytes */

/* Globals used by ZMODEM functions */
extern char Rxhdr[4];      /* Received header */
extern char Txhdr[4];      /* Transmitted header */
// extern long Txpos;     /* Transmitted file position */
// extern char Attn[ZATTNLEN+1];  /* Attention string rx sends to tx on err */

struct zm_ {
	int rxtimeout;          /* Constant: tenths of seconds to wait for something */
	int znulls;             /* Constant: Number of nulls to send at beginning of ZDATA hdr */
	int eflag;              /* Constant: local display of non zmodem characters */
				/* 0:  no display */
				/* 1:  display printing characters only */
				/* 2:  display all non ZMODEM characters */
	int baudrate;		/* Constant: in bps */
	int turbo_escape;       /* Constant: TRUE means quit quickly */
	int zrwindow;		/* RX window size (controls garbage count) */

	int zctlesc;            /* Variable: TRUE means to encode control characters */
	int txfcs32;            /* Variable: TRUE means send binary frames with 32 bit FCS */

	int rxtype;		/* State: type of header received */
	enum zm_type_enum protocol; /* State: x, y, or z-modem */
	char zsendline_tab[256]; /* State: conversion chart for zmodem escape sequence encoding */
	char lastsent;		/* State: last byte send */
	int crc32t;             /* State: display flag indicating 32-bit CRC being sent */
	int crc32;              /* State: display flag indicating 32 bit CRC being received */
	int rxframeind;	        /* State: ZBIN, ZBIN32, or ZHEX type of frame received */
	int zmodem_requested;
};

typedef struct zm_ zm_t;

zm_t *zm_init(enum zm_type_enum protocol, int rxtimeout, int znulls, int eflag, int baudrate, int turbo_escape, int zctlesc, int zrwindow);
int zm_get_zctlesc(zm_t *zm);
void zm_set_zctlesc(zm_t *zm, int zctlesc);
void zm_update_table(zm_t *zm);
extern void zsendline (zm_t *zm, int c);
void zm_send_binary_header (zm_t *zm, int type, char *hdr);
void zm_send_hex_header (zm_t *zm, int type, char *hdr);
void zm_send_data (zm_t *zm, const char *buf, size_t length, int frameend);
void zm_send_data32 (zm_t *zm, const char *buf, size_t length, int frameend);
int zm_receive_data (zm_t *zm, char *buf, int length, size_t *received);
int zm_get_header (zm_t *zm, char *hdr, size_t *);
void zm_store_header (size_t pos) LRZSZ_ATTRIB_REGPARM(1);
long zm_reclaim_header (char *hdr) LRZSZ_ATTRIB_REGPARM(1);

int tcp_server (char *buf) LRZSZ_ATTRIB_SECTION(lrzsz_rare);
int tcp_connect (char *buf) LRZSZ_ATTRIB_SECTION(lrzsz_rare);
int tcp_accept (int d) LRZSZ_ATTRIB_SECTION(lrzsz_rare);


const char * protname (void) LRZSZ_ATTRIB_SECTION(lrzsz_rare);

#endif
