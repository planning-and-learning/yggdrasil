import csv
import io
import os
import sys
import types
import zipfile
from collections.abc import Callable
from pathlib import Path
from typing import cast

import pytest

from pyyggdrasil.build_support import ProviderBackend


def _backend() -> ProviderBackend:
    return ProviderBackend(
        package="consumer",
        providers=("provider", "pyyggdrasil"),
        cmake_defines=("-DCONSUMER=ON",),
        extra_cmake_defines=lambda: ("-DEXTRA=ON",),
        jobs_env="CONSUMER_JOBS",
    )


def _num_jobs(backend: ProviderBackend) -> int:
    return cast(Callable[[], int], getattr(backend, "_num_jobs"))()


def _prepare_native_build(backend: ProviderBackend) -> None:
    cast(Callable[[], None], getattr(backend, "_prepare_native_build"))()


def _fix_wheel_stubs(backend: ProviderBackend, wheel_path: Path) -> None:
    cast(Callable[[Path], None], getattr(backend, "_fix_wheel_stubs"))(wheel_path)


def _fake_provider(name: str, prefix: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    def native_prefix() -> Path:
        return prefix

    provider = types.ModuleType(name)
    setattr(provider, "native_prefix", native_prefix)
    monkeypatch.setitem(sys.modules, name, provider)


def _write_wheel(path: Path, files: dict[str, str]) -> None:
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as wheel:
        for name, content in sorted(files.items()):
            wheel.writestr(name, content)


def _read_wheel(path: Path) -> dict[str, str]:
    with zipfile.ZipFile(path) as wheel:
        return {name: wheel.read(name).decode() for name in wheel.namelist() if not name.endswith("/")}


def test_jobs_and_native_build_environment(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    backend = _backend()
    monkeypatch.delenv("CONSUMER_JOBS", raising=False)
    monkeypatch.setattr(os, "cpu_count", lambda: 4)
    assert _num_jobs(backend) == 4
    monkeypatch.setenv("CONSUMER_JOBS", "6")
    assert _num_jobs(backend) == 6

    provider_prefix = tmp_path / "provider"
    provider_library = provider_prefix / "lib"
    provider_library.mkdir(parents=True)
    yggdrasil_prefix = tmp_path / "yggdrasil"
    yggdrasil_library = yggdrasil_prefix / "lib64"
    yggdrasil_library.mkdir(parents=True)
    _fake_provider("provider", provider_prefix, monkeypatch)
    _fake_provider("pyyggdrasil", yggdrasil_prefix, monkeypatch)

    monkeypatch.setenv("CMAKE_ARGS", "-DUSER_OPTION=ON")
    monkeypatch.delenv("CMAKE_BUILD_PARALLEL_LEVEL", raising=False)
    monkeypatch.setenv("LD_LIBRARY_PATH", "/existing/ld")
    monkeypatch.setenv("DYLD_LIBRARY_PATH", "/existing/dyld")

    _prepare_native_build(backend)

    assert os.environ["CMAKE_BUILD_PARALLEL_LEVEL"] == "6"
    assert os.environ["LD_LIBRARY_PATH"] == os.pathsep.join(
        (str(provider_library), str(yggdrasil_library), "/existing/ld")
    )
    assert os.environ["DYLD_LIBRARY_PATH"] == os.pathsep.join(
        (str(provider_library), str(yggdrasil_library), "/existing/dyld")
    )
    assert os.environ["CMAKE_ARGS"] == " ".join(
        (
            f"-DCMAKE_PREFIX_PATH={provider_prefix.resolve()};{yggdrasil_prefix.resolve()}",
            f"-DYGGDRASIL_NATIVE_PREFIX={yggdrasil_prefix.resolve()}",
            f"-DPython_EXECUTABLE={sys.executable}",
            "-DCONSUMER=ON",
            "-DEXTRA=ON",
            "-DUSER_OPTION=ON",
        )
    )


def test_num_jobs_rejects_invalid_values(monkeypatch: pytest.MonkeyPatch) -> None:
    backend = _backend()
    for value in ("0", "-1", "many"):
        monkeypatch.setenv("CONSUMER_JOBS", value)
        with pytest.raises(ValueError, match="positive integer"):
            _num_jobs(backend)


@pytest.mark.parametrize("private_root", ("_consumer", "_consumer.cpython-313-x86_64-linux-gnu"))
def test_default_stub_rewriting_and_publication(tmp_path: Path, private_root: str) -> None:
    backend = _backend()
    assert backend.rename_packages == ("consumer", "provider", "pyyggdrasil")
    assert ProviderBackend("consumer", ("pyyggdrasil",), rename_packages=()).rename_packages == ()

    wheel_path = tmp_path / "consumer-1.0.0-py3-none-any.whl"
    generated_stub = (
        "ref: consumer._consumer.api provider._provider pyyggdrasil._pyyggdrasil\n"
        "def load(path: os.PathLike, typed: os.PathLike[str]) -> None: ...\n"
    )
    _write_wheel(
        wheel_path,
        {
            "consumer/__init__.pyi": "from . import api as api\n",
            "consumer/api/__init__.py": "",
            "consumer/py.typed": "",
            f"consumer/{private_root}/__init__.pyi": "generated root must not replace public\n",
            f"consumer/{private_root}/api.pyi": generated_stub,
            "consumer-1.0.0.dist-info/RECORD": "",
        },
    )

    _fix_wheel_stubs(backend, wheel_path)

    files = _read_wheel(wheel_path)
    public_stub = "consumer/api/__init__.pyi"
    assert files["consumer/__init__.pyi"] == "from . import api as api\n"
    assert files[public_stub] == (
        "ref: consumer.api provider pyyggdrasil\n"
        "def load(path: os.PathLike[str], typed: os.PathLike[str]) -> None: ...\n"
    )
    assert not any(name.startswith(f"consumer/{private_root}/") for name in files)

    record_path = "consumer-1.0.0.dist-info/RECORD"
    record = {row[0]: row[1:] for row in csv.reader(io.StringIO(files[record_path]))}
    assert f"consumer/{private_root}/api.pyi" not in record
    assert record[public_stub][0].startswith("sha256=")
    assert record[public_stub][1] == str(len(files[public_stub].encode()))
    assert record["consumer/py.typed"][1] == "0"
    assert record[record_path] == ["", ""]
