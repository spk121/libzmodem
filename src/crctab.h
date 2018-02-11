#ifndef LIBZMODEM_CRCTAB_H
#define LIBZMODEM_CRCTAB_H

unsigned short updcrc(unsigned short cp, unsigned short crc);
long UPDC32(int b, long c);
// extern unsigned short crctab[256];
// #define updcrc(cp, crc) ( crctab[((crc >> 8) & 255)] ^ (crc << 8) ^ cp)
/* extern long cr3tab[]; */
/* #define UPDC32(b, c) (cr3tab[((int)c ^ b) & 0xff] ^ ((c >> 8) & 0x00FFFFFF)) */

#endif
