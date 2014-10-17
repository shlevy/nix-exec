AM_CXXFLAGS = $(NIX_CFLAGS) -std=c++11 -I$(srcdir)/include -I$(srcdir)/src \
  -D NIXEXEC_DATA_DIR=\"$(datadir)\" -D NIXEXEC_PREFIX=\"$(prefix)\" \
  -Wall -Wextra

pkglib_LTLIBRARIES = librunio.la

librunio_la_SOURCES = src/run-io.cc src/run-io.hh

bin_PROGRAMS = nix-exec

nix_exec_SOURCES = src/nix-exec.cc include/nix-exec.h src/run-io.hh
nix_exec_LDADD = $(NIX_LIBS) librunio.la

include_HEADERS = include/nix-exec.h

nixlibdir = $(datadir)/nix
dist_nixlib_DATA = nix/lib.nix
nodist_nixlib_DATA = nix/unsafe-perform-io.nix

nixplugindir = $(libdir)/nix-plugins
nixplugin_LTLIBRARIES = libunsafeperformio.la
libunsafeperformio_la_SOURCES = src/unsafe-perform-io.cc src/run-io.hh
libunsafeperformio_la_LIBADD = librunio.la

EXTRA_DIST=LICENSE README.md nix/unsafe-perform-io.nix.in

SED = sed

nix/unsafe-perform-io.nix: nix/unsafe-perform-io.nix.in
	$(SED) 's,[@]nixplugindir[@],$(nixplugindir),g' $< >$@
