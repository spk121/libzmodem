dnl Process this file with autoconf to produce a configure script.
AC_INIT([libzmodem], [0.12.21-mlg], [mike@lonelycactus.com])
AC_PREREQ(2.69)
AC_CONFIG_SRCDIR([src/mrz.c])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([-Wall foreign])
AC_USE_SYSTEM_EXTENSIONS
AM_PROG_AR
LT_INIT

dnl AC_C_CROSS is in AC_PROG_CC since 2.12

ALL_LINGUAS="de"

AC_ARG_ENABLE(pubdir,
	[--enable-pubdir=/path include support for a public writeable directory],
	[if test "$enableval" = "no" ; then 
		: 
	  else 
	    AC_DEFINE([PUBDIR],"$enableval", [A public writeable directory])
	  fi])

dnl Checks for programs.
dnl AC_PROG_INSTALL  included in AM_INIT_AUTOMAKE
dnl AC_PROG_MAKE_SET included in AM_INIT_AUTOMAKE
AC_PROG_CC

dnl Checks for header files.

dnl Checks for typedefs, structures, and compiler characteristics.

dnl Checks for library functions.

dnl special tests

dnl AM_GNU_GETTEXT([no-libtool])
dnl AM_GNU_GETTEXT([external],[],[])
dnl AM_GNU_GETTEXT_VERSION([0.19.8])

if test x"$DEJAGNU" = x
then
  DEJAGNU="\$(top_srcdir)/testsuite/global-conf.exp"
fi
AC_SUBST(DEJAGNU)

AC_CONFIG_FILES([Makefile testsuite/Makefile src/Makefile])
AC_OUTPUT
