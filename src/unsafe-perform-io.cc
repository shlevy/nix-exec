/* Work around nix's config.h */
#undef PACKAGE_NAME
#undef PACKAGE_STRING
#undef PACKAGE_TARNAME
#undef PACKAGE_VERSION
#include <eval.hh>

#if HAVE_BOEHMGC
#include <gc/gc_cpp.h>
#define NEW new (UseGC)
#else
#define NEW new
#endif

#include "run-io.hh"

static void unsafe( nix::EvalState & state
                  , const nix::Pos & pos
                  , nix::Value ** args
                  , nix::Value & v
                  ) {
  run_io(state, args[0], &pos, v);
}

extern "C" void init(nix::EvalState & state, nix::Value & v) {
  v.type = nix::tPrimOp;
  v.primOp = NEW nix::PrimOp(unsafe, 1, state.symbols.create("unsafePerformIO"));
}
