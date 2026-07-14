#!/usr/bin/env python3
import importlib.util
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


if __name__ == "__main__":
    main()
