AC_INIT([nix-exec], [1.1.0])

LT_INIT([disable-static])

AM_INIT_AUTOMAKE([foreign -Wall subdir-objects dist-xz no-dist-gzip])

AC_CONFIG_SRCDIR([LICENSE])

AC_LANG([C++])

AC_PROG_CXX

PKG_CHECK_MODULES([NIX], [nix-main nix-expr nix-store])

AC_SEARCH_LIBS([dlopen], [dl], [], AC_MSG_ERROR([unable to find the dlopen() function]))

AC_CONFIG_FILES([Makefile])

AC_OUTPUT
