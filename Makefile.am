AM_CXXFLAGS = $(NIX_CFLAGS) -std=c++11 -I$(srcdir)/include -I$(srcdir)/src \
  -D NIXEXEC_DATA_DIR=\"$(datadir)\" -D NIXEXEC_PREFIX=\"$(prefix)\" \
  -D NIXEXEC_PLUGIN_DIR=\"$(nixexecplugindir)\" -DSHREXT=\"$(SHREXT)\" \
  -Wall -Wextra -O3

pkglib_LTLIBRARIES = libnixexec.la

libnixexec_la_SOURCES = src/nix-exec-lib.cc src/nix-exec.hh

bin_PROGRAMS = nix-exec

nix_exec_SOURCES = src/nix-exec.cc include/nix-exec.h src/nix-exec.hh
nix_exec_LDADD = $(NIX_LIBS) libnixexec.la

include_HEADERS = include/nix-exec.h

nixlibdir = $(datadir)/nix
nodist_nixlib_DATA = nix/unsafe-lib.nix

nixexecplugindir = $(libdir)/nix-exec-plugins
nixexecplugin_LTLIBRARIES = libfetchgit.la libreexec.la

libfetchgit_la_SOURCES = src/fetchgit.cc
libfetchgit_la_CXXFLAGS = $(AM_CXXFLAGS) -D NIXEXEC_LIBEXEC_DIR=\"$(libexecdir)\"

libreexec_la_SOURCES = src/reexec.cc

nodist_libexec_SCRIPTS = scripts/fetchgit.sh

EXTRA_DIST=LICENSE README.md nix/unsafe-lib.nix.in scripts/fetchgit.sh.in

SUFFIXES = .in

.in:
	$(sed) \
		-e 's,[@]pkglibdir[@],$(pkglibdir),g' \
		-e 's,[@]SHREXT[@],'"$(SHREXT)"',g' \
		$< >$@
