extern "C" {
#include "nix-exec.h"
}

namespace nix {
  class EvalState;
  struct Value;
  struct Pos;
}

void run_io(nix::EvalState & state, nix::Value * io_val, const nix::Pos * pos, nix::Value & v);

void setup_unsafe_perform_io(nix::EvalState & state, nix::Value & v);
