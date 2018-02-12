#ifndef LIBZMODEM_ZREADLINE_H
#define LIBZMODEM_ZREADLINE_H

#include <stddef.h>

struct zreadline_ {
	char *readline_ptr; /* pointer for removing chars from linbuf */
	int readline_left; /* number of buffered chars left to read */
	size_t readline_readnum;
	int readline_fd;
	char *readline_buffer;
	int no_timeout; 	/* when true, readline does not timeout */
};

typedef struct zreadline_ zreadline_t;

zreadline_t *zreadline_init(int fd, size_t readnum, size_t bufsize, int no_timeout);
void zreadline_flush (zreadline_t *zr);
void zreadline_flushline (zreadline_t *zr);
int zreadline_getc(zreadline_t *zr, int timeout);
void zreadline_canit (zreadline_t *zr, int fd);


#endif
