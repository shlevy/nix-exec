extern "C" {
#include "nix-exec.h"
}

namespace nix {
  struct EvalState;
  struct Value;
  struct Pos;
}

void run_io(nix::EvalState & state, nix::Value * io_val, nix::Pos * pos);

void setup_lib(nix::EvalState & state, nix::Value & lib);
