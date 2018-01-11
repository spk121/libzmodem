dnl AC_REPLACE_GNU_GETOPT
AC_DEFUN([AC_REPLACE_GNU_GETOPT],
[AC_CHECK_FUNC(getopt_long, , [LIBOBJS="$LIBOBJS getopt1.o getopt.o"])
AC_SUBST(LIBOBJS)dnl
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

