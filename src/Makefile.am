bin_PROGRAMS= mrz msz
lib_LIBRARIES = libzmodem.la
mrz_SOURCES = mrz.c
mrz_LDADD = libzmodem.la
msz_SOURCES = msz.c
msz_LDADD = libzmodem.la
libzmodem_la_SOURCES = \
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
	zmodem.h \
	zreadline.c
pkginclude_HEADERS = rzsz.h

AM_CFLAGS = -Wall -Wextra -Wconversion
