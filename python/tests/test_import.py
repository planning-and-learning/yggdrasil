import shutil
import subprocess
import textwrap

import pytest

import pyyggdrasil


def test_native_prefix_layout():
    native_prefix = pyyggdrasil.native_prefix()

    assert pyyggdrasil.__version__ != ""
    assert pyyggdrasil.ExecutionContext(1).num_threads == 1
    assert (native_prefix / "include").is_dir()
    assert (native_prefix / "lib").is_dir()
    assert (native_prefix / "include" / "boost").is_dir()
    assert (native_prefix / "include" / "yggdrasil.hpp").is_file()
    assert (native_prefix / "include" / "yggdrasil" / "containers" / "indexed_hash_set.hpp").is_file()
    assert (native_prefix / "include" / "yggdrasil" / "buffer" / "indexed_hash_set.hpp").is_file()
    assert (native_prefix / "lib" / "cmake").is_dir()


def test_native_prefix_prefers_installed_package_prefix(tmp_path, monkeypatch):
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


def test_downstream_consumer_can_compile_ygg_common(tmp_path):
    compiler = shutil.which("c++") or shutil.which("clang++") or shutil.which("g++")
    if compiler is None:
        pytest.skip("No C++ compiler available")

    native_prefix = pyyggdrasil.native_prefix()
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
            f"-I{native_prefix / 'include'}",
            f"-I{native_prefix / 'nanobind' / 'include'}",
            "-fsyntax-only",
            str(source),
        ],
        check=True,
    )
