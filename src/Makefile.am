bin_PROGRAMS= mrz msz
lib_LIBRARIES = libzmodem.a
mrz_SOURCES = mrz.c
mrz_LDADD = libzmodem.a
msz_SOURCES = msz.c
msz_LDADD = libzmodem.a
libzmodem_a_SOURCES = \
	canit.c \
	crctab.c \
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
