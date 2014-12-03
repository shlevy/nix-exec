extern "C" {
#include "nix-exec.h"
}

namespace nix {
  class EvalState;
  struct Value;
  struct Pos;
}

void run_io( nix::EvalState & state
           , nix::Value & arg
           , const nix::Pos & pos
           , nix::Value & v
           );

extern "C" void setup_lib(nix::EvalState & state, nix::Value & v);
