bin_PROGRAMS=lrz lsz
lrz_SOURCES=lrz.c timing.c zperr.c zreadline.c crctab.c rbsb.c zm.c protname.c tcp.c canit.c log.c
lsz_SOURCES=lsz.c timing.c zperr.c zreadline.c crctab.c rbsb.c zm.c protname.c tcp.c canit.c log.c
noinst_HEADERS = timing.h zglobal.h zmodem.h gettext.h log.h
datadir = $(prefix)/share
localedir = $(datadir)/locale
DISTCLEAN_FILES=lrzszbug

LDADD = ../lib/libzmodem.a @LIBINTL@
EXTRA_DIST = lrzszbug.in
AM_CPPFLAGS = -I$(srcdir) -I$(top_srcdir)/src -I$(top_srcdir)/lib
AM_CFLAGS = -Wall -Wextra -Wconversion
#DEFS = -DLOCALEDIR=\"$(localedir)\" -DOS=\"@host_os@\" -DCPU=\"@host_cpu@\"
DEFS =  @DEFS@ -DLOCALEDIR=\"$(localedir)\"

install-exec-local:
	rm -f $(DESTDIR)/$(bindir)/`echo lsb | sed -e '$(transform)'`
	ln $(DESTDIR)/$(bindir)/`echo lsz |sed -e '$(transform)'` \
		$(DESTDIR)/$(bindir)/`echo lsb |sed -e '$(transform)'` 
	rm -f $(bindir)/`echo lsx | sed -e '$(transform)'`
	ln $(DESTDIR)/$(bindir)/`echo lsz |sed -e '$(transform)'` \
		$(DESTDIR)/$(bindir)/`echo lsx |sed -e '$(transform)'` 
	rm -f $(bindir)/`echo lrb | sed -e '$(transform)'`
	ln $(DESTDIR)/$(bindir)/`echo lrz |sed -e '$(transform)'` \
		$(DESTDIR)/$(bindir)/`echo lrb |sed -e '$(transform)'` 
	rm -f $(bindir)/`echo lrx | sed -e '$(transform)'`
	ln $(DESTDIR)/$(bindir)/`echo lrz |sed -e '$(transform)'` \
		$(DESTDIR)/$(bindir)/`echo lrx |sed -e '$(transform)'` 
#	rm -f $(bindir)/`echo lrzszbug | sed -e '$(transform)'`
#	ln $(bindir)/`echo lrzszbug |sed -e '$(transform)'` \
#		$(bindir)/`echo lrzszbug |sed -e '$(transform)'` 

uninstall-local:
	rm -f $(DESTDIR)/$(bindir)/`echo lsb |sed -e '$(transform)'` 
	rm -f $(DESTDIR)/$(bindir)/`echo lsx |sed -e '$(transform)'` 
	rm -f $(DESTDIR)/$(bindir)/`echo lrb |sed -e '$(transform)'` 
	rm -f $(DESTDIR)/$(bindir)/`echo lrx |sed -e '$(transform)'` 
#	rm -f $(bindir)/`echo lrzszbug |sed -e '$(transform)'` 
