import shutil
import subprocess
import sys
import textwrap
from pathlib import Path

import pytest

import pyyggdrasil
import pyyggdrasil.execution as execution


def test_native_prefix_layout() -> None:
    native_prefix = pyyggdrasil.native_prefix()

    assert pyyggdrasil.__version__ == "0.1.2"
    assert pyyggdrasil.execution.ExecutionContext(1).num_threads == 1
    assert pyyggdrasil.execution.ExecutionContext.max_num_threads() >= 1
    assert pyyggdrasil.include_dir() == native_prefix / "include"
    assert pyyggdrasil.include_dir().is_dir()
    assert native_prefix / "lib" in pyyggdrasil.library_dirs()
    assert native_prefix / "lib" / "cmake" in pyyggdrasil.cmake_dirs()
    assert (native_prefix / "lib").is_dir()
    assert (native_prefix / "include" / "boost").is_dir()
    assert (native_prefix / "include" / "boost" / "hash2" / "sha2.hpp").is_file()
    assert (native_prefix / "include" / "toml++" / "toml.hpp").is_file()
    assert (native_prefix / "include" / "yggdrasil.hpp").is_file()
    assert (
        native_prefix / "include" / "yggdrasil" / "containers" / "indexed_hash_set.hpp"
    ).is_file()
    assert (
        native_prefix / "include" / "yggdrasil" / "buffer" / "indexed_hash_set.hpp"
    ).is_file()
    assert (native_prefix / "lib" / "cmake").is_dir()
    assert (native_prefix / "nanobind" / "cmake" / "nanobind-config.cmake").is_file()
    assert not list(native_prefix.glob("lib*/libnanobind*"))
    assert pyyggdrasil.cmake_prefix() == native_prefix
    assert pyyggdrasil.cmake_dir().name == "yggdrasil"
    assert (pyyggdrasil.cmake_dir() / "yggdrasilConfig.cmake").is_file()
    assert (pyyggdrasil.cmake_dir() / "yggdrasilConfigVersion.cmake").is_file()
    assert (
        native_prefix / "lib" / "cmake" / "tomlplusplus" / "tomlplusplusConfig.cmake"
    ).is_file()


def test_execution_submodule_is_public() -> None:
    assert execution is pyyggdrasil.execution


def test_public_package_exports_are_explicit() -> None:
    assert pyyggdrasil.__all__ == [
        "__version__",
        "cmake_dir",
        "cmake_dirs",
        "cmake_prefix",
        "include_dir",
        "library_dirs",
        "native_prefix",
        "diagnostics",
        "execution",
        "serialization",
    ]
    for name in pyyggdrasil.__all__:
        assert hasattr(pyyggdrasil, name)


def test_module_cli_prints_discovery_paths() -> None:
    for flag, expected in [
        ("--prefix", str(pyyggdrasil.cmake_prefix())),
        ("--include-dir", str(pyyggdrasil.include_dir())),
        ("--cmake-dir", str(pyyggdrasil.cmake_dir())),
        ("--version", pyyggdrasil.__version__),
    ]:
        result = subprocess.run(
            [sys.executable, "-m", "pyyggdrasil", flag],
            check=True,
            capture_output=True,
            text=True,
        )
        assert result.stdout.strip() == expected


def test_execution_context_exposes_introspection_docs() -> None:
    assert "worker threads" in (pyyggdrasil.execution.ExecutionContext.__doc__ or "")
    assert "maximum thread count" in (pyyggdrasil.execution.ExecutionContext.max_num_threads.__doc__ or "")
    assert "Configured worker thread count" in (pyyggdrasil.execution.ExecutionContext.num_threads.__doc__ or "")


def test_execution_context_rejects_invalid_thread_counts() -> None:
    max_num_threads = pyyggdrasil.execution.ExecutionContext.max_num_threads()

    with pytest.raises(ValueError, match="num_threads must be at least 1"):
        pyyggdrasil.execution.ExecutionContext(0)

    with pytest.raises(ValueError, match="threads"):
        pyyggdrasil.execution.ExecutionContext(max_num_threads + 1)


def test_execution_context_repr() -> None:
    assert repr(pyyggdrasil.execution.ExecutionContext(1)) == "ExecutionContext(num_threads=1)"


def test_execution_context_supports_context_manager() -> None:
    with pyyggdrasil.execution.ExecutionContext(1) as context:
        assert isinstance(context, pyyggdrasil.execution.ExecutionContext)
        assert context.num_threads == 1

    with pytest.raises(RuntimeError, match="boom"):
        with pyyggdrasil.execution.ExecutionContext(1):
            raise RuntimeError("boom")


def test_source_version_reads_pyproject(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    package_dir = tmp_path / "src" / "pyyggdrasil"
    package_dir.mkdir(parents=True)
    pyproject = tmp_path / "pyproject.toml"
    pyproject.write_text(
        textwrap.dedent(
            """\
            [project]
            name = "pyyggdrasil"
            version = "1.2.3"
            """
        ),
        encoding="utf-8",
    )
    monkeypatch.setattr(pyyggdrasil, "__file__", str(package_dir / "__init__.py"))

    # _source_version is private; reach it via getattr to test internals without a private-access lint.
    source_version = getattr(pyyggdrasil, "_source_version")
    assert source_version() == "1.2.3"


def test_native_prefix_prefers_installed_package_prefix(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    repo_root = tmp_path / "repo"
    repository_include = repo_root / "include" / "yggdrasil"
    repository_include.mkdir(parents=True)

    package_root = repo_root / "site-packages" / "pyyggdrasil"
    package_root.mkdir(parents=True, exist_ok=True)
    (package_root / "include" / "yggdrasil").mkdir(parents=True)
    (package_root / "__init__.py").write_text("# test shim", encoding="utf-8")

    repository_root = repo_root / "pyproject.toml"
    repository_root.write_text("[project]\nname = \"repo\"\n", encoding="utf-8")
    monkeypatch.setattr(pyyggdrasil, "__file__", str(package_root / "__init__.py"))

    assert pyyggdrasil.native_prefix() == package_root


def test_downstream_consumer_can_compile_ygg_common(tmp_path: Path) -> None:
    compiler = shutil.which("c++") or shutil.which("clang++") or shutil.which("g++")
    if compiler is None:
        pytest.skip("No C++ compiler available")

    source = tmp_path / "consumer.cpp"
    source.write_text(
        textwrap.dedent(
            """\
            #include <yggdrasil/semantics/hash.hpp>
            #include <yggdrasil/containers/indexed_hash_set.hpp>
            #include <yggdrasil/containers/unordered_set.hpp>
            #include <yggdrasil/core/types.hpp>
            #include <yggdrasil/ids/index_mixins.hpp>

            #include <tuple>

            struct Item {};
            struct ItemData
            {
                int value;
                auto identifying_members() const noexcept { return std::tie(value); }
            };

            namespace ygg
            {
            template<>
            struct Data<Item> : ItemData
            {
                using ItemData::ItemData;
            };

            template<>
            struct Index<Item> : IndexMixin<Index<Item>>
            {
                using Base = IndexMixin<Index<Item>>;
                using Base::Base;
            };
            }

            int main()
            {
                auto index = ygg::Index<Item>(1);
                auto indices = ygg::UnorderedSet<ygg::Index<Item>> {};
                indices.insert(index);
                return indices.contains(index) ? 0 : 1;
            }
            """
        ),
        encoding="utf-8",
    )

    subprocess.run(
        [
            compiler,
            "-std=c++20",
            f"-I{pyyggdrasil.include_dir()}",
            "-fsyntax-only",
            str(source),
        ],
        check=True,
    )


def test_downstream_cmake_packages_configure(tmp_path: Path) -> None:
    cmake = shutil.which("cmake")
    if cmake is None:
        pytest.skip("CMake is not available")

    source_dir = tmp_path / "source"
    source_dir.mkdir()
    (source_dir / "CMakeLists.txt").write_text(
        textwrap.dedent(
            """\
            cmake_minimum_required(VERSION 3.21)
            project(pyyggdrasil_provider_probe LANGUAGES CXX)

            find_package(Python 3.11 REQUIRED COMPONENTS Interpreter Development.Module)
            find_package(yggdrasil 0.1.2 CONFIG REQUIRED PATHS ${CMAKE_PREFIX_PATH} NO_DEFAULT_PATH)
            find_package(nanobind CONFIG REQUIRED PATHS ${CMAKE_PREFIX_PATH} NO_DEFAULT_PATH)
            find_package(tomlplusplus 3.4 CONFIG REQUIRED PATHS ${CMAKE_PREFIX_PATH} NO_DEFAULT_PATH)

            add_library(provider_probe INTERFACE)
            target_link_libraries(provider_probe INTERFACE yggdrasil::yggdrasil tomlplusplus::tomlplusplus)
            """
        ),
        encoding="utf-8",
    )

    subprocess.run(
        [
            cmake,
            "-S",
            str(source_dir),
            "-B",
            str(tmp_path / "build"),
            f"-DCMAKE_PREFIX_PATH={pyyggdrasil.cmake_prefix()}",
        ],
        check=True,
    )
