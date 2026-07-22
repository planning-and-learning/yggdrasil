#!/usr/bin/env python3
import importlib.util
import math
import sys
from pathlib import Path


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: type_casters.py <extension>")

    extension = Path(sys.argv[1]).resolve()
    spec = importlib.util.spec_from_file_location("yggdrasil_type_casters_test", extension)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {extension}")

    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)

    assert module.array_view() == [1, 2, 3]
    assert module.pair_view() == (4, 5)
    assert module.nested_view() == [(6, 7), (8, 9)]

    assert module.empty_interval() is None
    assert module.singleton_interval() == 2.5
    assert module.bounded_interval() == (1.25, 3.5)
    assert module.roundtrip_interval(None) is None
    assert module.roundtrip_interval(2.5) == 2.5
    assert module.roundtrip_interval((2.5, 2.5)) == 2.5
    assert module.roundtrip_interval((1.25, 3.5)) == (1.25, 3.5)
    assert module.roundtrip_interval((3.5, 1.25)) is None
    assert module.roundtrip_interval((math.nan, 1.25)) is None
    assert module.roundtrip_interval((-math.inf, math.inf)) == (-math.inf, math.inf)

    try:
        module.roundtrip_interval((1.25, 2.5, 3.5))
    except TypeError:
        pass
    else:
        raise AssertionError("three bounds must not convert to ClosedInterval")


if __name__ == "__main__":
    main()
