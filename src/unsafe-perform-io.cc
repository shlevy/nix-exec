/* Work around nix's config.h */
#include "run-io.hh"

extern "C" void init(nix::EvalState & state, nix::Value & v) {
  setup_unsafe_perform_io(state, v);
}
