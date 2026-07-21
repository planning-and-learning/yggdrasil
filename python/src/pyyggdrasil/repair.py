"""Wheel repair helper for consumers of provider wheels (pyyggdrasil, pypddl, ...).

Wraps auditwheel (Linux) / delocate (macOS) and excludes every shared library
that a provider wheel already ships: those libraries must be resolved at
runtime via the consumer's rpaths into the provider package, not vendored into
each consumer wheel. Vendoring would store per-wheel copies under mangled
SONAMEs, so the dynamic loader would load one runtime per wheel (for nanobind
this reintroduces the reference-leak problem that the shared runtime exists to
solve).

Usage (cibuildwheel repair-wheel-command):
    python -m pyyggdrasil.repair --providers pypddl,pyyggdrasil \
        [--require-archs {delocate_archs}] --dest-dir {dest_dir} {wheel}
"""

import argparse
import importlib
import os
import shutil
import subprocess
import sys
from collections.abc import Callable, Sequence
from pathlib import Path


def provider_library_dirs(provider_names: Sequence[str]) -> list[Path]:
    """Return the native library directories of the given provider packages."""
    library_dirs: list[Path] = []
    for name in provider_names:
        module = importlib.import_module(name)
        native_prefix: Callable[[], str] = module.native_prefix
        prefix = Path(native_prefix())
        library_dirs.extend(path for path in sorted(prefix.glob("lib*")) if path.is_dir())
    return library_dirs


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--providers",
        required=True,
        help="Comma-separated provider package names whose shipped libraries are excluded from vendoring.",
    )
    parser.add_argument("--dest-dir", required=True, help="Output directory for the repaired wheel.")
    parser.add_argument(
        "--require-archs",
        default=None,
        help="macOS only; forwarded to delocate-wheel --require-archs.",
    )
    parser.add_argument("wheel")
    args = parser.parse_args(argv)

    library_dirs = provider_library_dirs(args.providers.split(","))
    env = None

    if sys.platform == "darwin":
        command = [
            "delocate-wheel",
            "--ignore-missing-dependencies",
            "--no-sanitize-rpaths",
            "-w",
            args.dest_dir,
            "-v",
            args.wheel,
        ]
        if args.require_archs:
            command += ["--require-archs", args.require_archs]
        for library_dir in library_dirs:
            for library in sorted(library_dir.glob("lib*.dylib")):
                command += ["--exclude", library.name]
    else:
        # Prefer the console script (always on PATH in manylinux images); fall
        # back to the module when auditwheel is installed in this interpreter.
        auditwheel_script = shutil.which("auditwheel")
        auditwheel = [auditwheel_script] if auditwheel_script else [sys.executable, "-m", "auditwheel"]
        command = [*auditwheel, "repair", "-w", args.dest_dir, args.wheel]
        # Pass every filename in the provider lib dirs (including the SONAME
        # symlinks such as libtbb.so.12), so NEEDED entries stay unmangled.
        for library_dir in library_dirs:
            for library in sorted(library_dir.glob("lib*.so*")):
                command += ["--exclude", library.name]
        env = dict(os.environ)
        env["LD_LIBRARY_PATH"] = os.pathsep.join(
            [*(str(path) for path in library_dirs), env.get("LD_LIBRARY_PATH", "")]
        ).rstrip(os.pathsep)

    print("pyyggdrasil.repair:", " ".join(command), file=sys.stderr)
    return subprocess.call(command, env=env)


if __name__ == "__main__":
    raise SystemExit(main())
