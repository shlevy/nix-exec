/* Work around nix's config.h */
#include "run-io.hh"

static void unsafe( nix::EvalState & state
                  , const nix::Pos & pos
                  , nix::Value ** args
                  , nix::Value & v
                  ) {
  run_io(state, args[0], &pos, v);
}

extern "C" void init(nix::EvalState & state, nix::Value & v) {
  setup_unsafe_perform_io(state, v);
}
