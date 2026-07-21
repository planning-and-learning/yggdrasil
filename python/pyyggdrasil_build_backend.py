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
from collections.abc import Callable
from pathlib import Path
from typing import Union

from scikit_build_core import build as scikit_build

# PEP 517 config_settings: keys map to a string or a list of strings.
ConfigSettings = dict[str, Union[str, list[str]]]


ROOT_DIR = Path(__file__).resolve().parent.parent
YGGDRASIL_BUILD_DIR = ROOT_DIR / "dependencies-build"
DEFAULT_BUILD_JOBS = 8


def _native_prefix() -> Path:
    return Path(os.environ.get("YGGDRASIL_NATIVE_PREFIX", ROOT_DIR / "dependencies-install")).resolve()


def _build_type() -> str:
    return os.environ.get("YGGDRASIL_BUILD_TYPE", "Release")


def _num_jobs() -> int:
    raw_value = os.environ.get("YGGDRASIL_JOBS")
    if raw_value is None:
        return DEFAULT_BUILD_JOBS

    try:
        jobs = int(raw_value)
    except ValueError as error:
        raise RuntimeError("YGGDRASIL_JOBS must be a positive integer") from error

    if jobs < 1:
        raise RuntimeError("YGGDRASIL_JOBS must be a positive integer")

    return jobs


def _is_disabled(value: str) -> bool:
    return value.upper() in {"0", "FALSE", "OFF", "NO"}


def _configure_and_install_dependencies() -> None:
    if _is_disabled(os.environ.get("YGGDRASIL_BUILD_NATIVE", "ON")):
        return

    cmake = shutil.which("cmake")
    if cmake is None:
        raise RuntimeError("cmake is required to build pyyggdrasil")

    YGGDRASIL_BUILD_DIR.mkdir(parents=True, exist_ok=True)

    cmake_args = [
        cmake,
        "-S",
        str(ROOT_DIR / "src"),
        "-B",
        str(YGGDRASIL_BUILD_DIR),
        f"-DCMAKE_BUILD_TYPE={_build_type()}",
        f"-DCMAKE_INSTALL_PREFIX={_native_prefix()}",
        "-DCMAKE_INSTALL_LIBDIR=lib",
        f"-DPython_EXECUTABLE={sys.executable}",
    ]

    subprocess.run(cmake_args, cwd=ROOT_DIR, check=True)
    subprocess.run([cmake, "--build", str(YGGDRASIL_BUILD_DIR), f"-j{_num_jobs()}"], cwd=ROOT_DIR, check=True)
    subprocess.run([cmake, "--install", str(YGGDRASIL_BUILD_DIR)], cwd=ROOT_DIR, check=True)


def _prepend_cmake_args(*args: str) -> None:
    existing = shlex.split(os.environ.get("CMAKE_ARGS", ""))
    os.environ["CMAKE_ARGS"] = shlex.join([*args, *existing])


def _prepare_native_build() -> None:
    _configure_and_install_dependencies()
    os.environ.setdefault("CMAKE_BUILD_PARALLEL_LEVEL", str(_num_jobs()))
    _prepend_cmake_args(
        f"-DYGGDRASIL_NATIVE_PREFIX={_native_prefix()}",
        "-DCMAKE_INSTALL_LIBDIR=lib",
    )


def _is_versioned_shared_object(name: str) -> bool:
    marker = ".so."
    if marker not in name:
        return False

    version = name.rsplit(marker, maxsplit=1)[1]
    return bool(version) and all(component.isdigit() for component in version.split("."))


def _is_native_library(path: Path) -> bool:
    name = path.name
    return (
        name.endswith(".so")
        or _is_versioned_shared_object(name)
        or name.endswith(".dylib")
        or name.endswith(".pyd")
        or name.endswith(".dll")
    )


def _strip_args() -> list[str]:
    if platform.system() == "Darwin":
        return ["-x"]
    return ["--strip-unneeded"]


def _rewrite_wheel(wheel_path: Path, mutator: Callable[[Path], None]) -> None:
    with tempfile.TemporaryDirectory(prefix="pyyggdrasil-wheel-") as tmp:
        wheel_root = Path(tmp) / "wheel"
        with zipfile.ZipFile(wheel_path) as wheel:
            wheel.extractall(wheel_root)

        mutator(wheel_root)
        _rewrite_record(wheel_root)

        replacement_path = wheel_path.with_suffix(".tmp")
        with zipfile.ZipFile(replacement_path, "w", compression=zipfile.ZIP_DEFLATED) as wheel:
            for path in sorted(wheel_root.rglob("*")):
                if path.is_file():
                    wheel.write(path, path.relative_to(wheel_root).as_posix())

        replacement_path.replace(wheel_path)


def _record_digest(path: Path) -> tuple[str, str]:
    content = path.read_bytes()
    digest = base64.urlsafe_b64encode(hashlib.sha256(content).digest()).rstrip(b"=").decode("ascii")
    return f"sha256={digest}", str(len(content))


def _rewrite_record(wheel_root: Path) -> None:
    record_files = list(wheel_root.glob("*.dist-info/RECORD"))
    if len(record_files) != 1:
        raise RuntimeError(f"expected exactly one RECORD file, found {len(record_files)}")

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


def _strip_wheel_native_libraries(wheel_path: Path) -> None:
    if _is_disabled(os.environ.get("YGGDRASIL_STRIP_WHEEL", "ON")):
        return

    strip = shutil.which("strip")
    if strip is None:
        return

    def strip_native_libraries(wheel_root: Path) -> None:
        for path in wheel_root.rglob("*"):
            if path.is_file() and _is_native_library(path):
                subprocess.run(
                    [strip, *_strip_args(), str(path)],
                    check=False,
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                )

    _rewrite_wheel(wheel_path, strip_native_libraries)


def get_requires_for_build_wheel(config_settings: ConfigSettings | None = None) -> list[str]:
    return scikit_build.get_requires_for_build_wheel(config_settings)


def get_requires_for_build_editable(config_settings: ConfigSettings | None = None) -> list[str]:
    return scikit_build.get_requires_for_build_editable(config_settings)


def prepare_metadata_for_build_wheel(metadata_directory: str, config_settings: ConfigSettings | None = None) -> str:
    return scikit_build.prepare_metadata_for_build_wheel(metadata_directory, config_settings)


def build_wheel(
    wheel_directory: str,
    config_settings: ConfigSettings | None = None,
    metadata_directory: str | None = None,
) -> str:
    _prepare_native_build()
    wheel_filename = scikit_build.build_wheel(wheel_directory, config_settings, metadata_directory)
    wheel_path = Path(wheel_directory) / wheel_filename
    _strip_wheel_native_libraries(wheel_path)
    return wheel_filename


def build_editable(
    wheel_directory: str,
    config_settings: ConfigSettings | None = None,
    metadata_directory: str | None = None,
) -> str:
    _prepare_native_build()
    return scikit_build.build_editable(wheel_directory, config_settings, metadata_directory)


def build_sdist(sdist_directory: str, config_settings: ConfigSettings | None = None) -> str:
    return scikit_build.build_sdist(sdist_directory)
