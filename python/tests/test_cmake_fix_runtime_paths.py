"""Contract test for the shipped yggdrasilFixRuntimePaths.cmake helper.

Drives the module via `cmake -P` against the installed wheel, covering the
two behaviors that matter on Linux CI: a no-op when the glob matches no
library directory, and an $ORIGIN rpath rewrite on a real ELF shared object
when patchelf is available.
"""

import shutil
import subprocess
import textwrap
from pathlib import Path

import pytest

import pyyggdrasil


def _require_cmake() -> str:
    cmake = shutil.which("cmake")
    if cmake is None:
        pytest.skip("cmake is required for the fix-runtime-paths contract test")
    return cmake


def _module() -> Path:
    module = Path(pyyggdrasil.cmake_dir()) / "yggdrasilFixRuntimePaths.cmake"
    assert module.is_file(), f"module missing from wheel: {module}"
    return module


def test_fix_runtime_paths_noop_without_libraries(tmp_path: Path) -> None:
    cmake = _require_cmake()
    module = _module()
    (tmp_path / "prefix" / "pkg" / "lib").mkdir(parents=True)  # exists but empty

    script = tmp_path / "noop.cmake"
    script.write_text(
        textwrap.dedent(
            f"""
            cmake_minimum_required(VERSION 3.21)
            set(CMAKE_INSTALL_PREFIX "{(tmp_path / 'prefix').as_posix()}")
            include("{module.as_posix()}")
            yggdrasil_fix_runtime_paths(LIB_DIR_GLOB "pkg/lib")
            message(STATUS "fix-runtime-paths noop OK")
            """
        ),
        encoding="utf-8",
    )
    result = subprocess.run([cmake, "-P", str(script)], check=True, capture_output=True, text=True)
    assert "fix-runtime-paths noop OK" in (result.stdout + result.stderr)


def test_fix_runtime_paths_sets_origin_rpath(tmp_path: Path) -> None:
    cmake = _require_cmake()
    module = _module()
    patchelf = shutil.which("patchelf")
    cxx = shutil.which("c++") or shutil.which("g++") or shutil.which("clang++")
    if patchelf is None or cxx is None:
        pytest.skip("patchelf and a C++ compiler are required for the rpath rewrite check")

    lib_dir = tmp_path / "prefix" / "pkg" / "lib"
    lib_dir.mkdir(parents=True)
    src = tmp_path / "stub.cpp"
    src.write_text("int yggdrasil_stub() { return 0; }\n", encoding="utf-8")
    so = lib_dir / "libstub.so"
    subprocess.run([cxx, "-shared", "-fPIC", "-o", str(so), str(src)], check=True)
    subprocess.run([patchelf, "--set-rpath", "/bogus/original", str(so)], check=True)

    script = tmp_path / "rpath.cmake"
    script.write_text(
        textwrap.dedent(
            f"""
            cmake_minimum_required(VERSION 3.21)
            set(CMAKE_INSTALL_PREFIX "{(tmp_path / 'prefix').as_posix()}")
            include("{module.as_posix()}")
            yggdrasil_fix_runtime_paths(LIB_DIR_GLOB "pkg/lib" RPATH "$ORIGIN/../sibling")
            """
        ),
        encoding="utf-8",
    )
    subprocess.run([cmake, "-P", str(script)], check=True)

    rpath = subprocess.run(
        [patchelf, "--print-rpath", str(so)], check=True, capture_output=True, text=True
    ).stdout.strip()
    assert rpath == "$ORIGIN/../sibling"
