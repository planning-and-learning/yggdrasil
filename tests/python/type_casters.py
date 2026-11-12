#!/usr/bin/env python3
# Copyright (C) 2026 Dominik Drexler
# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.util
import math
import sys
from pathlib import Path


def main() -> None:
    if len(sys.argv) != 2:
        msg = "usage: type_casters.py <extension>"
        raise SystemExit(msg)

    extension = Path(sys.argv[1]).resolve()
    spec = importlib.util.spec_from_file_location("yggdrasil_type_casters_test", extension)
    if spec is None or spec.loader is None:
        msg = f"cannot load {extension}"
        raise RuntimeError(msg)

    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)

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
        msg = "three bounds must not convert to ClosedInterval"
        raise AssertionError(msg)


if __name__ == "__main__":
    main()
