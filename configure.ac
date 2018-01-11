dnl Process this file with autoconf to produce a configure script.
AC_INIT([lrzsz], [0.12.21-mlg], [mike@lonelycactus.com])
AM_INIT_AUTOMAKE(-Wall foreign)
AC_PROG_CC
AC_CONFIG_HEADERS([config.h])
AC_USE_SYSTEM_EXTENSIONS
AM_PROG_AR

dnl AC_C_CROSS is in AC_PROG_CC since 2.12
AC_PREREQ(2.12)

PACKAGE_VERSION="$PACKAGE-$VERSION"
AC_DEFINE_UNQUOTED(PACKAGE, "$PACKAGE")
AC_DEFINE_UNQUOTED(VERSION, "$VERSION")
AC_DEFINE_UNQUOTED(PACKAGE_VERSION, "$PACKAGE_VERSION")
AC_SUBST(PACKAGE)
AC_SUBST(VERSION)
AC_SUBST(PACKAGE_VERSION)

ALL_LINGUAS="de"

AC_SUBST(PACKAGE)
AC_SUBST(VERSION)

changequote(,)dnl
case $VERSION in
	[0-9]*.[0-9]*.[0-9]*) README_ALPHA="README-alpha";;
	*) README_ALPHA=;;
esac
changequote([, ])dnl

AC_ARG_ENABLE(pubdir,
	[--enable-pubdir=/path   include support for a public writeable directory],
	[if test "$enableval" = "no" ; then 
		: 
	  else 
	    AC_DEFINE([PUBDIR],"$enableval", [A public writeable directory])
	  fi])
dnl Checks for programs.
dnl AC_PROG_INSTALL  included in AM_INIT_AUTOMAKE
dnl AC_PROG_MAKE_SET included in AM_INIT_AUTOMAKE
AC_PROG_RANLIB

dnl Checks for header files.
AC_HEADER_STDC

dnl Checks for typedefs, structures, and compiler characteristics.

dnl Checks for library functions.
AC_CHECK_FUNCS(times)
AC_REPLACE_FUNCS(mktime strdup strtoul strtol strpbrk)
AC_REPLACE_FUNCS(stpcpy strftime vasprintf)

dnl special tests
AC_SUBST(CFLAGS)
AC_SUBST(LDFLAGS)
AC_SUBST(LIBS)

dnl AM_GNU_GETTEXT([no-libtool])

dnl AC_DEFINE_UNQUOTED(LOCALEDIR,"$prefix/$DATADIRNAME")
dnl AC_LINK_FILES($nls_cv_header_libgt, $nls_cv_header_intl)


AC_CONFIG_FILES([Makefile intl/Makefile lib/Makefile testsuite/Makefile \
man/Makefile po/Makefile.in src/Makefile \
src/lrzszbug],
[sed -e "/POTFILES =/r po/POTFILES" po/Makefile.in > po/Makefile; 
test -z "$CONFIG_HEADERS" || echo timestamp > stamp-h])
AC_OUTPUT
