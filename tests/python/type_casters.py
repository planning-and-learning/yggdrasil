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
    assert module.concurrent_bit_packed_view() == [1, 2, 3]

    assert module.roundtrip_pair((4, 5)) == (4, 5)
    assert module.roundtrip_pair([-3, 4]) == (-3, 4)
    assert module.roundtrip_optional_pairs([(1, 2), None, (-3, 4)]) == [(1, 2), None, (-3, 4)]
    assert module.roundtrip_optional_pairs([]) == []
    for value in ((), (1,), (1, 2, 3), ("bad", 2), (1, "bad")):
        for function, argument in ((module.roundtrip_pair, value),
                                   (module.roundtrip_optional_pairs, [value])):
            try:
                function(argument)
            except TypeError:
                pass
            else:
                raise AssertionError(f"invalid pair accepted: {value!r}")

    for value in (42, 3.5, -7):
        result = module.roundtrip_variant(value)
        assert type(result) is type(value)
        assert result == value

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
