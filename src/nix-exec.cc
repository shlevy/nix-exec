#include <iostream>

/* Work around nix's config.h */
#undef PACKAGE_NAME
#undef PACKAGE_STRING
#undef PACKAGE_TARNAME
#undef PACKAGE_VERSION
#include <shared.hh>
#include <common-opts.hh>
#include <store-api.hh>
#include <globals.hh>

#include "nix-exec.hh"

static void setup_args(nix::EvalState & state, nix::Value & args, nix::Strings::difference_type arg_count) {
  state.mkList(args, arg_count);
  auto elem = args.listElems();
  auto argv = nixexec_argv + (nixexec_argc - arg_count);
  do {
    *elem = state.allocValue();
    mkStringNoCopy(**elem, *argv);
    ++elem;
    ++argv;
  } while (--arg_count);
}

static void run() {
  nix::initNix();
  nix::initGC();

  auto search_path = nix::Strings{};
  auto arg_count = nix::Strings::difference_type{0};

  nix::parseCmdLine(nixexec_argc, nixexec_argv,
      [&] (nix::Strings::iterator & arg, const nix::Strings::iterator & end) {
    if (*arg == "--help" || *arg == "-h") {
      std::cerr << "Usage: " << nixexec_argv[0] << " FILE ARGS..." << std::endl;
      throw nix::Exit();
    } else if (*arg == "--version") {
      std::cout << nixexec_argv[0] << " " VERSION " (Nix " << nix::nixVersion << ")" << std::endl;
      throw nix::Exit();
    } else if (nix::parseSearchPathArg(arg, end, search_path)) {
      return true;
    }

    if (*arg == "--" && ++arg == end)
      --arg;

    arg_count = 1;
    while (++arg != end)
      arg_count++;

    --arg;

    return true;
  });

  if (arg_count == 0)
    throw nix::UsageError("No file given");

  auto store = nix::openStore();
  auto state = nix::EvalState{search_path, store};

  auto expr_path = nixexec_argv[nixexec_argc - arg_count];

  auto expr = state.parseExprFromFile(nix::lookupFileArg(state, expr_path));

  auto & fn = *state.allocValue();

  state.eval(expr, fn);

  auto top_pos = nix::Pos{state.symbols.create(expr_path), 1, 1};

  state.forceFunction(fn, top_pos);

  auto & fn_args = *state.allocValue();

  state.mkAttrs(fn_args, 2);

  auto & args = *state.allocAttr(fn_args, state.symbols.create("args"));
  setup_args(state, args, arg_count);

  auto & lib = *state.allocAttr(fn_args, state.symbols.create("lib"));
  setup_lib(state, lib);

  fn_args.attrs->sort();

  auto & result = *state.allocValue();
  state.callFunction(fn, fn_args, result, top_pos);

  auto & fn_pos = fn.type == nix::tLambda
    ? fn.lambda.fun->pos
    : top_pos;
  nix::Value v;
  run_io(state, result, fn_pos, v);
}

int main(int argc, char ** argv) {
  nixexec_argc = argc;
  nixexec_argv = argv;
  return nix::handleExceptions(argv[0], run);
}
