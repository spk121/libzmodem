/*
  rbsb.c - terminal handling stuff for lrzsz
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

/*
 *  Rev 05-05-1988
 *  ============== (not quite, but originated there :-). -- uwe
 */
#include "zglobal.h"

#include <stdio.h>
#include <errno.h>
#include "log.h"

#ifdef USE_SGTTY
#  ifdef LLITOUT
long Locmode;		/* Saved "local mode" for 4.x BSD "new driver" */
long Locbit = LLITOUT;	/* Bit SUPPOSED to disable output translations */
#  endif
#endif

#include <sys/ioctl.h>
#include <sys/sysmacros.h>

static struct {
	unsigned baudr;
	speed_t speedcode;
} speeds[] = {
	{110,	B110},
	{300,	B300},
	{600,	B600},
	{1200,	B1200},
	{2400,	B2400},
	{4800,	B4800},
	{9600,	B9600},
	{19200,  B19200},
	{38400,  B38400},
#ifdef B57600
	{57600,  B57600},
#endif
#ifdef B115200
	{115200,  B115200},
#endif
#ifdef B230400
	{230400,  B230400},
#endif
#ifdef B460800
	{460800,  B460800},
#endif
#ifdef EXTA
	{19200,	EXTA},
#endif
#ifdef EXTB
	{38400,	EXTB},
#endif
	{0, 0}
};

static unsigned
getspeed(speed_t code)
{
	int n;

	for (n=0; speeds[n].baudr; ++n)
		if (speeds[n].speedcode == code)
			return speeds[n].baudr;
	return 38400;	/* Assume fifo if ioctl failed */
}

/*
 * return 1 if stdout and stderr are different devices
 *  indicating this program operating with a modem on a
 *  different line
 */
int Fromcu;		/* Were called from cu or yam */
int
from_cu(void)
{
	struct stat a, b;
	dev_t help=makedev(0,0);

	/* in case fstat fails */
	a.st_rdev=b.st_rdev=a.st_dev=b.st_dev=help;

	fstat(1, &a); fstat(2, &b);

	if (major(a.st_rdev) != major(b.st_rdev)
		|| minor(a.st_rdev) != minor(b.st_rdev))
		Fromcu=1;
	else if (major(a.st_dev) != major(b.st_dev)
		|| minor(a.st_dev) != minor(b.st_dev))
		Fromcu=1;
	else
		Fromcu=0;

	return Fromcu;
}




/*
 *  Return non 0 if something to read from io descriptor f
 */
int
rdchk(int fd)
{
	static long lf;

	ioctl(fd, FIONREAD, &lf);
	return ((int) lf);
}


struct termios oldtty, tty;

/*
 * mode(n)
 *  3: save old tty stat, set raw mode with flow control
 *  2: set XON/XOFF for sb/sz with ZMODEM
 *  1: save old tty stat, set raw mode
 *  0: restore original tty mode
 * Returns the output baudrate, or zero on failure
 */
int
io_mode(int fd, int n)
{
	static int did0 = FALSE;
	log_debug("mode:%d", n);

	switch(n) {

	case 2:		/* Un-raw mode used by sz, sb when -g detected */
		if(!did0) {
			did0 = TRUE;
			tcgetattr(fd,&oldtty);
		}
		tty = oldtty;

		tty.c_iflag = BRKINT|IXON;

		tty.c_oflag = 0;	/* Transparent output */

		tty.c_cflag &= ~PARENB;	/* Disable parity */
		tty.c_cflag |= CS8;	/* Set character size = 8 */
		tty.c_lflag =  0;
		tty.c_cc[VINTR] = -1;	/* Interrupt char */
#ifdef _POSIX_VDISABLE
		if (((int) _POSIX_VDISABLE)!=(-1)) {
			tty.c_cc[VQUIT] = _POSIX_VDISABLE;		/* Quit char */
		} else {
			tty.c_cc[VQUIT] = -1;			/* Quit char */
		}
#else
		tty.c_cc[VQUIT] = -1;			/* Quit char */
#endif
		tty.c_cc[VMIN] = 1;
		tty.c_cc[VTIME] = 1;	/* or in this many tenths of seconds */

		tcsetattr(fd,TCSADRAIN,&tty);

		return getspeed(cfgetospeed(&tty));
	case 1:
	case 3:
		if(!did0) {
			did0 = TRUE;
			tcgetattr(fd,&oldtty);
		}
		tty = oldtty;

		tty.c_iflag = IGNBRK;
		if (n==3) /* with flow control */
			tty.c_iflag |= IXOFF;

		 /* No echo, crlf mapping, INTR, QUIT, delays, no erase/kill */
		tty.c_lflag &= ~(ECHO | ICANON | ISIG);
		tty.c_oflag = 0;	/* Transparent output */

		tty.c_cflag &= ~(PARENB);	/* Same baud rate, disable parity */
		/* Set character size = 8 */
		tty.c_cflag &= ~(CSIZE);
		tty.c_cflag |= CS8;
		tty.c_cc[VMIN] = 1; /* This many chars satisfies reads */
		tty.c_cc[VTIME] = 1;	/* or in this many tenths of seconds */
		tcsetattr(fd,TCSADRAIN,&tty);
		return getspeed(cfgetospeed(&tty));
	case 0:
		if(!did0)
			return 0;
		tcdrain (fd); /* wait until everything is sent */
		tcflush (fd,TCIOFLUSH); /* flush input queue */
		tcsetattr (fd,TCSADRAIN,&oldtty);
		tcflow (fd,TCOON); /* restart output */

		return getspeed(cfgetospeed(&tty));
	default:
		return 0;
	}
}

void
sendbrk(int fd)
{
	tcsendbreak(fd,0);
}


/* End of rbsb.c */
