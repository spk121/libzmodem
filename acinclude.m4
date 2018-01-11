dnl AC_REPLACE_GNU_GETOPT
AC_DEFUN([AC_REPLACE_GNU_GETOPT],
[AC_CHECK_FUNC(getopt_long, , [LIBOBJS="$LIBOBJS getopt1.o getopt.o"])
AC_SUBST(LIBOBJS)dnl
])

dnl LRZSZ_TYPE_SPEED_T
AC_DEFUN([LRZSZ_TYPE_SPEED_T],[
AC_REQUIRE([AC_HEADER_STDC])dnl
AC_REQUIRE([LRZSZ_HEADERS_TERM_IO])dnl
AC_MSG_CHECKING(for speed_t)
AC_CACHE_VAL(ac_cv_type_speed_t,
[AC_EGREP_CPP(speed_t, [#include <sys/types.h>
#if STDC_HEADERS
#include <stdlib.h>
#include <stddef.h>
#endif
#include <termios.h>
], ac_cv_type_speed_t=yes, ac_cv_type_speed_t=no)])dnl
AC_MSG_RESULT($ac_cv_type_speed_t)
if test $ac_cv_type_speed_t = no; then
  AC_DEFINE([speed_t],long)
fi
])

AC_DEFUN([lrzsz_HEADER_SYS_SELECT],
[AC_CACHE_CHECK([whether sys/time.h and sys/select.h may both be included],
  lrzsz_cv_header_sys_select,
[AC_TRY_COMPILE([#include <sys/types.h>
#include <sys/time.h>
#include <sys/select.h>],
[struct tm *tp;], lrzsz_cv_header_sys_select=yes, lrzsz_cv_header_sys_select=no)])
if test $lrzsz_cv_header_sys_select = no; then
  AC_DEFINE(SYS_TIME_WITHOUT_SYS_SELECT)
fi
])

