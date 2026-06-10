import ast
from importlib.metadata import PackageNotFoundError, version
from pathlib import Path
from typing import Optional, Tuple

try:
    import tomllib
except ModuleNotFoundError:  # pragma: no cover - Python < 3.11
    tomllib = None

import sys

from ._pyyggdrasil import execution as execution

sys.modules[__name__ + ".execution"] = execution


def _has_yggdrasil_headers(path: Path) -> bool:
    return (path / "include" / "yggdrasil").is_dir()


def _read_pyproject_version(pyproject: Path) -> Optional[str]:
    if tomllib is not None:
        with pyproject.open("rb") as f:
            return tomllib.load(f).get("project", {}).get("version")

    in_project_table = False
    for line in pyproject.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            in_project_table = stripped == "[project]"
            continue
        if in_project_table and stripped.startswith("version"):
            raw_value = stripped.split("=", maxsplit=1)[1].strip()
            try:
                value = ast.literal_eval(raw_value)
            except (SyntaxError, ValueError):
                return raw_value.strip("\"").strip("'")
            return value if isinstance(value, str) else None

    return None


def _source_version() -> str:
    for parent in Path(__file__).resolve().parents:
        pyproject = parent / "pyproject.toml"
        if pyproject.exists():
            source_version = _read_pyproject_version(pyproject)
            if source_version:
                return source_version

    return "0.0.0"


try:
    __version__ = version("pyyggdrasil")
except PackageNotFoundError:
    __version__ = _source_version()


def native_prefix() -> Path:
    """Return the directory containing bundled native headers and libraries."""
    package_dir = Path(__file__).resolve().parent
    if _has_yggdrasil_headers(package_dir):
        return package_dir
    for parent in package_dir.parents:
        if (parent / "pyproject.toml").exists() and _has_yggdrasil_headers(parent):
            return parent

    return package_dir


def include_dir() -> Path:
    """Return the bundled C++ header include directory."""
    return native_prefix() / "include"


def library_dirs() -> Tuple[Path, ...]:
    """Return existing bundled native library directories."""
    prefix = native_prefix()
    return tuple(
        path for path in (prefix / "lib", prefix / "lib64") if path.is_dir()
    )


def cmake_dirs() -> Tuple[Path, ...]:
    """Return existing bundled CMake package directories."""
    return tuple(
        path / "cmake" for path in library_dirs() if (path / "cmake").is_dir()
    )


def cmake_prefix() -> Path:
    """Return the prefix to put on CMAKE_PREFIX_PATH to find yggdrasil and its
    bundled dependencies via find_package."""
    return native_prefix()


def cmake_dir() -> Path:
    """Return the directory containing yggdrasilConfig.cmake."""
    for cmake_packages_dir in cmake_dirs():
        candidate = cmake_packages_dir / "yggdrasil"
        if (candidate / "yggdrasilConfig.cmake").is_file():
            return candidate

    raise FileNotFoundError(
        "yggdrasilConfig.cmake not found under "
        f"{[str(path) for path in cmake_dirs()] or native_prefix()}; "
        "the installed pyyggdrasil is too old or incomplete."
    )


__all__ = [
    "__version__",
    "cmake_dir",
    "cmake_dirs",
    "cmake_prefix",
    "include_dir",
    "library_dirs",
    "native_prefix",
    "execution",
]
