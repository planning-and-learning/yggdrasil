import argparse

import pyyggdrasil


def main() -> None:
    parser = argparse.ArgumentParser(
        prog="python -m pyyggdrasil",
        description="Print discovery paths for the bundled native dependencies.",
    )
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument(
        "--prefix",
        action="store_true",
        help="Print the native prefix to put on CMAKE_PREFIX_PATH.",
    )
    group.add_argument(
        "--include-dir",
        action="store_true",
        help="Print the bundled C++ header include directory.",
    )
    group.add_argument(
        "--cmake-dir",
        action="store_true",
        help="Print the directory containing yggdrasilConfig.cmake.",
    )
    group.add_argument(
        "--version",
        action="store_true",
        help="Print the pyyggdrasil version.",
    )
    args = parser.parse_args()

    if args.prefix:
        print(pyyggdrasil.cmake_prefix())
    elif args.include_dir:
        print(pyyggdrasil.include_dir())
    elif args.cmake_dir:
        print(pyyggdrasil.cmake_dir())
    elif args.version:
        print(pyyggdrasil.__version__)


if __name__ == "__main__":
    main()
