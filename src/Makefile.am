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
	zm.c zm.h \
	_zmodem.h \
	zreadline.c zreadline.h
pkginclude_HEADERS = zmodem.h

# The libtool interface version, CURRENT:REVISION:AGE.  This is not the
# package version and takes no suffix.  For a release: if only the
# source changed, increment REVISION; if interfaces were added, removed
# or changed, increment CURRENT and set REVISION to 0, then increment
# AGE if they were only added, or set AGE to 0 otherwise.
libzmodem_la_LDFLAGS = -version-info 1:0:0

AM_CFLAGS = -Wall -Wextra -Wconversion

include flymake.mk
