AC_INIT([nix-exec], [3.0.0])

LT_INIT([disable-static])

AM_INIT_AUTOMAKE([foreign -Wall subdir-objects dist-xz no-dist-gzip])

AC_CONFIG_SRCDIR([LICENSE])

AC_LANG([C++])

AC_PROG_CXX

PKG_CHECK_MODULES([NIX], [nix-main nix-expr nix-store])

AC_SEARCH_LIBS([dlopen], [dl], [], AC_MSG_ERROR([unable to find the dlopen() function]))

AC_PATH_PROG([git], git, git)
AC_PATH_PROG([sed], sed, sed)
AC_PATH_PROG([cut], cut, cut)
AC_PATH_PROG([printf], printf, printf)
AC_PATH_PROG([rmdir], rmdir, rmdir)
AC_PATH_PROG([cp], cp, cp)
AC_PATH_PROG([mktemp], mktemp, mktemp)
AC_PATH_PROG([chmod], chmod, chmod)
AC_PATH_PROG([tar], tar, tar)
AC_PATH_PROG([sync], sync, sync)
AC_PATH_PROG([mv], mv, mv)
AC_PATH_PROG([basename], basename, basename)
AC_PATH_PROG([dirname], dirname, dirname)
AC_PATH_PROG([awk], awk, awk)
AC_PATH_PROG([sh], sh, sh)
AC_PATH_PROG([mkdir], mkdir, mkdir)

AC_SUBST([SHREXT], ["$shrext_cmds"])

AC_CONFIG_FILES([Makefile scripts/fetchgit.sh])

AC_OUTPUT
