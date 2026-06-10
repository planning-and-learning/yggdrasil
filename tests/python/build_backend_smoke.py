#!/usr/bin/env python3
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
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def load_backend():
    fake_scikit_build_core = types.ModuleType("scikit_build_core")
    fake_scikit_build_core.build = types.SimpleNamespace()
    sys.modules["scikit_build_core"] = fake_scikit_build_core

    spec = importlib.util.spec_from_file_location(
        "pyyggdrasil_build_backend", ROOT / "python" / "pyyggdrasil_build_backend.py"
    )
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def with_env(name: str, value: str | None):
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


def expect_raises(expected, func) -> None:
    try:
        func()
    except expected:
        return
    raise AssertionError(f"expected {expected.__name__}")


def expect_raises_message(expected, message_fragment: str, func) -> None:
    try:
        func()
    except expected as error:
        assert message_fragment in str(error)
        return
    raise AssertionError(f"expected {expected.__name__}")


def test_job_count(backend) -> None:
    old = with_env("YGGDRASIL_JOBS", None)
    try:
        assert backend._num_jobs() == backend.DEFAULT_BUILD_JOBS
        os.environ["YGGDRASIL_JOBS"] = "6"
        assert backend._num_jobs() == 6
        for value in ("0", "-1", "many"):
            os.environ["YGGDRASIL_JOBS"] = value
            expect_raises(RuntimeError, backend._num_jobs)
    finally:
        restore_env("YGGDRASIL_JOBS", old)


def test_cmake_arg_prepend(backend) -> None:
    old = with_env("CMAKE_ARGS", '-DOLD_PATH="/tmp/old path" -DKEEP=1')
    try:
        backend._prepend_cmake_args("-DNEW_PATH=/tmp/new path", "-DENABLED=ON")
        assert shlex.split(os.environ["CMAKE_ARGS"]) == [
            "-DNEW_PATH=/tmp/new path",
            "-DENABLED=ON",
            "-DOLD_PATH=/tmp/old path",
            "-DKEEP=1",
        ]
    finally:
        restore_env("CMAKE_ARGS", old)


def test_disabled_value_parsing(backend) -> None:
    for value in ("0", "false", "False", "OFF", "no"):
        assert backend._is_disabled(value)

    for value in ("1", "true", "ON", "yes", ""):
        assert not backend._is_disabled(value)


def test_native_library_detection(backend) -> None:
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        assert backend._is_native_library(root / "module.so")
        assert backend._is_native_library(
            root / "module.cpython-312-x86_64-linux-gnu.so"
        )
        assert backend._is_native_library(root / "libtbb.so.12.16")
        assert backend._is_native_library(root / "module.dylib")
        assert backend._is_native_library(root / "module.pyd")
        assert backend._is_native_library(root / "module.dll")
        assert not backend._is_native_library(root / "module.py")
        assert not backend._is_native_library(root / "module.so.txt")
        assert not backend._is_native_library(root / "module.so.")
        assert not backend._is_native_library(root / "module.so.12a")
        assert not backend._is_native_library(root / "module.so.12.16a")


def read_record(wheel: zipfile.ZipFile):
    return {
        row[0]: row[1:]
        for row in csv.reader(
            wheel.read("pyyggdrasil-0.0.0.dist-info/RECORD")
            .decode("utf-8")
            .splitlines()
        )
    }


def test_rewrite_record(backend) -> None:
    with tempfile.TemporaryDirectory() as tmp:
        wheel_root = Path(tmp) / "wheel"
        dist_info = wheel_root / "pyyggdrasil-0.0.0.dist-info"
        package_dir = wheel_root / "pyyggdrasil"
        dist_info.mkdir(parents=True)
        package_dir.mkdir()
        payload = package_dir / "__init__.py"
        payload.write_text("value = 1\n", encoding="utf-8")
        (dist_info / "RECORD").write_text("", encoding="utf-8")

        backend._rewrite_record(wheel_root)

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


def read_record_from_path(record: Path):
    return {
        row[0]: row[1:]
        for row in csv.reader(record.read_text(encoding="utf-8").splitlines())
    }


def test_rewrite_record_requires_exactly_one_record_file(backend) -> None:
    with tempfile.TemporaryDirectory() as tmp:
        wheel_root = Path(tmp) / "wheel"
        wheel_root.mkdir()

        expect_raises_message(
            RuntimeError,
            "expected exactly one RECORD file, found 0",
            lambda: backend._rewrite_record(wheel_root),
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
            lambda: backend._rewrite_record(wheel_root),
        )


def test_rewrite_wheel(backend) -> None:
    with tempfile.TemporaryDirectory() as tmp:
        wheel_path = Path(tmp) / "pyyggdrasil-0.0.0-py3-none-any.whl"
        with zipfile.ZipFile(wheel_path, "w", compression=zipfile.ZIP_DEFLATED) as wheel:
            wheel.writestr("pyyggdrasil/__init__.py", "value = 1\n")
            wheel.writestr("pyyggdrasil-0.0.0.dist-info/RECORD", "")

        def mutate(wheel_root: Path) -> None:
            (wheel_root / "pyyggdrasil" / "added.py").write_text(
                "added = True\n", encoding="utf-8"
            )

        backend._rewrite_wheel(wheel_path, mutate)

        with zipfile.ZipFile(wheel_path) as wheel:
            assert wheel.read("pyyggdrasil/added.py").decode("utf-8") == "added = True\n"
            rows = read_record(wheel)
            assert rows["pyyggdrasil/added.py"][0].startswith("sha256=")
            assert rows["pyyggdrasil-0.0.0.dist-info/RECORD"] == ["", ""]


def main() -> None:
    backend = load_backend()
    test_job_count(backend)
    test_cmake_arg_prepend(backend)
    test_disabled_value_parsing(backend)
    test_native_library_detection(backend)
    test_rewrite_record(backend)
    test_rewrite_record_requires_exactly_one_record_file(backend)
    test_rewrite_wheel(backend)


if __name__ == "__main__":
    main()
