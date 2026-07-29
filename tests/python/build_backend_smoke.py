#!/usr/bin/env python3
from __future__ import annotations

import base64
import csv
import hashlib
import importlib.util
import os
import shlex
import sys
import tempfile
import types
import zipfile
from collections.abc import Callable
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]


class BackendModule(types.ModuleType):
    _num_jobs: Callable[[], int]
    _prepend_cmake_args: Callable[[str, str], None]
    _is_disabled: Callable[[str], bool]
    _should_strip: Callable[[Path], bool]
    _rewrite_record: Callable[[Path], None]
    _strip_wheel_native_libraries: Callable[[Path], None]

    def num_jobs(self) -> int:
        return self._num_jobs()

    def prepend_cmake_args(self, first: str, second: str) -> None:
        self._prepend_cmake_args(first, second)

    def is_disabled(self, value: str) -> bool:
        return self._is_disabled(value)

    def should_strip(self, path: Path) -> bool:
        return self._should_strip(path)

    def rewrite_record(self, wheel_root: Path) -> None:
        self._rewrite_record(wheel_root)

    def strip_wheel_native_libraries(self, wheel_path: Path) -> None:
        self._strip_wheel_native_libraries(wheel_path)


def load_backend() -> BackendModule:
    fake_scikit_build_core = types.ModuleType("scikit_build_core")
    setattr(fake_scikit_build_core, "build", types.SimpleNamespace())
    sys.modules["scikit_build_core"] = fake_scikit_build_core

    spec = importlib.util.spec_from_file_location(
        "pyyggdrasil_build_backend", ROOT / "python" / "pyyggdrasil_build_backend.py"
    )
    assert spec is not None
    assert spec.loader is not None
    assert spec.origin is not None
    module = BackendModule(spec.name)
    module.__file__ = spec.origin
    module.__loader__ = spec.loader
    module.__package__ = spec.parent
    module.__spec__ = spec
    spec.loader.exec_module(module)
    return module


def with_env(name: str, value: str | None) -> str | None:
    old_value = os.environ.get(name)
    if value is None:
        os.environ.pop(name, None)
    else:
        os.environ[name] = value
    return old_value


def restore_env(name: str, old_value: str | None) -> None:
    if old_value is None:
        os.environ.pop(name, None)
    else:
        os.environ[name] = old_value


def expect_raises(expected: type[BaseException], func: Callable[[], object]) -> None:
    try:
        func()
    except expected:
        return
    raise AssertionError(f"expected {expected.__name__}")


def expect_raises_message(
    expected: type[BaseException], message_fragment: str, func: Callable[[], object]
) -> None:
    try:
        func()
    except expected as error:
        assert message_fragment in str(error)
        return
    raise AssertionError(f"expected {expected.__name__}")


def test_job_count(backend: BackendModule) -> None:
    old = with_env("YGGDRASIL_JOBS", None)
    try:
        assert backend.num_jobs() == (os.cpu_count() or 1)
        os.environ["YGGDRASIL_JOBS"] = "6"
        assert backend.num_jobs() == 6
        for value in ("0", "-1", "many"):
            os.environ["YGGDRASIL_JOBS"] = value
            expect_raises(RuntimeError, backend.num_jobs)
    finally:
        restore_env("YGGDRASIL_JOBS", old)


def test_cmake_arg_prepend(backend: BackendModule) -> None:
    old = with_env("CMAKE_ARGS", '-DOLD_PATH="/tmp/old path" -DKEEP=1')
    try:
        backend.prepend_cmake_args("-DNEW_PATH=/tmp/new path", "-DENABLED=ON")
        assert shlex.split(os.environ["CMAKE_ARGS"]) == [
            "-DNEW_PATH=/tmp/new path",
            "-DENABLED=ON",
            "-DOLD_PATH=/tmp/old path",
            "-DKEEP=1",
        ]
    finally:
        restore_env("CMAKE_ARGS", old)


def test_disabled_value_parsing(backend: BackendModule) -> None:
    for value in ("0", "false", "False", "OFF", "no"):
        assert backend.is_disabled(value)

    for value in ("1", "true", "ON", "yes", ""):
        assert not backend.is_disabled(value)


def test_strip_candidate_detection(backend: BackendModule) -> None:
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        assert backend.should_strip(root / "module.so")
        assert backend.should_strip(
            root / "module.cpython-312-x86_64-linux-gnu.so"
        )
        assert backend.should_strip(root / "libtbb.so.12.16")
        assert backend.should_strip(root / "module.dylib")
        assert backend.should_strip(root / "module.pyd")
        assert backend.should_strip(root / "module.dll")
        assert backend.should_strip(root / "bin" / "hydra_pmi_proxy")
        assert backend.should_strip(root / "bin" / "mpiexec")
        assert not backend.should_strip(root / "module.py")
        assert not backend.should_strip(root / "module.so.txt")
        assert not backend.should_strip(root / "module.so.")
        assert not backend.should_strip(root / "module.so.12a")
        assert not backend.should_strip(root / "module.so.12.16a")
        assert not backend.should_strip(root / "libexec" / "mpiexec")


def read_record(wheel: zipfile.ZipFile) -> dict[str, list[str]]:
    return {
        row[0]: row[1:]
        for row in csv.reader(
            wheel.read("pyyggdrasil-0.0.0.dist-info/RECORD")
            .decode("utf-8")
            .splitlines()
        )
    }


def test_rewrite_record(backend: BackendModule) -> None:
    with tempfile.TemporaryDirectory() as tmp:
        wheel_root = Path(tmp) / "wheel"
        dist_info = wheel_root / "pyyggdrasil-0.0.0.dist-info"
        package_dir = wheel_root / "pyyggdrasil"
        dist_info.mkdir(parents=True)
        package_dir.mkdir()
        payload = package_dir / "__init__.py"
        payload.write_text("value = 1\n", encoding="utf-8")
        (dist_info / "RECORD").write_text("", encoding="utf-8")

        backend.rewrite_record(wheel_root)

        rows = read_record_from_path(dist_info / "RECORD")
        digest = (
            base64.urlsafe_b64encode(hashlib.sha256(payload.read_bytes()).digest())
            .rstrip(b"=")
            .decode("ascii")
        )
        assert rows["pyyggdrasil/__init__.py"] == [
            f"sha256={digest}",
            str(payload.stat().st_size),
        ]
        assert rows["pyyggdrasil-0.0.0.dist-info/RECORD"] == ["", ""]


def read_record_from_path(record: Path) -> dict[str, list[str]]:
    return {
        row[0]: row[1:]
        for row in csv.reader(record.read_text(encoding="utf-8").splitlines())
    }


def test_rewrite_record_requires_exactly_one_record_file(
    backend: BackendModule,
) -> None:
    with tempfile.TemporaryDirectory() as tmp:
        wheel_root = Path(tmp) / "wheel"
        wheel_root.mkdir()

        expect_raises_message(
            RuntimeError,
            "expected exactly one RECORD file, found 0",
            lambda: backend.rewrite_record(wheel_root),
        )

        first_dist_info = wheel_root / "first.dist-info"
        second_dist_info = wheel_root / "second.dist-info"
        first_dist_info.mkdir()
        second_dist_info.mkdir()
        (first_dist_info / "RECORD").write_text("", encoding="utf-8")
        (second_dist_info / "RECORD").write_text("", encoding="utf-8")

        expect_raises_message(
            RuntimeError,
            "expected exactly one RECORD file, found 2",
            lambda: backend.rewrite_record(wheel_root),
        )


def test_strip_wheel_native_libraries(backend: BackendModule) -> None:
    with tempfile.TemporaryDirectory() as tmp:
        wheel_path = Path(tmp) / "pyyggdrasil-0.0.0-py3-none-any.whl"
        executable = zipfile.ZipInfo("pyyggdrasil/bin/mpiexec")
        executable.external_attr = 0o755 << 16
        with zipfile.ZipFile(wheel_path, "w", compression=zipfile.ZIP_DEFLATED) as wheel:
            wheel.writestr(executable, b"native executable")
            wheel.writestr("pyyggdrasil-0.0.0.dist-info/RECORD", "")

        with (
            mock.patch.object(backend.shutil, "which", return_value="/usr/bin/strip"),
            mock.patch.object(backend.subprocess, "run") as strip,
        ):
            backend.strip_wheel_native_libraries(wheel_path)

        strip.assert_called_once()
        with zipfile.ZipFile(wheel_path) as wheel:
            assert (wheel.getinfo(executable.filename).external_attr >> 16) & 0o777 == 0o755
            rows = read_record(wheel)
            assert rows[executable.filename][0].startswith("sha256=")
            assert rows["pyyggdrasil-0.0.0.dist-info/RECORD"] == ["", ""]


def main() -> None:
    backend = load_backend()
    test_job_count(backend)
    test_cmake_arg_prepend(backend)
    test_disabled_value_parsing(backend)
    test_strip_candidate_detection(backend)
    test_rewrite_record(backend)
    test_rewrite_record_requires_exactly_one_record_file(backend)
    test_strip_wheel_native_libraries(backend)


if __name__ == "__main__":
    main()
