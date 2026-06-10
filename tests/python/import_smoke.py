#!/usr/bin/env python3
import shutil
import sys
import tempfile
from pathlib import Path


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit("usage: import_smoke.py <package-init> <extension>")

    package_init = Path(sys.argv[1]).resolve()
    extension = Path(sys.argv[2]).resolve()

    with tempfile.TemporaryDirectory(prefix="pyyggdrasil-import-") as tmp:
        package_dir = Path(tmp) / "pyyggdrasil"
        package_dir.mkdir()
        lib_dir = package_dir / "lib"
        cmake_dir = lib_dir / "cmake"
        cmake_dir.mkdir(parents=True)
        shutil.copy2(package_init, package_dir / "__init__.py")
        shutil.copy2(extension, package_dir / extension.name)

        sys.path.insert(0, tmp)
        try:
            import pyyggdrasil

            assert pyyggdrasil.__all__ == [
                "__version__",
                "cmake_dirs",
                "include_dir",
                "library_dirs",
                "native_prefix",
                "execution",
            ]
            assert pyyggdrasil.__version__ != ""
            assert pyyggdrasil.native_prefix() == package_dir
            assert pyyggdrasil.include_dir() == package_dir / "include"
            assert pyyggdrasil.library_dirs() == (lib_dir,)
            assert pyyggdrasil.cmake_dirs() == (cmake_dir,)

            import pyyggdrasil.execution as execution

            assert execution is pyyggdrasil.execution

            source_root = Path(tmp) / "source-tree"
            source_package_dir = source_root / "python" / "src" / "pyyggdrasil"
            source_include_dir = source_root / "include" / "yggdrasil"
            source_cmake_dir = source_root / "lib64" / "cmake"
            source_package_dir.mkdir(parents=True)
            source_include_dir.mkdir(parents=True)
            source_cmake_dir.mkdir(parents=True)
            (source_root / "pyproject.toml").write_text(
                "[project]\nname = 'pyyggdrasil'\n", encoding="utf-8"
            )

            original_file = pyyggdrasil.__file__
            try:
                pyyggdrasil.__file__ = str(source_package_dir / "__init__.py")
                assert pyyggdrasil.native_prefix() == source_root
                assert pyyggdrasil.include_dir() == source_root / "include"
                assert pyyggdrasil.library_dirs() == (source_root / "lib64",)
                assert pyyggdrasil.cmake_dirs() == (source_cmake_dir,)
            finally:
                pyyggdrasil.__file__ = original_file

            for name in pyyggdrasil.__all__:
                assert hasattr(pyyggdrasil, name)

            assert "worker threads" in pyyggdrasil.execution.ExecutionContext.__doc__
            assert (
                "maximum thread count"
                in pyyggdrasil.execution.ExecutionContext.max_num_threads.__doc__
            )
            assert "worker thread count" in pyyggdrasil.execution.ExecutionContext.num_threads.__doc__

            max_num_threads = pyyggdrasil.execution.ExecutionContext.max_num_threads()
            assert max_num_threads >= 1
            context = pyyggdrasil.execution.ExecutionContext(1)
            assert context.num_threads == 1
            assert repr(context) == "ExecutionContext(num_threads=1)"
            with pyyggdrasil.execution.ExecutionContext(1) as active:
                assert active.num_threads == 1

            try:
                pyyggdrasil.execution.ExecutionContext(0)
            except ValueError as error:
                assert "num_threads must be at least 1" in str(error)
            else:
                raise AssertionError("ExecutionContext(0) did not fail")

            try:
                pyyggdrasil.execution.ExecutionContext(max_num_threads + 1)
            except ValueError as error:
                assert "Requested " in str(error)
                assert " are available by default." in str(error)
            else:
                raise AssertionError("ExecutionContext(max_num_threads + 1) did not fail")
        finally:
            sys.path.remove(tmp)


if __name__ == "__main__":
    main()
