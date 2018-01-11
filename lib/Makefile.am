noinst_LIBRARIES=libzmodem.a

EXTRA_DIST = ansi2knr.1 ansi2knr.c \
	getopt.c getopt1.c \
	strtol.c strtoul.c

libzmodem_a_SOURCES = long-options.c xstrtol.c xstrtoul.c error.c
libzmodem_a_LIBADD  = @LIBOBJS@
libzmodem_a_DEPENDENCIES = @LIBOBJS@

noinst_HEADERS = getopt.h long-options.h xstrtol.h xstrtoul.h error.h

