#ifndef LIBZMODEM_ZM_H
#define LIBZMODEM_ZM_H

#include <stdint.h>
#include <stddef.h>

#include "_zmodem.h"

#define ZCRC_DIFFERS (ERROR+1)
#define ZCRC_EQUAL (ERROR+2)

/* These are the values for the escape sequence table. */
#define ZM_ESCAPE_NEVER ((char) 0)
#define ZM_ESCAPE_ALWAYS ((char) 1)
#define ZM_ESCAPE_AFTER_AMPERSAND ((char) 2)

extern int bytes_per_error;  /* generate one error around every x bytes */

struct zm_ {
	zreadline_t *zr;	/* Buffered, interruptable input. */
	char Rxhdr[4];		/* Received header */
	char Txhdr[4];		/* Transmitted header */
	int rxtimeout;          /* Constant: tenths of seconds to wait for something */
	int znulls;             /* Constant: Number of nulls to send at beginning of ZDATA hdr */
	int eflag;              /* Constant: local display of non zmodem characters */
				/* 0:  no display */
				/* 1:  display printing characters only */
				/* 2:  display all non ZMODEM characters */
	int baudrate;		/* Constant: in bps */
	int zrwindow;		/* RX window size (controls garbage count) */

	int zctlesc;            /* Variable: TRUE means to encode control characters */
	int txfcs32;            /* Variable: TRUE means send binary frames with 32 bit FCS */

	int rxtype;		/* State: type of header received */
	char escape_sequence_table[256]; /* State: conversion chart for zmodem escape sequence encoding */
	char lastsent;		/* State: last byte send */
	int crc32t;             /* State: display flag indicating 32-bit CRC being sent */
	int crc32;              /* State: display flag indicating 32 bit CRC being received */
	int rxframeind;	        /* State: ZBIN, ZBIN32, or ZHEX type of frame received */
	int zmodem_requested;
};

typedef struct zm_ zm_t;

zm_t *zm_init(int fd, size_t readnum, size_t bufsize, int no_timeout,
	      int rxtimeout, int znulls, int eflag, int baudrate, int zctlesc, int zrwindow);
int zm_get_zctlesc(zm_t *zm);
void zm_set_zctlesc(zm_t *zm, int zctlesc);
void zm_escape_sequence_update(zm_t *zm);
void zm_put_escaped_char (zm_t *zm, int c);
void zm_send_binary_header (zm_t *zm, int type);
void zm_send_hex_header (zm_t *zm, int type);
void zm_send_data (zm_t *zm, const char *buf, size_t length, int frameend);
void zm_send_data32 (zm_t *zm, const char *buf, size_t length, int frameend);
void zm_set_header_payload (zm_t *zm, uint32_t val);
void zm_set_header_payload_bytes(zm_t *zm, uint8_t x0, uint8_t x1, uint8_t x2, uint8_t x3);

long zm_reclaim_send_header (zm_t *zm);
long zm_reclaim_receive_header (zm_t *zm);
int zm_receive_data (zm_t *zm, char *buf, int length, size_t *received);
int zm_get_header (zm_t *zm, uint32_t *payload);
void zm_ackbibi (zm_t *zm);
void zm_saybibi(zm_t *zm);
int zm_do_crc_check(zm_t *zm, FILE *f, size_t remote_bytes, size_t check_bytes);

#endif
