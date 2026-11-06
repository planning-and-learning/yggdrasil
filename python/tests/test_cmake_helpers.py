# Copyright (C) 2026 Dominik Drexler
# SPDX-License-Identifier: GPL-3.0-or-later
"""Contract test for the CMake package shipped in the pyyggdrasil wheel: find_package(yggdrasil) gives the target.

The build helpers the downstream repos use are brokkr's (pybrokkr), tested there.
"""

import shutil
import subprocess
import textwrap
from pathlib import Path

import pytest

import pyurd
import pyyggdrasil


def _require_cmake() -> str:
    cmake = shutil.which("cmake")
    if cmake is None:
        pytest.skip("cmake is required for the cmake helper contract tests")
    return cmake


def test_find_package_provides_target(tmp_path: Path) -> None:
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
            message(STATUS "yggdrasil contract OK")
            """
        ),
        encoding="utf-8",
    )

    result = subprocess.run(
        # pyurd's prefix, as a consumer's build backend or brokkr puts it there: yggdrasil's config finds urd in it.
        [cmake, "-S", str(tmp_path), "-B", str(tmp_path / "build"), f"-DCMAKE_PREFIX_PATH={pyurd.cmake_prefix()}"],
        capture_output=True,
        text=True,
        check=False,
    )
    assert result.returncode == 0, f"{result.stdout}\n{result.stderr}"
    assert "yggdrasil contract OK" in result.stdout
