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
        zm_store_header(pos) store position data in Txhdr
        long zm_reclaim_header(hdr) recover position offset from header
 */

#include "zglobal.h"

#include <stdio.h>
#include <stdlib.h>
#include "log.h"

/* Globals used by ZMODEM functions */
char Rxhdr[4];		/* Received header */
char Txhdr[4];		/* Transmitted header */
long Txpos;		/* Transmitted file position */
char Attn[ZATTNLEN+1];	/* Attention string rx sends to tx on err */

int bytes_per_error=0;

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
static inline int noxrd7 (zm_t *zm);
static inline int zdlread (zm_t *zm);
static int zdlread2 (zm_t *zm, int) LRZSZ_ATTRIB_REGPARM(1);
static inline int zgeth1 (zm_t *zm);
static void zputhex (int c, char *pos);
static inline int zgethex (zm_t *zm);
static int zm_read_binary_header (zm_t *zm, char *hdr);
static int zm_read_binary_header32 (zm_t *zm, char *hdr);
static int zm_read_hex_header (zm_t *zm, char *hdr);
static int zm_read_data32 (zm_t *zm, char *buf, int length, size_t *);
static void zm_send_binary_header32 (zm_t *zm, char *hdr, int type);
static void zsendline_init (zm_t *zm) LRZSZ_ATTRIB_SECTION(lrzsz_rare);

/* Return a newly allocated state machine for zm primitives. */
zm_t *
zm_init(int rxtimeout, int znulls, int eflag, int baudrate, int turbo_escape, int zctlesc, int zrwindow)
{
	zm_t *zm = (zm_t *) malloc (sizeof (zm_t));
	memset(zm, 0, sizeof(zm_t));
	zm->rxtimeout = rxtimeout;
	zm->znulls = znulls;
	zm->eflag = eflag;
	zm->baudrate = baudrate;
	zm->turbo_escape = turbo_escape;
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
noxrd7(zm_t *zm)
{
	register int c;

	for (;;) {
		if ((c = READLINE_PF(zm->rxtimeout)) < 0)
			return c;
		switch (c &= 0177) {
		case XON:
		case XOFF:
			continue;
		default:
			if (zm->zctlesc && !(c & 0140))
				continue;
		case '\r':
		case '\n':
		case ZDLE:
			return c;
		}
	}
}

static inline int
zgeth1(zm_t *zm)
{
	register int c, n;

	if ((c = noxrd7(zm)) < 0)
		return c;
	n = c - '0';
	if (n > 9)
		n -= ('a' - ':');
	if (n & ~0xF)
		return ERROR;
	if ((c = noxrd7(zm)) < 0)
		return c;
	c -= '0';
	if (c > 9)
		c -= ('a' - ':');
	if (c & ~0xF)
		return ERROR;
	c += (n<<4);
	return c;
}

/* Decode two lower case hex digits into an 8 bit byte value */
static inline int
zgethex(zm_t *zm)
{
	register int c;

	c = zgeth1(zm);
	log_trace("zgethex: %02X", c);
	return c;
}

/*
 * Read a byte, checking for ZMODEM escape encoding
 *  including CAN*5 which represents a quick abort
 */
static inline int
zdlread(zm_t *zm)
{
	int c;
	/* Quick check for non control characters */
	if ((c = READLINE_PF(zm->rxtimeout)) & 0140)
		return c;
	return zdlread2(zm, c);
}
/* no, i don't like gotos. -- uwe */
static int
zdlread2(zm_t *zm, int c)
{
	goto jump_over; /* bad style */

again:
	/* Quick check for non control characters */
	if ((c = READLINE_PF(zm->rxtimeout)) & 0140)
		return c;
jump_over:
	switch (c) {
	case ZDLE:
		break;
	case XON:
	case (XON|0200):
	case XOFF:
	case (XOFF|0200):
		goto again;
	default:
		if (zm->zctlesc && !(c & 0140)) {
			goto again;
		}
		return c;
	}
again2:
	if ((c = READLINE_PF(zm->rxtimeout)) < 0)
		return c;
	if (c == CAN && (c = READLINE_PF(zm->rxtimeout)) < 0)
		return c;
	if (c == CAN && (c = READLINE_PF(zm->rxtimeout)) < 0)
		return c;
	if (c == CAN && (c = READLINE_PF(zm->rxtimeout)) < 0)
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
		return 0177;
	case ZRUB1:
		return 0377;
	case XON:
	case (XON|0200):
	case XOFF:
	case (XOFF|0200):
		goto again2;
	default:
		if (zm->zctlesc && ! (c & 0140)) {
			goto again2;
		}
		if ((c & 0140) ==  0100)
			return (c ^ 0100);
		break;
	}
	log_debug(_("Bad escape sequence %x"), c);
	return ERROR;
}



/*
 * Send character c with ZMODEM escape sequence encoding.
 *  Escape XON, XOFF. Escape CR following @ (Telenet net escape)
 */
inline void
zsendline(zm_t *zm, int c)
{

	switch(zm->zsendline_tab[(unsigned) (c&=0377)])
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
		if ((zm->lastsent & 0177) != '@') {
			putchar(zm->lastsent = c);
		} else {
			putchar(ZDLE);
			c ^= 0100;
			putchar(zm->lastsent = c);
		}
		break;
	}
}

static inline void
zsendline_s(zm_t *zm, const char *s, size_t count)
{
	const char *end=s+count;
	while(s!=end) {
		int last_esc=0;
		const char *t=s;
		while (t!=end) {
			last_esc = zm->zsendline_tab[(unsigned) ((*t) & 0377)];
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
				if ((zm->lastsent & 0177) != '@') {
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
zm_send_binary_header(zm_t *zm, int type, char *hdr)
{
	register unsigned short crc;

	log_trace("zm_send_binary_header: %s %lx", frametypes[type+FTOFFSET], zm_reclaim_header(hdr));
	if (type == ZDATA)
		for (int n = 0; n < zm->znulls; n ++)
			putchar(0);

	putchar(ZPAD);
	putchar(ZDLE);

	zm->crc32t = zm->txfcs32;
	if (zm->crc32t)
		zm_send_binary_header32(zm, hdr, type);
	else {
		putchar(ZBIN);
		zsendline(zm, type);
		crc = updcrc(type, 0);

		for (int n = 0; n < 4; n ++) {
			zsendline(zm, hdr[n]);
			crc = updcrc((0377 & hdr[n]), crc);
		}
		crc = updcrc(0,updcrc(0,crc));
		zsendline(zm, crc>>8);
		zsendline(zm, crc);
	}
	if (type != ZDATA)
		fflush(stdout);
}


/* Send ZMODEM binary header hdr of type type */
static void
zm_send_binary_header32(zm_t *zm, char *hdr, int type)
{
	register unsigned long crc;

	putchar(ZBIN32);
	zsendline(zm, type);
	crc = 0xFFFFFFFFL; crc = UPDC32(type, crc);

	for (int n = 0; n < 4; n++) {
		crc = UPDC32((0377 & hdr[n]), crc);
		zsendline(zm, hdr[n]);
	}
	crc = ~crc;
	for (int n = 0; n < 4; n++) {
		zsendline(zm, (int)crc);
		crc >>= 8;
	}
}

/* Send ZMODEM HEX header hdr of type type */
void
zm_send_hex_header(zm_t *zm, int type, char *hdr)
{
	register unsigned short crc;
	char s[30];
	size_t len;

	log_trace("zm_send_hex_header: %s %lx", frametypes[(type & 0x7f)+FTOFFSET], zm_reclaim_header(hdr));
	s[0]=ZPAD;
	s[1]=ZPAD;
	s[2]=ZDLE;
	s[3]=ZHEX;
	zputhex(type & 0x7f ,s+4);
	len=6;
	zm->crc32t = 0;

	crc = updcrc((type & 0x7f), 0);
	for (int n = 0; n < 4; n++) {
		zputhex(hdr[n], s+len);
		len += 2;
		crc = updcrc((0377 & hdr[n]), crc);
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
		zsendline(zm, buf[i]);
		crc = updcrc((0377 & buf[i]), crc);
	}
	putchar(ZDLE);
	putchar(frameend);
	crc = updcrc(frameend, crc);

	crc = updcrc(0,updcrc(0,crc));
	zsendline(zm, crc>>8);
	zsendline(zm, crc);
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
	zsendline_s(zm, buf,length);
	for (size_t i = 0; i < length; i++) {
		c = buf[i] & 0377;
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
			zsendline(zm, c);
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
		if ((c = zdlread(zm)) & ~0377) {
crcfoo:
			switch (c) {
			case GOTCRCE:
			case GOTCRCG:
			case GOTCRCQ:
			case GOTCRCW:
				{
					d = c;
					c &= 0377;
					crc = updcrc(c, crc);
					if ((c = zdlread(zm)) & ~0377)
						goto crcfoo;
					crc = updcrc(c, crc);
					if ((c = zdlread(zm)) & ~0377)
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
		if ((c = zdlread(zm)) & ~0377) {
crcfoo:
			switch (c) {
			case GOTCRCE:
			case GOTCRCG:
			case GOTCRCQ:
			case GOTCRCW:
				d = c;
				c &= 0377;
				crc = UPDC32(c, crc);
				if ((c = zdlread(zm)) & ~0377)
					goto crcfoo;
				crc = UPDC32(c, crc);
				if ((c = zdlread(zm)) & ~0377)
					goto crcfoo;
				crc = UPDC32(c, crc);
				if ((c = zdlread(zm)) & ~0377)
					goto crcfoo;
				crc = UPDC32(c, crc);
				if ((c = zdlread(zm)) & ~0377)
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
zm_get_header(zm_t *zm, char *hdr, size_t *Rxpos)
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
	switch (c = READLINE_PF(zm->rxtimeout)) {
	case RCDO:
	case TIMEOUT:
		goto fifi;
	case CAN:
gotcan:
		if (--cancount <= 0) {
			c = ZCAN; goto fifi;
		}
		switch (c = READLINE_PF(1)) {
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
			if ((c &= 0177) & 0140)
				log_info("Unknown header character '%c'", c);
			else
				log_info("Unknown header character 0x'%u'", (unsigned char) c);
		}
		goto startover;
	case ZPAD|0200:		/* This is what we want. */
	case ZPAD:		/* This is what we want. */
		break;
	}
	cancount = 5;
splat:
	switch (c = noxrd7(zm)) {
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

	switch (c = noxrd7(zm)) {
	case RCDO:
	case TIMEOUT:
		goto fifi;
	case ZBIN:
		zm->rxframeind = ZBIN;
		zm->crc32 = FALSE;
		c =  zm_read_binary_header(zm, hdr);
		break;
	case ZBIN32:
		zm->crc32 = zm->rxframeind = ZBIN32;
		c =  zm_read_binary_header32(zm, hdr);
		break;
	case ZHEX:
		zm->rxframeind = ZHEX;
		zm->crc32 = FALSE;
		c =  zm_read_hex_header(zm, hdr);
		break;
	case CAN:
		goto gotcan;
	default:
		goto agn2;
	}
	rxpos = hdr[ZP3] & 0377;
	rxpos = (rxpos<<8) + (hdr[ZP2] & 0377);
	rxpos = (rxpos<<8) + (hdr[ZP1] & 0377);
	rxpos = (rxpos<<8) + (hdr[ZP0] & 0377);
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
zm_read_binary_header(zm_t *zm, char *hdr)
{
	register int c;
	register unsigned short crc;

	if ((c = zdlread(zm)) & ~0377)
		return c;
	zm->rxtype = c;
	crc = updcrc(c, 0);

	for (int n = 0; n < 4; n++) {
		if ((c = zdlread(zm)) & ~0377)
			return c;
		crc = updcrc(c, crc);
		hdr[n] = c;
	}
	if ((c = zdlread(zm)) & ~0377)
		return c;
	crc = updcrc(c, crc);
	if ((c = zdlread(zm)) & ~0377)
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
zm_read_binary_header32(zm_t *zm, char *hdr)
{
	register int c;
	register unsigned long crc;

	if ((c = zdlread(zm)) & ~0377)
		return c;
	zm->rxtype = c;
	crc = 0xFFFFFFFFL; crc = UPDC32(c, crc);
#ifdef DEBUGZ
	log_trace("zm_read_binary_header32 c=%X  crc=%lX", c, crc);
#endif

	for (int n = 0; n < 4; n++) {
		if ((c = zdlread(zm)) & ~0377)
			return c;
		crc = UPDC32(c, crc);
		hdr[n] = c;
#ifdef DEBUGZ
		log_trace("zm_read_binary_header32 c=%X  crc=%lX", c, crc);
#endif
	}
	for (int n = 0; n < 4; n ++) {
		if ((c = zdlread(zm)) & ~0377)
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
zm_read_hex_header(zm_t *zm, char *hdr)
{
	register int c;
	register unsigned short crc;

	if ((c = zgethex(zm)) < 0)
		return c;
	zm->rxtype = c;
	crc = updcrc(c, 0);

	for (int n = 0; n < 4; n ++) {
		if ((c = zgethex(zm)) < 0)
			return c;
		crc = updcrc(c, crc);
		hdr[n] = c;
	}
	if ((c = zgethex(zm)) < 0)
		return c;
	crc = updcrc(c, crc);
	if ((c = zgethex(zm)) < 0)
		return c;
	crc = updcrc(c, crc);
	if (crc & 0xFFFF) {
		log_error(badcrc); return ERROR;
	}
	switch ( c = READLINE_PF(1)) {
	case 0215:
		/* **** FALL THRU TO **** */
	case 015:
	 	/* Throw away possible cr/lf */
		READLINE_PF(1);
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
			case (XOFF | 0200):
			case (XON | 0200):
				zm->zsendline_tab[i]=1;
				break;
			case 020: /* ^P */
			case 0220:
				if (zm->turbo_escape)
					zm->zsendline_tab[i]=0;
				else
					zm->zsendline_tab[i]=1;
				break;
			case 015:
			case 0215:
				if (zm->zctlesc)
					zm->zsendline_tab[i]=1;
				else if (!zm->turbo_escape)
					zm->zsendline_tab[i]=2;
				else
					zm->zsendline_tab[i]=0;
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
zm_store_header(size_t pos)
{
	long lpos=(long) pos;
	Txhdr[ZP0] = lpos;
	Txhdr[ZP1] = lpos>>8;
	Txhdr[ZP2] = lpos>>16;
	Txhdr[ZP3] = lpos>>24;
}

/* Recover a long integer from a header */
long
zm_reclaim_header(char *hdr)
{
	long l;

	l = (hdr[ZP3] & 0377);
	l = (l << 8) | (hdr[ZP2] & 0377);
	l = (l << 8) | (hdr[ZP1] & 0377);
	l = (l << 8) | (hdr[ZP0] & 0377);
	return l;
}

/* End of zm.c */
