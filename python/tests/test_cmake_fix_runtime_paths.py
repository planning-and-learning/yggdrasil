"""Contract test for the shipped yggdrasilFixRuntimePaths.cmake helper.

Drives the module via `cmake -P` against the installed wheel, covering a no-op
when the glob matches no library directory and native runtime-path rewrites on
real ELF and Mach-O shared objects.
"""

import shutil
import subprocess
import sys
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


def test_fix_runtime_paths_rewrites_bundled_dylib_dependency(tmp_path: Path) -> None:
    if sys.platform != "darwin":
        pytest.skip("Mach-O dependency rewriting is only available on macOS")

    cmake = _require_cmake()
    module = _module()
    otool = shutil.which("otool")
    install_name_tool = shutil.which("install_name_tool")
    cxx = shutil.which("c++") or shutil.which("clang++")
    if otool is None or install_name_tool is None or cxx is None:
        pytest.skip("Apple binary tools and a C++ compiler are required for the Mach-O check")

    lib_dir = tmp_path / "prefix" / "pkg" / "lib"
    lib_dir.mkdir(parents=True)
    dependency_source = tmp_path / "dependency.cpp"
    dependency_source.write_text(
        'extern "C" int dependency() { return 1; }\n', encoding="utf-8"
    )
    consumer_source = tmp_path / "consumer.cpp"
    consumer_source.write_text(
        'extern "C" int dependency();\n'
        'extern "C" int consumer() { return dependency(); }\n',
        encoding="utf-8",
    )

    dependency = lib_dir / "libdependency.dylib"
    leaked_dependency = tmp_path / "dependencies-install" / "lib" / dependency.name
    consumer = lib_dir / "libconsumer.dylib"
    subprocess.run(
        [
            cxx,
            "-dynamiclib",
            "-Wl,-headerpad_max_install_names",
            f"-Wl,-install_name,{leaked_dependency}",
            "-o",
            str(dependency),
            str(dependency_source),
        ],
        check=True,
    )
    subprocess.run(
        [
            cxx,
            "-dynamiclib",
            "-Wl,-headerpad_max_install_names",
            "-o",
            str(consumer),
            str(consumer_source),
            str(dependency),
        ],
        check=True,
    )

    before = subprocess.run(
        [otool, "-L", str(consumer)], check=True, capture_output=True, text=True
    ).stdout
    assert str(leaked_dependency) in before
    system_dependencies = [
        line.strip().split(" (", 1)[0]
        for line in before.splitlines()
        if line.strip().startswith(("/usr/lib/", "/System/Library/"))
    ]

    script = tmp_path / "rpath.cmake"
    script.write_text(
        textwrap.dedent(
            f"""
            cmake_minimum_required(VERSION 3.21)
            set(CMAKE_INSTALL_PREFIX "{(tmp_path / 'prefix').as_posix()}")
            include("{module.as_posix()}")
            yggdrasil_fix_runtime_paths(
                LIB_DIR_GLOB "pkg/lib"
                RPATHS "@loader_path;@loader_path/../pyyggdrasil/lib"
            )
            """
        ),
        encoding="utf-8",
    )
    subprocess.run([cmake, "-P", str(script)], check=True)

    after = subprocess.run(
        [otool, "-L", str(consumer)], check=True, capture_output=True, text=True
    ).stdout
    assert "@rpath/libdependency.dylib" in after
    assert str(leaked_dependency) not in after
    assert all(system_dependency in after for system_dependency in system_dependencies)

    dependency_id = subprocess.run(
        [otool, "-D", str(dependency)], check=True, capture_output=True, text=True
    ).stdout
    assert "@rpath/libdependency.dylib" in dependency_id

    load_commands = subprocess.run(
        [otool, "-l", str(consumer)], check=True, capture_output=True, text=True
    ).stdout
    assert "path @loader_path (offset" in load_commands
    assert "path @loader_path/../pyyggdrasil/lib (offset" in load_commands
