bin_PROGRAMS= mrz msz
lib_LTLIBRARIES = libzmodem.la
mrz_SOURCES = mrz.c
mrz_LDADD = libzmodem.la
msz_SOURCES = msz.c
msz_LDADD = libzmodem.la
libzmodem_la_SOURCES = \
	crctab.c crctab.h \
	gettext.h \
	log.h log.c \
	lrz.c \
	lsz.c \
	protname.c \
	rbsb.c \
	tcp.c \
	timing.h timing.c \
	zglobal.h \
	zm.c \
	_zmodem.h \
	zreadline.c
pkginclude_HEADERS = zmodem.h

AM_CFLAGS = -Wall -Wextra -Wconversion

include flymake.mk
