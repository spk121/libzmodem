/*
  canit - cancel zmodem connection 
  Copyright (C) until 1988 Chuck Forsberg (Omen Technology INC)
  Copyright (C) 1994 Matt Porter, Michael D. Black
  Copyright (C) 1996, 1997 Uwe Ohse

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


#include "canit.h"

#include <unistd.h> // for write

/* send cancel string to get the other end to shut up */
void
canit (zreadline_t *zr, int fd)
{
	static char canistr[] =
	{
		24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 0
	};
	zreadline_flushline(zr);
	write(fd,canistr,strlen(canistr));
	if (fd==0)
		write(1,canistr,strlen(canistr));
}

#ifdef CANIT_MAIN
int main()
{
  canit(NULL, 0);
}
#endif
