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
nodist_nixlib_DATA = nix/unsafe-perform-io.nix nix/lib.nix

nixplugindir = $(libdir)/nix-plugins
nixplugin_LTLIBRARIES = libunsafeperformio.la
libunsafeperformio_la_SOURCES = src/unsafe-perform-io.cc src/run-io.hh
libunsafeperformio_la_LIBADD = librunio.la

EXTRA_DIST=LICENSE README.md nix/unsafe-perform-io.nix.in nix/lib.nix.in

SED = sed

SUFFIXES = .in

.in:
	echo $(PACKAGE_VERSION) | (IFS=. read -r major minor patchlevel; \
	$(SED) \
		-e 's,[@]nixplugindir[@],$(nixplugindir),g' \
		-e 's,[@]prefix[@],$(prefix),g' \
		-e 's,[@]datadir[@],$(datadir),g' \
		-e 's,[@]version_major[@],'"$$major"',g' \
		-e 's,[@]version_minor[@],'"$$minor"',g' \
		-e 's,[@]version_patchlevel[@],'"$$patchlevel"',g' \
		$< >$@)
