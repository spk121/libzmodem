/*
  zm.c - zmodem protocol handling lowlevelstuff
  Copyright (C) until 1998 Chuck Forsberg (OMEN Technology Inc)
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

  Entry point Functions:
        zm_send_binary_header(type, hdr) send binary header
        zm_send_hex_header(type, hdr) send hex header
        zm_get_header(hdr) receive header - binary or hex
        zm_send_data(buf, len, frameend) send data
        zm_receive_data(buf, len, bytes_received) receive data
        zm_set_send_header(pos) store position data in Txhdr
        long zm_reclaim_header() recover position offset from header
 */

#include "zglobal.h"

#include <stdio.h>
#include <stdlib.h>
#include "log.h"
#include "crctab.h"
#include "zm.h"

/* Globals used by ZMODEM functions */
char Rxhdr[4];		/* Received header */
char Txhdr[4];		/* Transmitted header */
long Txpos;		/* Transmitted file position */
char Attn[ZATTNLEN+1];	/* Attention string rx sends to tx on err */

int bytes_per_error=0;

#define ISPRINT(x) ((unsigned)(x) & 0x60u)

static const char *frametypes[] = {
	"Carrier Lost",		/* -3 */
	"TIMEOUT",		/* -2 */
	"ERROR",		/* -1 */
#define FTOFFSET 3
	"ZRQINIT",
	"ZRINIT",
	"ZSINIT",
	"ZACK",
	"ZFILE",
	"ZSKIP",
	"ZNAK",
	"ZABORT",
	"ZFIN",
	"ZRPOS",
	"ZDATA",
	"ZEOF",
	"ZFERR",
	"ZCRC",
	"ZCHALLENGE",
	"ZCOMPL",
	"ZCAN",
	"ZFREECNT",
	"ZCOMMAND",
	"ZSTDERR",
	"xxxxx"
#define FRTYPES 22	/* Total number of frame types in this array */
			/*  not including psuedo negative entries */
};

#define badcrc _("Bad CRC")
/* static char *badcrc = "Bad CRC"; */
static int zm_get_ascii_char (zm_t *zm);
static int zm_get_escaped_char (zm_t *zm);
static int zm_get_escaped_char_internal (zm_t *zm, int);
static int zm_get_hex_encoded_byte (zm_t *zm);
static void zputhex (int c, char *pos);
static int zm_read_binary_header (zm_t *zm);
static int zm_read_binary_header32 (zm_t *zm);
static int zm_read_hex_header (zm_t *zm);
static int zm_read_data32 (zm_t *zm, char *buf, int length, size_t *);
static void zm_send_binary_header32 (zm_t *zm, int type);
static void zsendline_init (zm_t *zm);
static void zm_put_escaped_string(zm_t *zm, char *str, size_t len);


/* Return a newly allocated state machine for zm primitives. */
zm_t *
zm_init(int fd, size_t readnum, size_t bufsize, int no_timeout,
	int rxtimeout, int znulls, int eflag, int baudrate, int zctlesc, int zrwindow)
{
	zm_t *zm = (zm_t *) malloc (sizeof (zm_t));
	memset(zm, 0, sizeof(zm_t));
	zm->zr = zreadline_init(fd, readnum, bufsize, no_timeout);
	zm->rxtimeout = rxtimeout;
	zm->znulls = znulls;
	zm->eflag = eflag;
	zm->baudrate = baudrate;
	zm->zctlesc = zctlesc;
	zm->zrwindow = zrwindow;
	zsendline_init(zm);
	return zm;
}

int
zm_get_zctlesc(zm_t *zm)
{
	return zm->zctlesc;
}

void
zm_set_zctlesc(zm_t *zm, int x)
{
	zm->zctlesc = x;
}

void
zm_update_table(zm_t *zm)
{
	zsendline_init(zm);
}
/*
 * Read a character from the modem line with timeout.
 *  Eat parity, XON and XOFF characters.
 */
static inline int
zm_get_ascii_char(zm_t *zm)
{
	register int c;

	for (;;) {
		if ((c = zreadline_getc(zm->zr, zm->rxtimeout)) < 0)
			return c;
		c &= 0x7F;
		switch (c) {
		case XON:
		case XOFF:
			continue;
		default:
			if (zm->zctlesc && (c < ' '))
				continue;
		case '\r':
		case '\n':
		case ZDLE:
			return c;
		}
	}
}

static int
zm_get_hex_encoded_byte(zm_t *zm)
{
	register int c, n;

	if ((c = zm_get_ascii_char(zm)) < 0)
		return c;
	n = c - '0';
	if (n > 9)
		n -= ('a' - ':');
	if (n & ~0xF)
		return ERROR;
	if ((c = zm_get_ascii_char(zm)) < 0)
		return c;
	c -= '0';
	if (c > 9)
		c -= ('a' - ':');
	if (c & ~0xF)
		return ERROR;
	c += (n<<4);
	log_trace("zm_get_hex_encoded_byte: %02X", c);
	return c;
}

/*
 * Read a byte, checking for ZMODEM escape encoding
 *  including CAN*5 which represents a quick abort
 */
static inline int
zm_get_escaped_char(zm_t *zm)
{
	int c = zreadline_getc(zm->zr, zm->rxtimeout);

	/* Quick check for non control characters */
	if (ISPRINT(c))
		return c;
	return zm_get_escaped_char_internal(zm, c);
}
/* no, i don't like gotos. -- uwe */
static int
zm_get_escaped_char_internal(zm_t *zm, int c)
{
	goto jump_over; /* bad style */

again:
	/* Quick check for non control characters */
	c = zreadline_getc(zm->zr, zm->rxtimeout);
	if (ISPRINT(c))
		return c;
jump_over:
	switch (c) {
	case ZDLE:
		break;
	case XON:
	case (XON|0x80):
	case XOFF:
	case (XOFF|0x80):
		goto again;
	default:
		if (zm->zctlesc && !ISPRINT(c)) {
			goto again;
		}
		return c;
	}
again2:
	/* We only end up here if the previous char was ZDLE. */
	if ((c = zreadline_getc(zm->zr, zm->rxtimeout)) < 0)
		return c;
	if (c == CAN && (c = zreadline_getc(zm->zr, zm->rxtimeout)) < 0)
		return c;
	if (c == CAN && (c = zreadline_getc(zm->zr, zm->rxtimeout)) < 0)
		return c;
	if (c == CAN && (c = zreadline_getc(zm->zr, zm->rxtimeout)) < 0)
		return c;
	switch (c) {
	case CAN:
		return GOTCAN;
	case ZCRCE:
	case ZCRCG:
	case ZCRCQ:
	case ZCRCW:
		return (c | GOTOR);
	case ZRUB0:
		return 0x7F;
	case ZRUB1:
		return 0xFF;
	case XON:
	case (XON|0x80):
	case XOFF:
	case (XOFF|0x80):
		goto again2;
	default:
		if (zm->zctlesc && ! ISPRINT(c)) {
			goto again2;
		}
		/* What is this logic?  This seems like it is trying
		 * to be an downcase operation?  If so, it is
		 * wrong. --MLG */
		if ((c & 0x60) ==  0x40)
			return (c ^ 0x40);
		break;
	}
	log_debug(_("Bad escape sequence %x"), c);
	return ERROR;
}



/*
 * Send character c with ZMODEM escape sequence encoding.
 *  Escape XON, XOFF. Escape CR following @ (Telenet net escape)
 */
void
zm_put_escaped_char(zm_t *zm, int c)
{

	switch(zm->zsendline_tab[(unsigned) (c&=0xFF)])
	{
	case 0:
		putchar(zm->lastsent = c);
		break;
	case 1:
		putchar(ZDLE);
		c ^= 0100;
		putchar(zm->lastsent = c);
		break;
	case 2:
		if ((zm->lastsent & 0x7F) != '@') {
			putchar(zm->lastsent = c);
		} else {
			putchar(ZDLE);
			c ^= 0100;
			putchar(zm->lastsent = c);
		}
		break;
	}
}

static void
zm_put_escaped_string (zm_t *zm, const char *s, size_t count)
{
	const char *end=s+count;
	while(s!=end) {
		int last_esc=0;
		const char *t=s;
		while (t!=end) {
			last_esc = zm->zsendline_tab[(unsigned) ((*t) & 0xFF)];
			if (last_esc)
				break;
			t++;
		}
		if (t!=s) {
			fwrite(s,(size_t)(t-s),1,stdout);
			zm->lastsent=t[-1];
			s=t;
		}
		if (last_esc) {
			int c=*s;
			switch(last_esc) {
			case 0:
				putchar(zm->lastsent = c);
				break;
			case 1:
				putchar(ZDLE);
				c ^= 0100;
				putchar(zm->lastsent = c);
				break;
			case 2:
				if ((zm->lastsent & 0x7F) != '@') {
					putchar(zm->lastsent = c);
				} else {
					putchar(ZDLE);
					c ^= 0100;
					putchar(zm->lastsent = c);
				}
				break;
			}
			s++;
		}
	}
}


/* Send ZMODEM binary header hdr of type type */
void
zm_send_binary_header(zm_t *zm, int type)
{
	register unsigned short crc;

	log_trace("zm_send_binary_header: %s %lx", frametypes[type+FTOFFSET], zm_reclaim_send_header(zm));
	if (type == ZDATA)
		for (int n = 0; n < zm->znulls; n ++)
			putchar(0);

	putchar(ZPAD);
	putchar(ZDLE);

	zm->crc32t = zm->txfcs32;
	if (zm->crc32t)
		zm_send_binary_header32(zm, type);
	else {
		putchar(ZBIN);
		zm_put_escaped_char(zm, type);
		crc = updcrc(type, 0);

		for (int n = 0; n < 4; n ++) {
			zm_put_escaped_char(zm, zm->Txhdr[n]);
			crc = updcrc((0xFF & zm->Txhdr[n]), crc);
		}
		crc = updcrc(0,updcrc(0,crc));
		zm_put_escaped_char(zm, crc>>8);
		zm_put_escaped_char(zm, crc);
	}
	if (type != ZDATA)
		fflush(stdout);
}


/* Send ZMODEM binary header hdr of type type */
static void
zm_send_binary_header32(zm_t *zm, int type)
{
	register unsigned long crc;

	putchar(ZBIN32);
	zm_put_escaped_char(zm, type);
	crc = 0xFFFFFFFFL; crc = UPDC32(type, crc);

	for (int n = 0; n < 4; n++) {
		crc = UPDC32((0xFF & zm->Txhdr[n]), crc);
		zm_put_escaped_char(zm, zm->Txhdr[n]);
	}
	crc = ~crc;
	for (int n = 0; n < 4; n++) {
		zm_put_escaped_char(zm, (int)crc);
		crc >>= 8;
	}
}

/* Send ZMODEM HEX header hdr of type type */
void
zm_send_hex_header(zm_t *zm, int type)
{
	register unsigned short crc;
	char s[30];
	size_t len;

	log_trace("zm_send_hex_header: %s %lx", frametypes[(type & 0x7f)+FTOFFSET], zm_reclaim_send_header(zm));
	s[0]=ZPAD;
	s[1]=ZPAD;
	s[2]=ZDLE;
	s[3]=ZHEX;
	zputhex(type & 0x7f ,s+4);
	len=6;
	zm->crc32t = 0;

	crc = updcrc((type & 0x7f), 0);
	for (int n = 0; n < 4; n++) {
		zputhex(zm->Txhdr[n], s+len);
		len += 2;
		crc = updcrc((0xFF & zm->Txhdr[n]), crc);
	}
	crc = updcrc(0,updcrc(0,crc));
	zputhex(crc>>8,s+len);
	zputhex(crc,s+len+2);
	len+=4;

	/* Make it printable on remote machine */
	s[len++]=015;
	s[len++]=0212;
	/*
	 * Uncork the remote in case a fake XOFF has stopped data flow
	 */
	if (type != ZFIN && type != ZACK)
	{
		s[len++]=021;
	}
	fflush(stdout);
	write(1,s,len);
}

/*
 * Send binary array buf of length length, with ending ZDLE sequence frameend
 */
static const char *Zendnames[] = { "ZCRCE", "ZCRCG", "ZCRCQ", "ZCRCW"};
void
zm_send_data(zm_t *zm, const char *buf, size_t length, int frameend)
{
	register unsigned short crc;

	log_trace("zm_send_data: %lu %s", (unsigned long) length,
		Zendnames[(frameend-ZCRCE)&3]);
	crc = 0;
	for (size_t i = 0; i < length; i++) {
		zm_put_escaped_char(zm, buf[i]);
		crc = updcrc((0xFF & buf[i]), crc);
	}
	putchar(ZDLE);
	putchar(frameend);
	crc = updcrc(frameend, crc);

	crc = updcrc(0,updcrc(0,crc));
	zm_put_escaped_char(zm, crc>>8);
	zm_put_escaped_char(zm, crc);
	if (frameend == ZCRCW) {
		putchar(XON);
		fflush(stdout);
	}
}

void
zm_send_data32(zm_t *zm, const char *buf, size_t length, int frameend)
{
	int c;
	unsigned long crc;
	log_trace("zsdat32: %zu %s", length, Zendnames[(frameend-ZCRCE)&3]);

	crc = 0xFFFFFFFFL;
	zm_put_escaped_string(zm, buf, length);
	for (size_t i = 0; i < length; i++) {
		c = buf[i] & 0xFF;
		crc = UPDC32(c, crc);
	}
	putchar(ZDLE);
	putchar(frameend);
	crc = UPDC32(frameend, crc);

	crc = ~crc;
	for (int i = 0; i < 4; i ++) {
		c=(int) crc;
		if (c & 0140)
			putchar(zm->lastsent = c);
		else
			zm_put_escaped_char(zm, c);
		crc >>= 8;
	}
	if (frameend == ZCRCW) {
		putchar(XON);
		fflush(stdout);
	}
}

#if __GNUC__ < 2 || (__GNUC__ == 2 && __GNUC_MINOR__ <= 4)
#  undef DEBUG_BLOCKSIZE
#endif

#ifdef DEBUG_BLOCKSIZE
struct debug_blocksize {
	int size;
	long count;
};
struct debug_blocksize blocksizes[]={
	{32,0},
	{64,0},
	{128,0},
	{256,0},
	{512,0},
	{1024,0},
	{2048,0},
	{4096,0},
	{8192,0},
	{0,0}
};
static inline void
count_blk(int size)
{
	for (int i=0; blocksizes[i].size; i++) {
		if (blocksizes[i].size==size) {
			blocksizes[i].count++;
			return;
		}
	}
	blocksizes[i].count++;
}

#define COUNT_BLK(x) count_blk(x)
#else
#define COUNT_BLK(x)
#endif

/*
 * Receive array buf of max length with ending ZDLE sequence
 *  and CRC.  Returns the ending character or error code.
 *  NB: On errors may store length+1 bytes!
 */
int
zm_receive_data(zm_t *zm, char *buf, int length, size_t *bytes_received)
{
	register int c;
	register unsigned short crc;
	register int d;

	*bytes_received=0;
	if (zm->rxframeind == ZBIN32)
		return zm_read_data32(zm, buf, length, bytes_received);

	crc = 0;
	for (int i = 0; i < length + 1; i ++) {
		if ((c = zm_get_escaped_char(zm)) & ~0xFF) {
crcfoo:
			switch (c) {
			case GOTCRCE:
			case GOTCRCG:
			case GOTCRCQ:
			case GOTCRCW:
				{
					d = c;
					c &= 0xFF;
					crc = updcrc(c, crc);
					if ((c = zm_get_escaped_char(zm)) & ~0xFF)
						goto crcfoo;
					crc = updcrc(c, crc);
					if ((c = zm_get_escaped_char(zm)) & ~0xFF)
						goto crcfoo;
					crc = updcrc(c, crc);
					if (crc & 0xFFFF) {
						log_error(badcrc);
						return ERROR;
					}
					*bytes_received = i;
					COUNT_BLK(*bytes_received);
					log_trace("zm_receive_data: %lu  %s", (unsigned long) (*bytes_received),
							Zendnames[(d-GOTCRCE)&3]);
					return d;
				}
			case GOTCAN:
				log_error(_("Sender Canceled"));
				return ZCAN;
			case TIMEOUT:
				log_error(_("TIMEOUT"));
				return c;
			default:
				log_error(_("Bad data subpacket"));
				return c;
			}
		}
		buf[i] = c;
		crc = updcrc(c, crc);
	}
	log_error(_("Data subpacket too long"));
	return ERROR;
}

static int
zm_read_data32(zm_t *zm, char *buf, int length, size_t *bytes_received)
{
	register int c;
	register unsigned long crc;
	register int d;

	crc = 0xFFFFFFFFL;
	for (int i = 0; i < length + 1; i ++) {
		if ((c = zm_get_escaped_char(zm)) & ~0xFF) {
crcfoo:
			switch (c) {
			case GOTCRCE:
			case GOTCRCG:
			case GOTCRCQ:
			case GOTCRCW:
				d = c;
				c &= 0xFF;
				crc = UPDC32(c, crc);
				if ((c = zm_get_escaped_char(zm)) & ~0xFF)
					goto crcfoo;
				crc = UPDC32(c, crc);
				if ((c = zm_get_escaped_char(zm)) & ~0xFF)
					goto crcfoo;
				crc = UPDC32(c, crc);
				if ((c = zm_get_escaped_char(zm)) & ~0xFF)
					goto crcfoo;
				crc = UPDC32(c, crc);
				if ((c = zm_get_escaped_char(zm)) & ~0xFF)
					goto crcfoo;
				crc = UPDC32(c, crc);
				if (crc != 0xDEBB20E3) {
					log_error(badcrc);
					return ERROR;
				}
				*bytes_received = i;
				COUNT_BLK(*bytes_received);
				log_trace("zm_read_data32: %lu %s", (unsigned long) *bytes_received,
					Zendnames[(d-GOTCRCE)&3]);
				return d;
			case GOTCAN:
				log_error(_("Sender Canceled"));
				return ZCAN;
			case TIMEOUT:
				log_error(_("TIMEOUT"));
				return c;
			default:
				log_error(_("Bad data subpacket"));
				return c;
			}
		}
		buf[i] = c;
		crc = UPDC32(c, crc);
	}
	log_error(_("Data subpacket too long"));
	return ERROR;
}

/*
 * Read a ZMODEM header to hdr, either binary or hex.
 *  eflag controls local display of non zmodem characters:
 *	0:  no display
 *	1:  display printing characters only
 *	2:  display all non ZMODEM characters
 *  On success, set Zmodem to 1, set Rxpos and return type of header.
 *   Otherwise return negative on error.
 *   Return ERROR instantly if ZCRCW sequence, for fast error recovery.
 */
int
zm_get_header(zm_t *zm, size_t *Rxpos)
{
	register int c, cancount;
	unsigned int max_garbage; /* Max bytes before start of frame */
	size_t rxpos=0; /* keep gcc happy */

	max_garbage = zm->zrwindow + zm->baudrate;
	zm->rxframeind = zm->rxtype = 0;

startover:
	cancount = 5;
again:
	/* Return immediate ERROR if ZCRCW sequence seen */
	switch (c = zreadline_getc(zm->zr, zm->rxtimeout)) {
	case RCDO:
	case TIMEOUT:
		goto fifi;
	case CAN:
gotcan:
		if (--cancount <= 0) {
			c = ZCAN; goto fifi;
		}
		switch (c = zreadline_getc(zm->zr, 1)) {
		case TIMEOUT:
			goto again;
		case ZCRCW:
			c = ERROR;
		/* **** FALL THRU TO **** */
		case RCDO:
			goto fifi;
		default:
			break;
		case CAN:
			if (--cancount <= 0) {
				c = ZCAN; goto fifi;
			}
			goto again;
		}
	/* **** FALL THRU TO **** */
	default:
agn2:
		if ( --max_garbage == 0) {
			log_error(_("Garbage count exceeded"));
			return(ERROR);
		}
		if (zm->eflag) {
			if ((c &= 0x7F) & 0140)
				log_info("Unknown header character '%c'", c);
			else
				log_info("Unknown header character 0x'%u'", (unsigned char) c);
		}
		goto startover;
	case ZPAD|0x80:		/* This is what we want. */
	case ZPAD:		/* This is what we want. */
		break;
	}
	cancount = 5;
splat:
	switch (c = zm_get_ascii_char(zm)) {
	case ZPAD:
		goto splat;
	case RCDO:
	case TIMEOUT:
		goto fifi;
	default:
		goto agn2;
	case ZDLE:		/* This is what we want. */
		break;
	}

	switch (c = zm_get_ascii_char(zm)) {
	case RCDO:
	case TIMEOUT:
		goto fifi;
	case ZBIN:
		zm->rxframeind = ZBIN;
		zm->crc32 = FALSE;
		c =  zm_read_binary_header(zm);
		break;
	case ZBIN32:
		zm->crc32 = zm->rxframeind = ZBIN32;
		c =  zm_read_binary_header32(zm);
		break;
	case ZHEX:
		zm->rxframeind = ZHEX;
		zm->crc32 = FALSE;
		c =  zm_read_hex_header(zm);
		break;
	case CAN:
		goto gotcan;
	default:
		goto agn2;
	}
	rxpos = zm->Rxhdr[ZP3] & 0xFF;
	rxpos = (rxpos<<8) + (zm->Rxhdr[ZP2] & 0xFF);
	rxpos = (rxpos<<8) + (zm->Rxhdr[ZP1] & 0xFF);
	rxpos = (rxpos<<8) + (zm->Rxhdr[ZP0] & 0xFF);
fifi:
	switch (c) {
	case GOTCAN:
		c = ZCAN;
	/* **** FALL THRU TO **** */
	case ZNAK:
	case ZCAN:
	case ERROR:
	case TIMEOUT:
	case RCDO:
		log_error(_("Got %s"), frametypes[c+FTOFFSET]);
	/* **** FALL THRU TO **** */
	default:
		if (c >= -3 && c <= FRTYPES)
			log_trace("zm_get_header: %s %lx", frametypes[c+FTOFFSET], (unsigned long) rxpos);
		else
			log_trace("zm_get_header: %d %lx", c, (unsigned long) rxpos);
	}
	if (Rxpos)
		*Rxpos=rxpos;
	return c;
}

/* Receive a binary style header (type and position) */
static int
zm_read_binary_header(zm_t *zm)
{
	register int c;
	register unsigned short crc;

	if ((c = zm_get_escaped_char(zm)) & ~0xFF)
		return c;
	zm->rxtype = c;
	crc = updcrc(c, 0);

	for (int n = 0; n < 4; n++) {
		if ((c = zm_get_escaped_char(zm)) & ~0xFF)
			return c;
		crc = updcrc(c, crc);
		zm->Rxhdr[n] = c;
	}
	if ((c = zm_get_escaped_char(zm)) & ~0xFF)
		return c;
	crc = updcrc(c, crc);
	if ((c = zm_get_escaped_char(zm)) & ~0xFF)
		return c;
	crc = updcrc(c, crc);
	if (crc & 0xFFFF) {
		log_error(badcrc);
		return ERROR;
	}
	zm->zmodem_requested=TRUE;
	return zm->rxtype;
}

/* Receive a binary style header (type and position) with 32 bit FCS */
static int
zm_read_binary_header32(zm_t *zm)
{
	register int c;
	register unsigned long crc;

	if ((c = zm_get_escaped_char(zm)) & ~0xFF)
		return c;
	zm->rxtype = c;
	crc = 0xFFFFFFFFL; crc = UPDC32(c, crc);
#ifdef DEBUGZ
	log_trace("zm_read_binary_header32 c=%X  crc=%lX", c, crc);
#endif

	for (int n = 0; n < 4; n++) {
		if ((c = zm_get_escaped_char(zm)) & ~0xFF)
			return c;
		crc = UPDC32(c, crc);
		zm->Rxhdr[n] = c;
#ifdef DEBUGZ
		log_trace("zm_read_binary_header32 c=%X  crc=%lX", c, crc);
#endif
	}
	for (int n = 0; n < 4; n ++) {
		if ((c = zm_get_escaped_char(zm)) & ~0xFF)
			return c;
		crc = UPDC32(c, crc);
#ifdef DEBUGZ
		log_trace("zm_read_binary_header32 c=%X  crc=%lX", c, crc);
#endif
	}
	if (crc != 0xDEBB20E3) {
		log_error(badcrc);
		return ERROR;
	}
	zm->zmodem_requested=TRUE;
	return zm->rxtype;
}


/* Receive a hex style header (type and position) */
static int
zm_read_hex_header(zm_t *zm)
{
	register int c;
	register unsigned short crc;

	if ((c = zm_get_hex_encoded_byte(zm)) < 0)
		return c;
	zm->rxtype = c;
	crc = updcrc(c, 0);

	for (int n = 0; n < 4; n ++) {
		if ((c = zm_get_hex_encoded_byte(zm)) < 0)
			return c;
		crc = updcrc(c, crc);
		zm->Rxhdr[n] = c;
	}
	if ((c = zm_get_hex_encoded_byte(zm)) < 0)
		return c;
	crc = updcrc(c, crc);
	if ((c = zm_get_hex_encoded_byte(zm)) < 0)
		return c;
	crc = updcrc(c, crc);
	if (crc & 0xFFFF) {
		log_error(badcrc); return ERROR;
	}
	switch ( c = zreadline_getc(zm->zr, 1)) {
	case 0215:
		/* **** FALL THRU TO **** */
	case 015:
	 	/* Throw away possible cr/lf */
		zreadline_getc(zm->zr, 1);
		break;
	}
	zm->zmodem_requested=TRUE;
	return zm->rxtype;
}

/* Write a byte as two hex digits */
static void
zputhex(int c, char *pos)
{
	static char	digits[]	= "0123456789abcdef";

	log_trace("zputhex: %02X", c);
	pos[0]=digits[(c&0xF0)>>4];
	pos[1]=digits[c&0x0F];
}

void
zsendline_init(zm_t *zm)
{
	for (int i=0; i<256; i++) {
		if (i & 0140)
			zm->zsendline_tab[i]=0;
		else {
			switch(i)
			{
			case ZDLE:
			case XOFF: /* ^Q */
			case XON: /* ^S */
			case (XOFF | 0x80):
			case (XON | 0x80):
				zm->zsendline_tab[i]=1;
				break;
			case 020: /* ^P */
			case 0220:
				zm->zsendline_tab[i]=1;
				break;
			case 015:
			case 0215:
				if (zm->zctlesc)
					zm->zsendline_tab[i]=1;
				else
					zm->zsendline_tab[i]=2;
				break;
			default:
				if (zm->zctlesc)
					zm->zsendline_tab[i]=1;
				else
					zm->zsendline_tab[i]=0;
			}
		}
	}
}



/* Store pos in Txhdr */
void
zm_set_send_header(zm_t *zm, size_t pos)
{
	long lpos=(long) pos;
	zm->Txhdr[ZP0] = lpos;
	zm->Txhdr[ZP1] = lpos>>8;
	zm->Txhdr[ZP2] = lpos>>16;
	zm->Txhdr[ZP3] = lpos>>24;
}


/* Recover a long integer from a header */
long
zm_reclaim_send_header(zm_t *zm)
{
	long l;

	l = (zm->Txhdr[ZP3] & 0xFF);
	l = (l << 8) | (zm->Txhdr[ZP2] & 0xFF);
	l = (l << 8) | (zm->Txhdr[ZP1] & 0xFF);
	l = (l << 8) | (zm->Txhdr[ZP0] & 0xFF);
	return l;
}

/* Recover a long integer from a header */
long
zm_reclaim_receive_header(zm_t *zm)
{
	long l;

	l = (zm->Rxhdr[ZP3] & 0xFF);
	l = (l << 8) | (zm->Rxhdr[ZP2] & 0xFF);
	l = (l << 8) | (zm->Rxhdr[ZP1] & 0xFF);
	l = (l << 8) | (zm->Rxhdr[ZP0] & 0xFF);
	return l;
}



/*
 * Ack a ZFIN packet, let byegones be byegones
 */
void
zm_ackbibi(zm_t *zm)
{
	int n;

	log_debug("ackbibi:");
	zm_set_send_header(zm, 0L);
	for (n=3; --n>=0; ) {
		zreadline_flushline(zm->zr);
		zm_send_hex_header(zm, ZFIN);
		switch (zreadline_getc(zm->zr,100)) {
		case 'O':
			zreadline_getc(zm->zr,1);	/* Discard 2nd 'O' */
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

/* Say "bibi" to the receiver, try to do it cleanly */
void
zm_saybibi(zm_t *zm)
{
	for (;;) {
		zm_set_send_header(zm, 0L);		/* CAF Was zm_send_binary_header - minor change */

		/* Spec 8.3: "The sender closes the session with a
		 * ZFIN header.  The receiver acknowledges this with
		 * its own ZFIN header."  */
		zm_send_hex_header(zm, ZFIN);	/*  to make debugging easier */
		switch (zm_get_header(zm, NULL)) {
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

/*
 * do ZCRC-Check for open file f.
 * check at most check_bytes bytes (crash recovery). if 0 -> whole file.
 * remote file size is remote_bytes.
 */
int
zm_do_crc_check(zm_t *zm, FILE *f, size_t remote_bytes, size_t check_bytes)
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
		zm_set_send_header(zm, check_bytes);
		zm_send_hex_header(zm, ZCRC);
		while(t2<3) {
			size_t tmp;
			c = zm_get_header(zm, &tmp);
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

/* End of zm.c */
