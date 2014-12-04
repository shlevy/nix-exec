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
monad functions and `nix-exec`'s configuration settings.

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
allow native code to be executed when running the `IO` value. `dlopen` takes
three arguments, `filename`, `symbol`, and `args`.

When running a monadic value resulting from a call `dlopen`, `nix-exec` will
dynamically load the file at `filename`, load a `nix::PrimOpFun` from the DSO
at symbol `symbol`, and pass the values in the `args list to the `PrimOpFun`.
`PrimOpFun` is defined in `<nix/eval.hh>`.

The `filename` argument can be the result of a derivation, in which case
`nix-exec` will build the derivation before trying to dynamically load it.

Note that the `PrimOpFun` must return a value that is properly forced, i.e.
not a thunk or an un-called function application.

Configuration settings
-----------------------

The `configuration` attribute in the `nix-exec` `lib` argument is a set
containing the following information about the compile-time configuration
of `nix-exec`:

* `prefix`: The installation prefix
* `datadir`:  The data directory
* `version.major`: The major version number
* `version.minor`: The minor version number
* `version.patchlevel`: The version patchlevel.

unsafe-perform-io
------------------

The `builtins` attribute in the `nix-exec` lib contains contains an
`unsafe-perform-io` attribute that is a function that takes an IO value, runs
it, and returns the produced value. It has largely similar pitfalls to Haskell's
`unsafePerformIO` function.

fetchgit
---------

For bootstrapping purposes, the `builtins` attribute in the `nix-exec` lib
contains a `fetchgit` attribute that is a function that takes a set with the
following arguments:

* `url`: The URL of the repository
* `rev`: The desired revision
* `fetchSubmodules`: Whether to fetch submodules (default `true`)
* `cache-dir`: The directory to cache repos and archives in (default
  `$HOME/.cache/fetchgit`).

When called, `fetchgit` returns an IO value that, when run, checks out
the given revision of the given git repository into a directory and yields a
`path` pointing to that directory.

Global symbols
--------------

`nix-exec` defines a number of external variables in the C header
`<nix-exec.h>` to introspect the execution environment:

* `nixexec_argc`: The number of arguments passed to `nix-exec`
* `nixexec_argv`: A NULL-terminated list of arguments passed to `nix-exec`

In addition, symbols defined in `libnixmain`, `libnixexpr`, and `libnixstore`
are all available.

unsafe-lib.nix
----------------

For cases where the expression author doesn't completely control the invocation
of the evaluator (e.g. `nixops` has no way to specify that it should run
`nix-exec`), `nix-exec` installs `unsafe-lib.nix` in `$(datadir)/nix`. Importing
this file evaluates to the `lib` set passed to normal `nix-exec` programs. This
uses `builtins.importNative` under the hood, so it requires the
`allow-unsafe-native-code-during-evaluation` nix option to be set to true.

Note that when using `unsafe-lib.nix`, `nixexec_argc` will be `0` and
`nixexec_argv` will be `NULL` unless called within an actual `nix-exec`
invocation.

API stability
--------------

The `nix::PrimOpFun` API is not necessarily stable from version to version of
`nix`. As such, scripts should inspect `builtins.nixVersion` to ensure that
loaded dynamic objects are compatible.

Example
-------

This prints out the arguments passed to it, one per line:

```nix
#!/usr/bin/env nix-exec
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

  printArgs = args: lib.dlopen print-args-so "print" [ args ];
in printArgs args
```

[1]: http://en.wikipedia.org/wiki/Monad_(functional_programming)
[2]: http://en.wikipedia.org/wiki/Monad_(functional_programming)#fmap_and_join
