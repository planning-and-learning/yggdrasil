from pathlib import Path
from importlib.metadata import PackageNotFoundError, version

def _has_ygg_headers(path: Path) -> bool:
    return (path / "include" / "ygg").is_dir()


def _source_version() -> str:
    for parent in Path(__file__).resolve().parents:
        pyproject = parent / "pyproject.toml"
        if not pyproject.exists():
            continue

        for line in pyproject.read_text(encoding="utf-8").splitlines():
            if line.startswith("version"):
                return line.split("=", maxsplit=1)[1].strip().strip("\"")

    return "0.0.0"


try:
    __version__ = version("pyyggdrasil")
except PackageNotFoundError:
    __version__ = _source_version()


def native_prefix() -> Path:
    package_dir = Path(__file__).resolve().parent
    if _has_ygg_headers(package_dir):
        return package_dir
    for parent in package_dir.parents:
        if (parent / "pyproject.toml").exists() and _has_ygg_headers(parent):
            return parent

    return package_dir
