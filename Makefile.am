bin_PROGRAMS = nix-exec

nix_exec_SOURCES = src/nix-exec.cc include/nix-exec.h
nix_exec_CXXFLAGS = $(NIX_CFLAGS) -std=c++11 -I$(srcdir)/include \
  -D NIXEXEC_DATA_DIR=\"$(datadir)\" -D NIXEXEC_PREFIX=\"$(prefix)\" \
  -Wall -Wextra
nix_exec_LDADD = $(NIX_LIBS)

include_HEADERS = include/nix-exec.h

nixlibdir = $(datadir)/nix
dist_nixlib_DATA = nix/lib.nix

EXTRA_DIST=LICENSE README.md
