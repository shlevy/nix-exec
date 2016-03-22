#include <stack>
#include <cerrno>
#include <cstring>
extern "C" {
#include <dlfcn.h>
}

/* Work around nix's config.h */
#undef PACKAGE_NAME
#undef PACKAGE_STRING
#undef PACKAGE_TARNAME
#undef PACKAGE_VERSION
#include <eval.hh>
#include <store-api.hh>
#include <eval-inline.hh>
#include <globals.hh>

#if HAVE_BOEHMGC
#include <gc/gc_cpp.h>
#define NEW new (UseGC)
#else
#define NEW new
#endif

#include "nix-exec.hh"

int nixexec_argc;
char ** nixexec_argv;

using nix::Value;
using nix::Pos;
using nix::EvalState;
using boost::format;
using std::string;

/* In order to achieve a tail-recursive implementation of run_io, we pass a
 * stack of nix function values around internally. The implementation below is
 * the moral equivalent of something like:
 *
 * call x [] = x
 * call x (Func f : fs) = call (f x) fs
 * call x (Run : fs) = run x fs
 *
 * run (Unit a) fs = call a fs
 * run (Map f ma) fs = run ma (Func f : fs)
 * run (Join mma) fs = run mma (Run : fs)
 * run (Dlopen path sym args) fs = run (Unit (runNativeCode path sym args)) fs
 */

struct fn_stack_elem {
  Value * fun;
  const Pos & pos;
  fn_stack_elem(Value * fun, const Pos & pos) : fun(fun), pos(pos) {};
};

typedef std::stack<fn_stack_elem> fn_stack;

class io_value : public nix::ExternalValueBase {
  string showType() const override {
    return "a nix-exec IO value";
  };

  string typeOf() const override {
    return "nix-exec-io";
  };

  public:
  virtual void run(EvalState & state, fn_stack & fns, Value & v) = 0;
};

static io_value & force_io_value( EvalState & state
                                , Value & v
                                , const Pos & pos
                                ) {
  state.forceValue(v);
  io_value * val;
  auto is_io =  v.type == nix::tExternal
             && (val = dynamic_cast<io_value *>(v.external));
  if (!is_io)
    nix::throwTypeError("value is %1% while a nix-exec IO value was expected, at %2%", v, pos);
  return *val;
}

static void apply_fns( EvalState & state
                     , fn_stack & fns
                     , Value * arg
                     , Value & v
                     ) {
  assert(!fns.empty());
  while (true) {
    auto elem = fns.top();
    fns.pop();
    if (elem.fun) {
      if (fns.empty())
        return state.callFunction(*elem.fun, *arg, v, elem.pos);
      auto & res = *state.allocValue();
      state.callFunction(*elem.fun, *arg, res, elem.pos);
      arg = &res;
    } else {
      return force_io_value(state, *arg, elem.pos).run(state, fns, v);
    }
  }
}

class unit_value : public io_value {
  Value & a;

  std::ostream & print(std::ostream & str) const override {
    return str << "nix-exec-lib.unit (" << a << ")";
  };

  void run(EvalState & state, fn_stack & fns, Value & v) override {
    if (fns.empty()) {
      state.forceValue(a);
      v = a;
    } else {
      return apply_fns(state, fns, &a, v);
    }
  };

  size_t valueSize(std::set<const void *> & seen) const override {
    size_t res = sizeof *this;
    if (seen.find(&a) == seen.end()) {
      seen.insert(&a);
      /* Overestimates, since valueSize doesn't take the seen set. Oh well */
      res += nix::valueSize(a);
    }
    return res;
  };

  /* Should we have operator==? There's no general way to write it such that
   * (unit x) == (map id x), so for now no
   */

  public:
  unit_value(Value & a) : a(a) {};
};

class map_value : public io_value {
  Value & f;
  const Pos & pos;
  Value & ma_val;

  std::ostream & print(std::ostream & str) const override {
    return str << "nix-exec-lib.map (" << f << ") (" << ma_val <<")";
  };

  void run(EvalState & state, fn_stack & fns, Value & v) override {
    state.forceFunction(f, pos);
    fns.emplace(&f, pos);
    auto & ma = force_io_value(state, ma_val, pos);
    if (nix::settings.showTrace) {
      try {
        return ma.run(state, fns, v);
      } catch (nix::Error & e) {
        if (f.type == nix::tLambda) {
          e.addPrefix( format("while mapping %1% over %2%, at %3%:\n")
                     % f.lambda.fun->showNamePos()
                     % ma
                     % pos
                     );
        } else {
          auto op = &f;
          while (op->type == nix::tPrimOpApp)
            op = op->primOpApp.left;
          e.addPrefix( format("while mapping primop %1% over %2%, at %3%:\n")
                     % op->primOp->name
                     % ma
                     % pos
                     );
        }
        throw;
      }
    } else
      return ma.run(state, fns, v);
  };

  size_t valueSize(std::set<const void *> & seen) const override {
    size_t res = sizeof *this;
    if (seen.find(&f) == seen.end()) {
      seen.insert(&f);
      res += nix::valueSize(f);
    }
    if (seen.find(&pos) == seen.end()) {
      seen.insert(&pos);
      res += sizeof pos;
    }
    if (seen.find(&ma_val) == seen.end()) {
      seen.insert(&ma_val);
      res += nix::valueSize(ma_val);
    }
    return res;
  };

  public:
  map_value(Value & f, Value & ma_val, const Pos & pos) :
    f(f), pos(pos), ma_val(ma_val) {};
};

class join_value : public io_value {
  const Pos & pos;
  Value & mma_val;

  std::ostream & print(std::ostream & str) const override {
    return str << "nix-exec-lib.join (" << mma_val << ")";
  };

  void run(EvalState & state, fn_stack & fns, Value & v) override {
    auto & mma = force_io_value(state, mma_val, pos);
    fns.emplace(nullptr, pos);
    if (nix::settings.showTrace) {
      try {
        mma.run(state, fns, v);
      } catch (nix::Error & e) {
        e.addPrefix( format("while joining %1%, at %2%:\n")
                   % mma
                   % pos
                   );
        throw;
      }
    } else
      return mma.run(state, fns, v);
  };

  size_t valueSize(std::set<const void *> & seen) const override {
    size_t res = sizeof *this;
    if (seen.find(&mma_val) == seen.end()) {
      seen.insert(&mma_val);
      res += nix::valueSize(mma_val);
    }
    if (seen.find(&pos) == seen.end()) {
      seen.insert(&pos);
      res += sizeof pos;
    }
    return res;
  };

  public:
  join_value(Value & mma_val, const Pos & pos) : pos(pos), mma_val(mma_val) {};
};

class dlopen_value : public io_value {
  Value & filename_val;
  Value & symbol_val;
  Value & args;
  const Pos & pos;

  std::ostream & print(std::ostream & str) const override {
    return str << "nix-exec-lib.dlopen (" << filename_val << ") (" << symbol_val
        << ") (" << args << ")";
  };

  void run(EvalState & state, fn_stack & fns, Value & v) override {
    auto & arg = *state.allocValue();
    {
      auto ctx = nix::PathSet{};
      auto filename = state.coerceToString( pos
                                          , filename_val
                                          , ctx
                                          , false
                                          , false
					  );
      try {
        state.realiseContext(ctx);
      } catch (nix::InvalidPathError & e) {
        throw nix::EvalError(format("cannot dlopen `%1%', since path `%2%' is not valid, at %3%")
          % filename % e.path % pos);
      }

      auto handle = ::dlopen(filename.c_str(), RTLD_LAZY | RTLD_LOCAL);
      if (!handle)
        throw nix::EvalError(format("could not open `%1%': %2%") % filename % ::dlerror());

      auto symbol = state.forceStringNoCtx(symbol_val, pos);
      ::dlerror();
      nix::PrimOpFun fn = (nix::PrimOpFun) ::dlsym(handle, symbol.c_str());
      auto err = ::dlerror();
      if (err)
        throw nix::EvalError(format("could not load symbol `%1%' from `%2%': %3%") % symbol % filename % err);

      state.forceList(args, pos);
      fn(state, pos, args.listElems(), arg);
    }
    if (fns.empty()) {
      v = arg;
    } else {
      return apply_fns(state, fns, &arg, v);
    }
  };

  size_t valueSize(std::set<const void *> & seen) const override {
    auto res = sizeof *this;
    if (seen.find(&filename_val) == seen.end()) {
      seen.insert(&filename_val);
      res += nix::valueSize(filename_val);
    }
    if (seen.find(&symbol_val) == seen.end()) {
      seen.insert(&symbol_val);
      res += nix::valueSize(symbol_val);
    }
    if (seen.find(&args) == seen.end()) {
      seen.insert(&args);
      res += nix::valueSize(args);
    }
    if (seen.find(&pos) == seen.end()) {
      seen.insert(&pos);
      res += sizeof pos;
    }
    return res;
  };

  public:
  dlopen_value( Value & filename_val
              , Value & symbol_val
              , Value & args
              , const Pos & pos
              ) :
    filename_val(filename_val), symbol_val(symbol_val), args(args), pos(pos) {};
};

void run_io(EvalState & state, Value & arg, const Pos & pos, Value & v) {
  fn_stack fns;
  return force_io_value(state, arg, pos).run(state, fns, v);
}

static void unit(EvalState & state, const Pos & pos, Value ** args, Value & v) {
  v.type = nix::tExternal;
  v.external = NEW unit_value(*args[0]);
}

static void join(EvalState & state, const Pos & pos, Value ** args, Value & v) {
  v.type = nix::tExternal;
  v.external = NEW join_value(*args[0], pos);
}

static void map(EvalState & state, const Pos & pos, Value ** args, Value & v) {
  v.type = nix::tExternal;
  v.external = NEW map_value(*args[0], *args[1], pos);
}

static void prim_dlopen( EvalState & state
                       , const Pos & pos
                       , Value ** args
                       , Value & v
                       ) {
  v.type = nix::tExternal;
  v.external = NEW dlopen_value(*args[0], *args[1], *args[2], pos);
}

struct exploded_version {
  nix::NixInt major;
  nix::NixInt minor;
  nix::NixInt patch;
};

enum class acc_tag : char { major, minor, patch };

static constexpr nix::NixInt char_to_digit(char c) {
  return c == '0' ?
    0 :
  (c == '1' ?
    1 :
  (c == '2' ?
    2 :
  (c == '3' ?
    3 :
  (c == '4' ?
    4 :
  (c == '5' ?
    5 :
  (c == '6' ?
    6 :
  (c == '7' ?
    7 :
  (c == '8' ?
    8 :
  (c == '9' ?
    9 :
  throw std::domain_error("invalid char in version string"))))))))));
}

template <size_t N> static constexpr
  exploded_version explode_version_impl( const char(&str)[N]
                                       , size_t off
                                       , exploded_version acc
                                       , acc_tag tag
                                       ) {
  /* God I want c++14 constexpr... */
  return off < (N - 1) ?
    (tag == acc_tag::major ?
      (str[off] == '.' ?
        explode_version_impl( str
                            , off + 1
                            , exploded_version{acc.major, 0, -1}
                            , acc_tag::minor
                            ) :
        explode_version_impl( str
                            , off + 1
                            , exploded_version{ 10 * acc.major + char_to_digit(
                                                  str[off]
                                                )
                                              , -1
                                              , -1
                                              }
                            , tag
                            )
      ) :
    (tag == acc_tag::minor ?
      (str[off] == '.' ?
        explode_version_impl( str
                            , off + 1
                            , exploded_version{acc.major, acc.minor, 0}
                            , acc_tag::patch
                            ) :
        explode_version_impl( str
                            , off + 1
                            , exploded_version{ acc.major
                                              , 10 * acc.minor + char_to_digit(
                                                  str[off]
                                                )
                                              , -1
                                              }
                            , tag
                            )
      ) :
    (tag == acc_tag::patch ?
      (off + 2 == N ?
        exploded_version{ acc.major
                        , acc.minor
                        , 10 * acc.patch + char_to_digit(str[off])
                        } :
        explode_version_impl( str
                            , off + 1
                            , exploded_version{ acc.major
                                              , acc.minor
                                              , 10 * acc.patch + char_to_digit(
                                                  str[off]
                                                )
                                              }
                            , tag
                            )
      ) :
      throw std::domain_error("internal error")
    ))) :
    throw std::out_of_range("not enough dots in version");
}

template <size_t N>
  static constexpr exploded_version explode_version(const char(&str)[N]) {
  return explode_version_impl( str
                             , 0
                             , exploded_version{0, -1, -1}
                             , acc_tag::major
                             );
}

static void setup_version(EvalState & state, Value & v) {
  state.mkAttrs(v, 3);

  constexpr auto version = explode_version(VERSION);
  static_assert(  version.major > 0
               && version.minor != -1
               && version.patch != -1
               , "invalid exploded version"
               );

  auto & major = *state.allocAttr(v, state.symbols.create("major"));
  mkInt(major, version.major);

  auto & minor = *state.allocAttr(v, state.symbols.create("minor"));
  mkInt(minor, version.minor);

  auto & patch = *state.allocAttr(v, state.symbols.create("patchlevel"));
  mkInt(patch, version.patch);

  v.attrs->sort();
}

static void setup_config(EvalState & state, Value & v) {
  state.mkAttrs(v, 3);

  auto & prefix = *state.allocAttr(v, state.symbols.create("prefix"));
  nix::mkPathNoCopy(prefix, NIXEXEC_PREFIX);

  auto & datadir = *state.allocAttr(v, state.symbols.create("datadir"));
  nix::mkPathNoCopy(datadir, NIXEXEC_DATA_DIR);

  auto & version = *state.allocAttr(v, state.symbols.create("version"));
  setup_version(state, version);

  v.attrs->sort();
}

static void unsafe( EvalState & state
                  , const Pos & pos
                  , Value ** args
                  , Value & v
                  ) {
  run_io(state, *args[0], pos, v);
}

static void setup_builtins(EvalState & state, Value & dlopen_prim, Value & v) {
  state.mkAttrs(v, 3);

  auto unsafe_sym = state.symbols.create("unsafe-perform-io");
  auto & unsafe_perform_io = *state.allocAttr(v, unsafe_sym);
  unsafe_perform_io.type = nix::tPrimOp;
  unsafe_perform_io.primOp = NEW nix::PrimOp(unsafe, 1, unsafe_sym);

  auto fetchgit_expr = state.parseExprFromString( "dlopen: spec: dlopen \""
                                                  NIXEXEC_PLUGIN_DIR
                                                  "/libfetchgit"
                                                  SHREXT
                                                  "\" \"fetchgit\" [ spec ]"
                                                , __FILE__
                                                );
  auto & fetchgit_fun = *state.allocValue();
  state.eval(fetchgit_expr, fetchgit_fun);
  auto & fetchgit = *state.allocAttr(v, state.symbols.create("fetchgit"));
  state.callFunction(fetchgit_fun, dlopen_prim, fetchgit, Pos{});

  auto reexec_expr = state.parseExprFromString( "dlopen: path: dlopen \""
                                                 NIXEXEC_PLUGIN_DIR
                                                 "/libreexec"
                                                 SHREXT
                                                 "\" \"reexec\" [ path ]"
                                              , __FILE__
                                              );
  auto & reexec_fun = *state.allocValue();
  state.eval(reexec_expr, reexec_fun);
  auto & reexec = *state.allocAttr(v, state.symbols.create("reexec"));
  state.callFunction(reexec_fun, dlopen_prim, reexec, Pos{});

  v.attrs->sort();
}

extern "C" void setup_lib(EvalState & state, Value & v) {
  state.mkAttrs(v, 6);

  auto unit_sym = state.symbols.create("unit");
  auto & unit_prim = *state.allocAttr(v, unit_sym);
  unit_prim.type = nix::tPrimOp;
  unit_prim.primOp = NEW nix::PrimOp(unit, 1, unit_sym);

  auto join_sym = state.symbols.create("join");
  auto & join_prim = *state.allocAttr(v, join_sym);
  join_prim.type = nix::tPrimOp;
  join_prim.primOp = NEW nix::PrimOp(join, 1, join_sym);

  auto map_sym = state.symbols.create("map");
  auto & map_prim = *state.allocAttr(v, map_sym);
  map_prim.type = nix::tPrimOp;
  map_prim.primOp = NEW nix::PrimOp(map, 2, map_sym);

  auto dlopen_sym = state.symbols.create("dlopen");
  auto & dlopen_prim = *state.allocAttr(v, dlopen_sym);
  dlopen_prim.type = nix::tPrimOp;
  dlopen_prim.primOp = NEW nix::PrimOp(prim_dlopen, 3, dlopen_sym);

  auto & config = *state.allocAttr(v, state.symbols.create("configuration"));
  setup_config(state, config);

  auto & builtins = *state.allocAttr(v, state.symbols.create("builtins"));
  setup_builtins(state, dlopen_prim, builtins);

  v.attrs->sort();
}
