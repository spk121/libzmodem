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
#include "zreadline.h"
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


/* rbsb.c */
extern int Fromcu;
extern int iofd;

/* rbsb.c */
int from_cu (void) LRZSZ_ATTRIB_SECTION(lrzsz_rare);
int rdchk (int fd);
int io_mode (int fd, int n) LRZSZ_ATTRIB_SECTION(lrzsz_rare);
void sendbrk (int fd);


/* zm.c */

int tcp_server (char *buf) LRZSZ_ATTRIB_SECTION(lrzsz_rare);
int tcp_connect (char *buf) LRZSZ_ATTRIB_SECTION(lrzsz_rare);
int tcp_accept (int d) LRZSZ_ATTRIB_SECTION(lrzsz_rare);


const char * protname (void) LRZSZ_ATTRIB_SECTION(lrzsz_rare);

#endif
