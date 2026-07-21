"""Contract tests for the CMake helpers shipped in the pyyggdrasil wheel.

The yggdrasil_* helper functions in lib/cmake/yggdrasil/yggdrasilPythonHelpers.cmake
are consumed by the downstream repos (loki, tyr, runir) both from their
CMakeLists.txt and from their exported Config files. These tests pin the
behavioral contract against the installed wheel.
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
        pytest.skip("cmake is required for the cmake helper contract tests")
    return cmake


def _helpers_path() -> Path:
    helpers = Path(pyyggdrasil.cmake_dir()) / "yggdrasilPythonHelpers.cmake"
    assert helpers.is_file(), f"helpers module missing from wheel: {helpers}"
    return helpers


def test_helper_functions_contract(tmp_path: Path) -> None:
    cmake = _require_cmake()
    helpers = _helpers_path()

    dependency_prefix = tmp_path / "dependency"
    (dependency_prefix / "lib").mkdir(parents=True)

    script = tmp_path / "contract.cmake"
    script.write_text(
        textwrap.dedent(
            f"""
            cmake_minimum_required(VERSION 3.21)
            set(CMAKE_INSTALL_LIBDIR lib64)
            include("{helpers.as_posix()}")

            # libdir detection: prefix only has lib/, CMAKE_INSTALL_LIBDIR is lib64
            yggdrasil_register_python_native_runtime_prefix("pyyggdrasil" "{dependency_prefix.as_posix()}")
            # no second argument: falls back to CMAKE_INSTALL_LIBDIR
            yggdrasil_register_python_native_runtime_prefix("pypddl/native")

            yggdrasil_make_python_native_runtime_rpaths(runtime_rpaths "$ORIGIN" "../")
            if(NOT runtime_rpaths STREQUAL "$ORIGIN;$ORIGIN/../pyyggdrasil/lib;$ORIGIN/../pypddl/native/lib64")
                message(FATAL_ERROR "unexpected rpath list: ${{runtime_rpaths}}")
            endif()

            yggdrasil_make_python_native_runtime_rpath_string(runtime_rpath "@loader_path" "../../../")
            if(NOT runtime_rpath STREQUAL "@loader_path:@loader_path/../../../pyyggdrasil/lib:@loader_path/../../../pypddl/native/lib64")
                message(FATAL_ERROR "unexpected rpath string: ${{runtime_rpath}}")
            endif()

            # include-dir registration only records existing directories
            yggdrasil_register_native_dependency_prefix("{dependency_prefix.as_posix()}")
            if(YGGDRASIL_NATIVE_DEPENDENCY_INCLUDE_DIRECTORIES)
                message(FATAL_ERROR "prefix without include/ must not register: ${{YGGDRASIL_NATIVE_DEPENDENCY_INCLUDE_DIRECTORIES}}")
            endif()

            # manual override: preset out-var is respected and prepended to CMAKE_PREFIX_PATH
            set(OVERRIDE_PREFIX "{dependency_prefix.as_posix()}")
            yggdrasil_find_python_native_prefix(nonexistent_package OVERRIDE_PREFIX)
            if(NOT OVERRIDE_PREFIX STREQUAL "{dependency_prefix.as_posix()}")
                message(FATAL_ERROR "manual override was clobbered: ${{OVERRIDE_PREFIX}}")
            endif()
            if(NOT "{dependency_prefix.as_posix()}" IN_LIST CMAKE_PREFIX_PATH)
                message(FATAL_ERROR "override prefix missing from CMAKE_PREFIX_PATH")
            endif()
            """
        ),
        encoding="utf-8",
    )

    subprocess.run([cmake, "-P", str(script)], check=True)


def test_find_package_provides_helpers_and_target(tmp_path: Path) -> None:
    cmake = _require_cmake()
    prefix = pyyggdrasil.native_prefix()

    (tmp_path / "CMakeLists.txt").write_text(
        textwrap.dedent(
            f"""
            cmake_minimum_required(VERSION 3.21)
            project(helpers_probe LANGUAGES CXX)
            find_package(yggdrasil CONFIG REQUIRED PATHS "{Path(prefix).as_posix()}" NO_DEFAULT_PATH)
            if(NOT TARGET yggdrasil::yggdrasil)
                message(FATAL_ERROR "yggdrasil::yggdrasil target missing")
            endif()
            if(NOT COMMAND yggdrasil_make_python_native_runtime_rpaths)
                message(FATAL_ERROR "helper functions not provided by yggdrasilConfig")
            endif()
            message(STATUS "helpers contract OK")
            """
        ),
        encoding="utf-8",
    )

    result = subprocess.run(
        [cmake, "-S", str(tmp_path), "-B", str(tmp_path / "build")],
        check=True,
        capture_output=True,
        text=True,
    )
    assert "helpers contract OK" in result.stdout
