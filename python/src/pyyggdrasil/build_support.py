"""Shared PEP 517 build-backend machinery for the provider-wheel family.

The consumer repos (loki -> pypddl, tyr -> pytyr, runir -> pyrunir) wrap
scikit-build-core with the same pre-build environment setup and wheel
post-processing. Their thin backend modules instantiate :class:`ProviderBackend`
with package-specific configuration and re-export the hook functions.

pyyggdrasil's own backend stays standalone: it cannot import pyyggdrasil while
building it.
"""

from __future__ import annotations

import base64
import csv
import hashlib
import importlib
import os
import platform
import re
import shutil
import subprocess
import sys
import zipfile
from collections.abc import Callable, Sequence
from pathlib import Path
from tempfile import TemporaryDirectory
from typing import Union

from scikit_build_core import build as scikit_build

# PEP 517 config_settings: keys map to a string or a list of strings.
ConfigSettings = dict[str, Union[str, list[str]]]


def _is_disabled(value: str) -> bool:
    return value.upper() in {"0", "FALSE", "OFF", "NO"}


def _is_native_library(path: Path) -> bool:
    name = path.name
    return ".so" in name or name.endswith(".dylib") or name.endswith(".pyd")


def _strip_args() -> list[str]:
    if platform.system() == "Darwin":
        return ["-x"]
    return ["--strip-debug"]


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


def _repack_wheel(wheel_path: Path, wheel_root: Path) -> None:
    _rewrite_record(wheel_root)
    replacement_path = wheel_path.with_suffix(".tmp")
    with zipfile.ZipFile(replacement_path, "w", compression=zipfile.ZIP_DEFLATED) as wheel:
        for path in sorted(wheel_root.rglob("*")):
            if path.is_file():
                wheel.write(path, path.relative_to(wheel_root).as_posix())
    replacement_path.replace(wheel_path)


def _native_library_dirs(native_prefixes: Sequence[Path]) -> list[Path]:
    result: list[Path] = []
    for prefix in native_prefixes:
        for path in sorted(Path(prefix).glob("lib*")):
            if path.is_dir():
                result.append(path)
    return result


def _prepend_env_paths(name: str, paths: Sequence[Path]) -> None:
    existing = os.environ.get(name, "")
    entries = [str(path) for path in paths]
    if existing:
        entries.append(existing)
    os.environ[name] = os.pathsep.join(entries)


def _prepend_cmake_args(*args: str) -> None:
    existing = os.environ.get("CMAKE_ARGS", "")
    os.environ["CMAKE_ARGS"] = " ".join([*args, existing]).strip()


class ProviderBackend:
    """PEP 517 hooks for a provider wheel that consumes sibling provider wheels.

    Parameters:
        package: the python package the wheel ships (e.g. "pypddl").
        providers: provider packages whose native prefixes feed the build, in
            rpath order (e.g. ("pypddl", "pyyggdrasil") for pytyr). Must
            contain "pyyggdrasil".
        cmake_defines: static -D arguments prepended to CMAKE_ARGS.
        extra_cmake_defines: optional callable returning additional -D
            arguments, evaluated at build time (for env-dependent defines).
        rename_packages: packages whose private-module references are rewritten
            in the wheel's *.pyi stubs (<p>._<p> -> <p>). Defaults to the
            package and its providers.
        jobs_env / strip_env: environment variable names controlling build
            parallelism and wheel stripping.
    """

    def __init__(
        self,
        package: str,
        providers: Sequence[str],
        cmake_defines: Sequence[str] = (),
        extra_cmake_defines: Callable[[], Sequence[str]] | None = None,
        rename_packages: Sequence[str] | None = None,
        jobs_env: str | None = None,
        strip_env: str | None = None,
    ) -> None:
        if "pyyggdrasil" not in providers:
            raise ValueError("providers must include pyyggdrasil")
        self.package = package
        self.providers: tuple[str, ...] = tuple(providers)
        self.cmake_defines: tuple[str, ...] = tuple(cmake_defines)
        self.extra_cmake_defines = extra_cmake_defines
        self.rename_packages: tuple[str, ...] = (
            tuple(rename_packages) if rename_packages is not None else (package, *self.providers)
        )
        self.jobs_env = jobs_env or f"{package.upper()}_JOBS"
        self.strip_env = strip_env or f"{package.upper()}_STRIP_WHEEL"

    # -- pre-build ---------------------------------------------------------

    def _num_jobs(self) -> int:
        raw_jobs = os.environ.get(self.jobs_env, "8")
        try:
            jobs = int(raw_jobs)
        except ValueError as err:
            raise ValueError(f"{self.jobs_env} must be a positive integer") from err
        if jobs <= 0:
            raise ValueError(f"{self.jobs_env} must be a positive integer")
        return jobs

    def _native_prefixes(self) -> list[Path]:
        prefixes: list[Path] = []
        for provider in self.providers:
            module = importlib.import_module(provider)
            native_prefix: Callable[[], str] = module.native_prefix
            prefixes.append(Path(native_prefix()).resolve())
        return prefixes

    def _prepare_native_build(self) -> None:
        native_prefixes = self._native_prefixes()
        native_library_dirs = _native_library_dirs(native_prefixes)
        yggdrasil_prefix = native_prefixes[self.providers.index("pyyggdrasil")]

        os.environ.setdefault("CMAKE_BUILD_PARALLEL_LEVEL", str(self._num_jobs()))
        _prepend_env_paths("LD_LIBRARY_PATH", native_library_dirs)
        _prepend_env_paths("DYLD_LIBRARY_PATH", native_library_dirs)

        defines = [
            f"-DCMAKE_PREFIX_PATH={';'.join(str(prefix) for prefix in native_prefixes)}",
            f"-DYGGDRASIL_NATIVE_PREFIX={yggdrasil_prefix}",
            f"-DPython_EXECUTABLE={sys.executable}",
            *self.cmake_defines,
        ]
        if self.extra_cmake_defines is not None:
            defines.extend(self.extra_cmake_defines())
        _prepend_cmake_args(*defines)

    # -- wheel post-processing ---------------------------------------------

    def _patch_stub_text(self, text: str) -> str:
        for rename_package in self.rename_packages:
            text = text.replace(f"{rename_package}._{rename_package}.", f"{rename_package}.")
            text = text.replace(f"{rename_package}._{rename_package}", rename_package)
        # nanobind stubgen emits bare `os.PathLike` for filesystem::path params; subscript it so
        # strict type checkers do not see PathLike[Unknown].
        text = re.sub(r"os\.PathLike(?!\[)", "os.PathLike[str]", text)
        return text

    def _fix_wheel_stubs(self, wheel_path: str | os.PathLike[str]) -> None:
        wheel_path = Path(wheel_path)
        with TemporaryDirectory(prefix=f"{self.package}-wheel-") as tmp:
            wheel_root = Path(tmp) / "wheel"
            with zipfile.ZipFile(wheel_path) as wheel:
                wheel.extractall(wheel_root)

            package_root = wheel_root / self.package

            def install_stub(path: Path, target: Path) -> None:
                package_dir = target.with_suffix("")
                if package_dir.is_dir():
                    target = package_dir / "__init__.pyi"

                # Handwritten (or otherwise pre-existing) public stubs win over
                # generated ones, matching yggdrasil_patch_python_stubs.
                if target.exists():
                    return

                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text(self._patch_stub_text(path.read_text(encoding="utf-8")), encoding="utf-8")

            private_stub_roots = sorted(
                path
                for path in package_root.glob(f"_{self.package}*")
                if path.is_dir() and (path.name == f"_{self.package}" or path.name.startswith(f"_{self.package}."))
            )
            for private_stub_root in private_stub_roots:
                for path in sorted(private_stub_root.rglob("*.pyi")):
                    install_stub(path, package_root / path.relative_to(private_stub_root))
                shutil.rmtree(private_stub_root)

            for path in sorted(wheel_root.rglob("*.pyi")):
                path.write_text(self._patch_stub_text(path.read_text(encoding="utf-8")), encoding="utf-8")

            _repack_wheel(wheel_path, wheel_root)

    def _strip_wheel_native_libraries(self, wheel_path: str | os.PathLike[str]) -> None:
        if _is_disabled(os.environ.get(self.strip_env, "ON")):
            return

        strip = shutil.which("strip")
        if strip is None:
            return

        wheel_path = Path(wheel_path)
        with TemporaryDirectory(prefix=f"{self.package}-wheel-") as tmp:
            wheel_root = Path(tmp) / "wheel"
            with zipfile.ZipFile(wheel_path) as wheel:
                wheel.extractall(wheel_root)

            for path in wheel_root.rglob("*"):
                if path.is_file() and _is_native_library(path):
                    subprocess.run(
                        [strip, *_strip_args(), str(path)],
                        check=False,
                        stdout=subprocess.DEVNULL,
                        stderr=subprocess.DEVNULL,
                    )

            _repack_wheel(wheel_path, wheel_root)

    # -- PEP 517 hooks -------------------------------------------------------

    def get_requires_for_build_wheel(self, config_settings: ConfigSettings | None = None) -> list[str]:
        return scikit_build.get_requires_for_build_wheel(config_settings)

    def get_requires_for_build_editable(self, config_settings: ConfigSettings | None = None) -> list[str]:
        return scikit_build.get_requires_for_build_editable(config_settings)

    def prepare_metadata_for_build_wheel(self, metadata_directory: str, config_settings: ConfigSettings | None = None) -> str:
        return scikit_build.prepare_metadata_for_build_wheel(metadata_directory, config_settings)

    def build_wheel(
        self,
        wheel_directory: str,
        config_settings: ConfigSettings | None = None,
        metadata_directory: str | None = None,
    ) -> str:
        self._prepare_native_build()
        wheel_filename = scikit_build.build_wheel(wheel_directory, config_settings, metadata_directory)
        wheel_path = Path(wheel_directory) / wheel_filename
        self._fix_wheel_stubs(wheel_path)
        self._strip_wheel_native_libraries(wheel_path)
        return wheel_filename

    def build_editable(
        self,
        wheel_directory: str,
        config_settings: ConfigSettings | None = None,
        metadata_directory: str | None = None,
    ) -> str:
        self._prepare_native_build()
        wheel_filename = scikit_build.build_editable(wheel_directory, config_settings, metadata_directory)
        self._fix_wheel_stubs(Path(wheel_directory) / wheel_filename)
        return wheel_filename

    def build_sdist(self, sdist_directory: str, config_settings: ConfigSettings | None = None) -> str:
        return scikit_build.build_sdist(sdist_directory, config_settings)

    def install_hooks(self, namespace: dict[str, object]) -> None:
        """Export the PEP 517 hook functions into a backend module namespace."""
        namespace["get_requires_for_build_wheel"] = self.get_requires_for_build_wheel
        namespace["get_requires_for_build_editable"] = self.get_requires_for_build_editable
        namespace["prepare_metadata_for_build_wheel"] = self.prepare_metadata_for_build_wheel
        namespace["build_wheel"] = self.build_wheel
        namespace["build_editable"] = self.build_editable
        namespace["build_sdist"] = self.build_sdist
