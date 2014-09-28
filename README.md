nix-exec
=========

`nix-exec` is a tool to run programs defined in nix expressions. It has
two major goals:

* Provide a common framework for defining libraries for tools that require
  complex interactions with `nix`, such as `hydra` and `nixops` (and arguably
  `nix-env` and `nixos-rebuild`), both to increase ease of development and to
  encourage reusability
* Define a basic minimal bootstrapping environment for `nix`-using tools. For
  example, using `nixops` currently requires you to install `python` and
  several `python` libraries, and `hydra` requires a `perl` web framework. If
  both were rewritten to be `nix-exec` programs, a system containing only
  `nix-exec` and the top-level scripts for `hydra` and `nixops` could
  run either without any futher manual installation.

`nix-exec` is designed to have a minimal interface to keep it usable in as
wide of a context as possible.

Invocation
-----------

    $ nix-exec SCRIPT [ARGS...]

`nix-exec` is meant to be invoked on a `nix` script, with an optional set of
arguments. Any arguments recognized by `nix` passed before the script name
(such as `--verbose`) will be used to initialize `nix`. If the script name
starts with a `-`, the `--` argument can be used to signify the end of
arguments that should be possibly passed to `nix`.

`nix-exec` is designed to be usable in a shebang.

Expression entry point
-----------------------

The top-level script should evaluate to a function of a single argument
that returns a `nix-exec` IO value (see below). The argument will be an
attribute set containing a list `args` of the arguments passed to the script
(including the script name) and an attribute set `lib` containing the IO
monad functions.

IO monad
---------

`nix-exec` provides a [monad][1]
for defining programs which it executes. The `lib` argument contains the monad functions:

* `unit` (AKA `return`) :: a -> m a: Bring a value into the monad
* `map` (AKA `fmap`) :: (a -> b) -> m a -> m b: Apply a function to a monadic
  value
* `join` :: m m a -> m a: 'Flatten' a nested monadic value

For Haskell programmers, note that this is the
['map and join'][2]
definition of a monad, and that the familiar `>>=` can be defined in terms of
`map` and `join`.

dlopen
-------

In addition, the `nix-exec` `lib` argument contains a `dlopen` function to
allow native code to be executed when running the `IO` value. `dlopen` is
a variable-argument function: It always takes a `filename`, `symbol`, and
`arity` argument, and then takes as many arguments as specified in the
`arity` argument and returns a monadic value. For example,
`dlopen ./foo.so "fun" 1 true` is a monadic value while
`dlopen ./bar.so "other_fun" 2 null` is a function from a value to a
monadic value.

When running a monadic value resulting from a call `dlopen`, `nix-exec` will
dynamically load the file at `filename`, load a `nix::PrimOpFun` from the DSO
at symbol `symbol`, and pass the arguments to the `PrimOpFun`. `PrimOpFun` is
defined in `<nix/eval.hh>`.

The `filename` argument can be the result of a derivation, in which case
`nix-exec` will build the derivation before trying to dynamically load it.

Global symbols
--------------

`nix-exec` defines a number of external variables in the C header
`<nix-exec.h>` to introspect the execution environment:

* `nixexec_prefix`: The prefix `nix-exec` is installed into
* `nixexec_data_dir`: The data directory `nix-exec`'s nix expressions
  are installed into
* `nixexec_version_patchlevel`, `nixexec_version_minor`,
  `nixexec_version_major`: The version of `nix-exec` running
* `nixexec_argc`, `nixexec_argv`: The full argument list passed
  to `nix-exec`, including arguments that were passed to `nix`.

In addition, symbols defined in `libnixmain`, `libnixexpr`, and `libnixstore`
are all available.

API stability
--------------

The `nix::PrimOpFun` API is not necessarily stable from version to version of
`nix`. As such, scripts should inspect `builtins.nixVersion` to ensure that
loaded dynamic objects are compatible.

Example
-------

This prints out the arguments passed to it, one per line:

```nix
#/usr/bin/env nix-exec
{ args, lib }: let
  pkgs = import <nixpkgs> {};

  print-args-src = builtins.toFile "print-args.cc" ''
    #include <iostream>
    #include <eval.hh>
    #include <eval-inline.hh>

    extern "C" void print(nix::EvalState & state, const nix::Pos & pos, nix::Value ** args, nix::Value & v) {
      state.forceList(*args[0], pos);
      for (unsigned int index = 0; index < args[0]->list.length; ++index) {
        auto str = state.forceStringNoCtx(*args[0]->list.elems[index], pos);
        std::cout << str << std::endl;
      }
      v.type = nix::tNull;
    }
  '';

  print-args-so = pkgs.runCommand "print-args.so" {} ''
    c++ -shared -fPIC -I${pkgs.nixUnstable}/include/nix -I${pkgs.boehmgc}/include -std=c++11 -O3 ${print-args-src} -o $out
    strip -S $out
  '';

  printArgs = lib.dlopen print-args-so "print" 1;
in printArgs args
```

[1]: http://en.wikipedia.org/wiki/Monad_(functional_programming)
[2]: http://en.wikipedia.org/wiki/Monad_(functional_programming)#fmap_and_join