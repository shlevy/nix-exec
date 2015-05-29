#include <cerrno>
#include <cstring>
extern "C" {
#include <unistd.h>
}

/* Work around nix's config.h */
#undef PACKAGE_NAME
#undef PACKAGE_STRING
#undef PACKAGE_TARNAME
#undef PACKAGE_VERSION
#include <eval.hh>

#include <nix-exec.hh>

using boost::format;
using nix::Value;

extern "C" void reexec( nix::EvalState & state
                      , const nix::Pos & pos
                      , Value ** args
                      , Value & v
                      ) {
  if (nixexec_argv == NULL) {
    throw nix::Error("cannot reexec within unsafe-perform-io");
  }
  auto ctx = nix::PathSet{};
  auto filename = state.coerceToString( pos
                                      , *args[0]
                                      , ctx
                                      , false
                                      , false
                                      );
  if (filename == nixexec_argv[0]) {
    v.type = nix::tNull;
  } else {
    try {
      nix::realiseContext(ctx);
    } catch (nix::InvalidPathError & e) {
      throw nix::EvalError(format("cannot exec `%1%', since path `%2%' is not valid, at %3%")
        % filename % e.path % pos);
    }
    /* const_cast legal because execvp respects constness */
    nixexec_argv[0] = const_cast<char *>(filename.c_str());
    execvp(nixexec_argv[0], nixexec_argv);
    throw nix::SysError(format("executing `%1%'") % filename);
  }
}
