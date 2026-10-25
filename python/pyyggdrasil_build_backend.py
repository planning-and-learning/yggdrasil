# Copyright (C) 2026 Dominik Drexler
# SPDX-License-Identifier: GPL-3.0-or-later

"""PEP 517 build backend of pyyggdrasil: builds the native dependencies, then delegates to scikit-build-core."""

from __future__ import annotations

import base64
import csv
import hashlib
import os
import platform
import shlex
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

from scikit_build_core import build as scikit_build

# PEP 517 config_settings: keys map to a string or a list of strings.
ConfigSettings = dict[str, str | list[str]]


ROOT_DIR = Path(__file__).resolve().parent.parent
YGGDRASIL_BUILD_DIR = ROOT_DIR / "dependencies-build"


def _native_prefix() -> Path:
    return Path(os.environ.get("YGGDRASIL_NATIVE_PREFIX", ROOT_DIR / "dependencies-install")).resolve()


def _build_type() -> str:
    return os.environ.get("YGGDRASIL_BUILD_TYPE", "Release")


def num_jobs() -> int:
    """The build's parallel job count: YGGDRASIL_JOBS, else the CPU count."""
    raw_value = os.environ.get("YGGDRASIL_JOBS", str(os.cpu_count() or 1))
    try:
        jobs = int(raw_value)
    except ValueError as error:
        msg = "YGGDRASIL_JOBS must be a positive integer"
        raise RuntimeError(msg) from error

    if jobs < 1:
        msg = "YGGDRASIL_JOBS must be a positive integer"
        raise RuntimeError(msg)

    return jobs


def is_disabled(value: str) -> bool:
    """Whether an environment switch's value turns its feature off."""
    return value.upper() in {"0", "FALSE", "OFF", "NO"}


def configure_and_install_dependencies() -> None:
    """Build and install the native dependencies (src/) into the native prefix, unless disabled."""
    if is_disabled(os.environ.get("YGGDRASIL_BUILD_NATIVE", "ON")):
        return

    cmake = shutil.which("cmake")
    if cmake is None:
        msg = "cmake is required to build pyyggdrasil"
        raise RuntimeError(msg)

    native_prefix = _native_prefix()
    if (
        "YGGDRASIL_NATIVE_PREFIX" in os.environ
        and native_prefix.exists()
        and (not native_prefix.is_dir() or any(native_prefix.iterdir()))
    ):
        msg = (
            "YGGDRASIL_NATIVE_PREFIX must be empty when YGGDRASIL_BUILD_NATIVE is enabled; "
            "set YGGDRASIL_BUILD_NATIVE=OFF to package an existing prefix"
        )
        raise RuntimeError(msg)

    default_native_prefix = ROOT_DIR / "dependencies-install"
    generated_dirs = [YGGDRASIL_BUILD_DIR]
    if "YGGDRASIL_NATIVE_PREFIX" not in os.environ:
        generated_dirs.append(default_native_prefix)
    for generated_dir in generated_dirs:
        if generated_dir.is_symlink():
            msg = f"refusing to clean symlinked build directory: {generated_dir}"
            raise RuntimeError(msg)
        if generated_dir.exists():
            shutil.rmtree(generated_dir)

    YGGDRASIL_BUILD_DIR.mkdir(parents=True, exist_ok=True)

    cmake_args = [
        cmake,
        "-S",
        str(ROOT_DIR / "src"),
        "-B",
        str(YGGDRASIL_BUILD_DIR),
        f"-DCMAKE_BUILD_TYPE={_build_type()}",
        f"-DCMAKE_INSTALL_PREFIX={native_prefix}",
        "-DCMAKE_INSTALL_LIBDIR=lib",
        f"-DYGGDRASIL_JOBS={num_jobs()}",
        f"-DPython_EXECUTABLE={sys.executable}",
    ]

    subprocess.run(cmake_args, cwd=ROOT_DIR, check=True)
    subprocess.run([cmake, "--build", str(YGGDRASIL_BUILD_DIR), f"-j{num_jobs()}"], cwd=ROOT_DIR, check=True)
    subprocess.run([cmake, "--install", str(YGGDRASIL_BUILD_DIR)], cwd=ROOT_DIR, check=True)


def prepend_cmake_args(*args: str) -> None:
    """Put ``args`` before the arguments already in CMAKE_ARGS."""
    existing = shlex.split(os.environ.get("CMAKE_ARGS", ""))
    os.environ["CMAKE_ARGS"] = shlex.join([*args, *existing])


def _prepare_native_build() -> None:
    configure_and_install_dependencies()
    os.environ.setdefault("CMAKE_BUILD_PARALLEL_LEVEL", str(num_jobs()))
    prepend_cmake_args(
        f"-DYGGDRASIL_NATIVE_PREFIX={_native_prefix()}",
        "-DCMAKE_INSTALL_LIBDIR=lib",
    )


def _is_versioned_shared_object(name: str) -> bool:
    marker = ".so."
    if marker not in name:
        return False

    version = name.rsplit(marker, maxsplit=1)[1]
    return bool(version) and all(component.isdigit() for component in version.split("."))


def should_strip(path: Path) -> bool:
    """Whether ``path`` is a native library whose symbols the wheel strips."""
    name = path.name
    return (
        name.endswith((".so", ".dylib", ".pyd", ".dll"))
        or _is_versioned_shared_object(name)
        or (path.parent.name == "bin" and name in {"hydra_pmi_proxy", "mpiexec"})
    )


def _strip_args() -> list[str]:
    if platform.system() == "Darwin":
        return ["-x"]
    return ["--strip-unneeded"]


def _record_digest(path: Path) -> tuple[str, str]:
    content = path.read_bytes()
    digest = base64.urlsafe_b64encode(hashlib.sha256(content).digest()).rstrip(b"=").decode("ascii")
    return f"sha256={digest}", str(len(content))


def rewrite_record(wheel_root: Path) -> None:
    """Recompute the hashes and sizes in the unpacked wheel's RECORD."""
    record_files = list(wheel_root.glob("*.dist-info/RECORD"))
    if len(record_files) != 1:
        msg = f"expected exactly one RECORD file, found {len(record_files)}"
        raise RuntimeError(msg)

    record_file = record_files[0]
    rows: list[tuple[str, str, str]] = []
    for path in sorted(wheel_root.rglob("*")):
        if not path.is_file():
            continue

        relative_path = path.relative_to(wheel_root).as_posix()
        if path == record_file:
            rows.append((relative_path, "", ""))
        else:
            digest, size = _record_digest(path)
            rows.append((relative_path, digest, size))

    with record_file.open("w", newline="") as out:
        csv.writer(out).writerows(rows)


def strip_wheel_native_libraries(wheel_path: Path) -> None:
    """Strip the native libraries inside the wheel, unless YGGDRASIL_STRIP_WHEEL is off."""
    if is_disabled(os.environ.get("YGGDRASIL_STRIP_WHEEL", "ON")):
        return

    strip = shutil.which("strip")
    if strip is None:
        return

    with tempfile.TemporaryDirectory(prefix="pyyggdrasil-wheel-") as tmp:
        wheel_root = Path(tmp) / "wheel"
        with zipfile.ZipFile(wheel_path) as wheel:
            for info in wheel.infolist():
                path = Path(wheel.extract(info, wheel_root))
                if mode := (info.external_attr >> 16) & 0o777:
                    path.chmod(mode)

        for path in wheel_root.rglob("*"):
            if path.is_file() and should_strip(path):
                subprocess.run(
                    [strip, *_strip_args(), str(path)],
                    check=False,
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                )

        rewrite_record(wheel_root)
        replacement_path = wheel_path.with_suffix(".tmp")
        with zipfile.ZipFile(replacement_path, "w", compression=zipfile.ZIP_DEFLATED) as wheel:
            for path in sorted(wheel_root.rglob("*")):
                if path.is_file():
                    wheel.write(path, path.relative_to(wheel_root).as_posix())
        replacement_path.replace(wheel_path)


def get_requires_for_build_wheel(config_settings: ConfigSettings | None = None) -> list[str]:
    """PEP 517 hook: scikit-build-core's build requirements for a wheel."""
    return scikit_build.get_requires_for_build_wheel(config_settings)


def get_requires_for_build_editable(config_settings: ConfigSettings | None = None) -> list[str]:
    """PEP 517 hook: scikit-build-core's build requirements for an editable wheel."""
    return scikit_build.get_requires_for_build_editable(config_settings)


def prepare_metadata_for_build_wheel(metadata_directory: str, config_settings: ConfigSettings | None = None) -> str:
    """PEP 517 hook: the wheel's metadata, from scikit-build-core."""
    return scikit_build.prepare_metadata_for_build_wheel(metadata_directory, config_settings)


def build_wheel(
    wheel_directory: str,
    config_settings: ConfigSettings | None = None,
    metadata_directory: str | None = None,
) -> str:
    """PEP 517 hook: build the native dependencies, then the wheel."""
    _prepare_native_build()
    wheel_filename = scikit_build.build_wheel(wheel_directory, config_settings, metadata_directory)
    wheel_path = Path(wheel_directory) / wheel_filename
    strip_wheel_native_libraries(wheel_path)
    return wheel_filename


def build_editable(
    wheel_directory: str,
    config_settings: ConfigSettings | None = None,
    metadata_directory: str | None = None,
) -> str:
    """PEP 517 hook: build the native dependencies, then an editable wheel."""
    _prepare_native_build()
    return scikit_build.build_editable(wheel_directory, config_settings, metadata_directory)


def build_sdist(sdist_directory: str, config_settings: ConfigSettings | None = None) -> str:
    """PEP 517 hook: the source distribution, from scikit-build-core."""
    return scikit_build.build_sdist(sdist_directory, config_settings)
