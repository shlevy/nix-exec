{
  unit = a: { inherit a; type = "io"; subtype = "unit"; };

  join = mma: { inherit mma; type = "io"; subtype = "join"; };

  map = f: ma: { inherit f ma; type = "io"; subtype = "map"; };

  dlopen = filename: symbol: arity: let
    go = args: arity: if arity > 0
      then arg: go (args ++ [ arg ]) (arity - 1)
      else { inherit filename symbol args; type = "io"; subtype = "dlopen"; };
  in go [] arity;
}
