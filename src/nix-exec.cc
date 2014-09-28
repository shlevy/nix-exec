#include <stack>
extern "C" {
#include <dlfcn.h>
}
#include <iostream>

/* Work around nix's config.h */
#undef PACKAGE_NAME
#undef PACKAGE_STRING
#undef PACKAGE_TARNAME
#undef PACKAGE_VERSION
#include <types.hh>
#include <store-api.hh>
#include <nixexpr.hh>
#include <misc.hh>
#include <eval.hh>
#include <eval-inline.hh>
#include <shared.hh>
#include <globals.hh>
#include <common-opts.hh>

extern "C" {
#include "nix-exec.h"
}

/* !!! Copied from primops.cc in nix
 * I wrote it so own the copyright... */

static void realiseContext(const nix::PathSet & context)
{
  auto drvs = nix::PathSet{};
  for (const auto & i : context) {
    auto ctx = nix::Path{};

    if (i.at(0) == '!') {
      auto index = i.find("!", 1);
      ctx = std::string(i, index + 1);
      drvs.insert(ctx + std::string(i, 0, index));
    } else
      ctx = i;

    if (!nix::store->isValidPath(ctx))
      throw nix::EvalError(boost::format("path `%1%' is not valid") % ctx);
  }

  if (!drvs.empty()) {
    /* For performance, prefetch all substitute info. */
    nix::PathSet willBuild, willSubstitute, unknown;
    unsigned long long downloadSize, narSize;
    nix::queryMissing(*nix::store, drvs,
      willBuild, willSubstitute, unknown, downloadSize, narSize);

    nix::store->buildPaths(drvs);
  }
}

static void run_io(nix::EvalState & state, nix::Value * io_val, nix::Pos * pos) {
  using boost::format;
  constexpr char invalid_io_message[] =
    "attempted to run invalid io value (please use nix-exec lib functions!), at %1%";

  auto subtype_sym = state.symbols.create("subtype");
  auto a_sym = state.symbols.create("a");
  auto mma_sym = state.symbols.create("mma");
  auto f_sym = state.symbols.create("f");
  auto ma_sym = state.symbols.create("ma");
  auto filename_sym = state.symbols.create("filename");
  auto symbol_sym = state.symbols.create("symbol");
  auto args_sym = state.symbols.create("args");

  auto fn_stack = std::stack<nix::Value *>{std::stack<nix::Value *>::container_type{nullptr}};

  int m_level = 1;
  while (m_level) {
    state.forceAttrs(*io_val, *pos);

    auto type_attr = io_val->attrs->find(state.sType);
    if (type_attr == io_val->attrs->end())
      throw nix::EvalError(format("attempted to run value of non-io type, at %1%") % pos);
    auto type = state.forceStringNoCtx(*type_attr->value, *type_attr->pos);
    if (type != "io")
      throw nix::EvalError(format("attempted to run value of non-io type `%1%', at %2%")
          % type % pos);

    auto subtype_attr = io_val->attrs->find(subtype_sym);
    if (subtype_attr == io_val->attrs->end())
      throw nix::EvalError(format(invalid_io_message) % pos);
    auto subtype = state.forceStringNoCtx(*subtype_attr->value, *subtype_attr->pos);

    if (subtype == "join") {
      auto mma_attr = io_val->attrs->find(mma_sym);
      if (mma_attr == io_val->attrs->end())
        throw nix::EvalError(format(invalid_io_message) % pos);

      io_val = mma_attr->value;
      pos = mma_attr->pos;
      ++m_level;
      fn_stack.push(nullptr);
    } else if (subtype == "map") {
      auto f_attr = io_val->attrs->find(f_sym);
      if (f_attr == io_val->attrs->end())
        throw nix::EvalError(format(invalid_io_message) % pos);

      auto ma_attr = io_val->attrs->find(ma_sym);
      if (ma_attr == io_val->attrs->end())
        throw nix::EvalError(format(invalid_io_message) % pos);

      state.forceFunction(*f_attr->value, *f_attr->pos);

      fn_stack.push(f_attr->value);

      io_val = ma_attr->value;
      pos = ma_attr->pos;
    } else {
      if (subtype == "unit") {
        auto a_attr = io_val->attrs->find(a_sym);
        if (a_attr == io_val->attrs->end())
          throw nix::EvalError(format(invalid_io_message) % pos);

        io_val = a_attr->value;
        pos = a_attr->pos;
      } else if (subtype == "dlopen") {
        auto filename_attr = io_val->attrs->find(filename_sym);
        if (filename_attr == io_val->attrs->end())
          throw nix::EvalError(format(invalid_io_message) % pos);

        auto ctx = nix::PathSet{};
        auto filename = state.coerceToString(*filename_attr->pos,
            *filename_attr->value, ctx, false, false);
        realiseContext(ctx);

        auto handle = ::dlopen(filename.c_str(), RTLD_LAZY | RTLD_LOCAL);
        if (!handle)
          throw nix::EvalError(format("could not open `%1%': %2%") % filename % ::dlerror());

        auto symbol_attr = io_val->attrs->find(symbol_sym);
        if (symbol_attr == io_val->attrs->end())
          throw nix::EvalError(format(invalid_io_message) % pos);

        auto symbol = state.forceStringNoCtx(*symbol_attr->value, *symbol_attr->pos);

        ::dlerror();
        nix::PrimOpFun fn = (nix::PrimOpFun) ::dlsym(handle, symbol.c_str());
        auto err = ::dlerror();
        if (err)
          throw nix::EvalError(format("could not load symbol `%1%' from `%2%': %3%") % symbol % filename % err);

        auto args_attr = io_val->attrs->find(args_sym);
        if (args_attr == io_val->attrs->end())
          throw nix::EvalError(format(invalid_io_message) % pos);

        state.forceValue(*args_attr->value);
        if (args_attr->value->type != nix::tList)
          throw nix::EvalError(format(invalid_io_message) % pos);

        io_val = state.allocValue();

        fn(state, *pos, args_attr->value->list.elems, *io_val);
      } else {
        throw nix::EvalError(format(invalid_io_message) % pos);
      }

      while (auto fn = fn_stack.top()) {
        auto v = state.allocValue();
        state.callFunction(*fn, *io_val, *v, *pos);
        io_val = v;
        fn_stack.pop();
      }

      fn_stack.pop();

      --m_level;
    }
  }
}

const char * nixexec_prefix = NIXEXEC_PREFIX;
const char * nixexec_data_dir = NIXEXEC_DATA_DIR;

const unsigned int nixexec_version_patchlevel = 0x0;
const unsigned int nixexec_version_minor = 0x0;
const unsigned int nixexec_version_major = 0x1;

int nixexec_argc;
char ** nixexec_argv;

static void setup_args(nix::EvalState & state, nix::Value & args, nix::Strings::difference_type arg_count) {
  state.mkList(args, arg_count);
  auto elem = args.list.elems;
  auto argv = nixexec_argv + (nixexec_argc - arg_count);
  do {
    *elem = state.allocValue();
    mkStringNoCopy(**elem, *argv);
    ++elem;
    ++argv;
  } while (--arg_count);
}

static void setup_lib(nix::EvalState & state, nix::Value & lib) {
  auto expr = state.parseExprFromFile(NIXEXEC_DATA_DIR "/nix/lib.nix");

  state.eval(expr, lib);
}

static void run() {
  nix::initNix();

  auto search_path = nix::Strings{};
  auto arg_count = nix::Strings::difference_type{0};

  nix::parseCmdLine(nixexec_argc, nixexec_argv,
      [&] (nix::Strings::iterator & arg, const nix::Strings::iterator & end) {
    if (*arg == "--help" || *arg == "-h") {
      std::cerr << "Usage: " << nixexec_argv[0] << " FILE ARGS..." << std::endl;
      throw nix::Exit();
    } else if (*arg == "--version") {
      std::cout << nixexec_argv[0] << " " << nixexec_version_major << ".";
      std::cout << nixexec_version_minor << "." << nixexec_version_patchlevel;
      std::cout << " (Nix " << nix::nixVersion << ")" << std::endl;
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

  auto state = nix::EvalState{search_path};

  nix::store = nix::openStore();

  auto expr_path = nixexec_argv[nixexec_argc - arg_count];

  auto expr = state.parseExprFromFile(nix::lookupFileArg(state, expr_path));

  auto fn = state.allocValue();

  state.eval(expr, *fn);

  auto top_pos = nix::Pos{state.symbols.create(expr_path), 1, 1};

  state.forceFunction(*fn, top_pos);

  auto fn_args = state.allocValue();

  state.mkAttrs(*fn_args, 2);

  auto args = state.allocAttr(*fn_args, state.symbols.create("args"));
  setup_args(state, *args, arg_count);

  auto lib = state.allocAttr(*fn_args, state.symbols.create("lib"));
  setup_lib(state, *lib);

  fn_args->attrs->sort();

  auto result = state.allocValue();
  state.callFunction(*fn, *fn_args, *result, top_pos);

  run_io(state, result, &top_pos);
}

int main(int argc, char ** argv) {
  nixexec_argc = argc;
  nixexec_argv = argv;
  return nix::handleExceptions(argv[0], run);
}