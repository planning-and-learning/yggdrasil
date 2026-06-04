import base64
import csv
import hashlib
import os
import platform
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

from scikit_build_core import build as scikit_build


ROOT_DIR = Path(__file__).resolve().parent.parent
YGGDRASIL_BUILD_DIR = ROOT_DIR / "dependencies-build"


def _native_prefix() -> Path:
    return Path(os.environ.get("YGGDRASIL_NATIVE_PREFIX", ROOT_DIR / "dependencies-install")).resolve()


def _build_type() -> str:
    return os.environ.get("YGGDRASIL_BUILD_TYPE", "Release")


def _num_jobs() -> int:
    return int(os.environ.get("YGGDRASIL_JOBS", "8"))


def _is_disabled(value: str) -> bool:
    return value.upper() in {"0", "FALSE", "OFF", "NO"}


def _configure_and_install_dependencies() -> None:
    if _is_disabled(os.environ.get("YGGDRASIL_BUILD_NATIVE", "ON")):
        return

    cmake = shutil.which("cmake")
    if cmake is None:
        raise RuntimeError("cmake is required to build pyyggdrasil")

    YGGDRASIL_BUILD_DIR.mkdir(parents=True, exist_ok=True)

    cmake_args = [
        cmake,
        "-S",
        str(ROOT_DIR / "src"),
        "-B",
        str(YGGDRASIL_BUILD_DIR),
        f"-DCMAKE_BUILD_TYPE={_build_type()}",
        f"-DCMAKE_INSTALL_PREFIX={_native_prefix()}",
        "-DCMAKE_INSTALL_LIBDIR=lib",
        f"-DPython_EXECUTABLE={sys.executable}",
    ]

    subprocess.run(cmake_args, cwd=ROOT_DIR, check=True)
    subprocess.run([cmake, "--build", str(YGGDRASIL_BUILD_DIR), f"-j{_num_jobs()}"], cwd=ROOT_DIR, check=True)
    subprocess.run([cmake, "--install", str(YGGDRASIL_BUILD_DIR)], cwd=ROOT_DIR, check=True)


def _prepend_cmake_args(*args: str) -> None:
    existing = os.environ.get("CMAKE_ARGS", "")
    os.environ["CMAKE_ARGS"] = " ".join([*args, existing]).strip()


def _prepare_native_build() -> None:
    _configure_and_install_dependencies()
    os.environ.setdefault("CMAKE_BUILD_PARALLEL_LEVEL", str(_num_jobs()))
    _prepend_cmake_args(
        f"-DYGGDRASIL_NATIVE_PREFIX={_native_prefix()}",
        "-DCMAKE_INSTALL_LIBDIR=lib",
    )


def _is_native_library(path: Path) -> bool:
    name = path.name
    return ".so" in name or name.endswith(".dylib") or name.endswith(".pyd")


def _strip_args() -> list[str]:
    if platform.system() == "Darwin":
        return ["-x"]
    return ["--strip-unneeded"]


def _patch_stub_text(text: str) -> str:
    text = text.replace("pyyggdrasil._pyyggdrasil.", "pyyggdrasil.")
    return text.replace("pyyggdrasil._pyyggdrasil", "pyyggdrasil")


def _fix_wheel_stubs(wheel_path: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="pyyggdrasil-wheel-") as tmp:
        wheel_root = Path(tmp) / "wheel"
        with zipfile.ZipFile(wheel_path) as wheel:
            wheel.extractall(wheel_root)

        def install_stub(path: Path, target: Path) -> None:
            package_dir = target.with_suffix("")
            if package_dir.is_dir():
                target = package_dir / "__init__.pyi"

            if target.exists():
                return

            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(_patch_stub_text(path.read_text(encoding="utf-8")), encoding="utf-8")

        private_stub_root = wheel_root / "pyyggdrasil" / "_pyyggdrasil"
        if private_stub_root.is_dir():
            public_stub_root = wheel_root / "pyyggdrasil"
            for path in sorted(private_stub_root.rglob("*.pyi")):
                install_stub(path, public_stub_root / path.relative_to(private_stub_root))
            shutil.rmtree(private_stub_root)

        for path in sorted(wheel_root.rglob("*.pyi")):
            path.write_text(_patch_stub_text(path.read_text(encoding="utf-8")), encoding="utf-8")

        _rewrite_record(wheel_root)

        replacement_path = wheel_path.with_suffix(".tmp")
        with zipfile.ZipFile(replacement_path, "w", compression=zipfile.ZIP_DEFLATED) as wheel:
            for path in sorted(wheel_root.rglob("*")):
                if path.is_file():
                    wheel.write(path, path.relative_to(wheel_root).as_posix())

        replacement_path.replace(wheel_path)


def _record_digest(path: Path) -> tuple[str, str]:
    content = path.read_bytes()
    digest = base64.urlsafe_b64encode(hashlib.sha256(content).digest()).rstrip(b"=").decode("ascii")
    return f"sha256={digest}", str(len(content))


def _rewrite_record(wheel_root: Path) -> None:
    record_files = list(wheel_root.glob("*.dist-info/RECORD"))
    if len(record_files) != 1:
        raise RuntimeError(f"expected exactly one RECORD file, found {len(record_files)}")

    record_file = record_files[0]
    rows = []
    for path in sorted(wheel_root.rglob("*")):
        if not path.is_file():
            continue

        relative_path = path.relative_to(wheel_root).as_posix()
        if path == record_file:
            rows.append((relative_path, "", ""))
        else:
            digest, size = _record_digest(path)
            rows.append((relative_path, digest, size))

    with record_file.open("w", newline="") as out:
        csv.writer(out).writerows(rows)


def _strip_wheel_native_libraries(wheel_path: Path) -> None:
    if _is_disabled(os.environ.get("YGGDRASIL_STRIP_WHEEL", "ON")):
        return

    strip = shutil.which("strip")
    if strip is None:
        return

    with tempfile.TemporaryDirectory(prefix="pyyggdrasil-wheel-") as tmp:
        wheel_root = Path(tmp) / "wheel"
        with zipfile.ZipFile(wheel_path) as wheel:
            wheel.extractall(wheel_root)

        for path in wheel_root.rglob("*"):
            if path.is_file() and _is_native_library(path):
                subprocess.run(
                    [strip, *_strip_args(), str(path)],
                    check=False,
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                )

        _rewrite_record(wheel_root)

        replacement_path = wheel_path.with_suffix(".tmp")
        with zipfile.ZipFile(replacement_path, "w", compression=zipfile.ZIP_DEFLATED) as wheel:
            for path in sorted(wheel_root.rglob("*")):
                if path.is_file():
                    wheel.write(path, path.relative_to(wheel_root).as_posix())

        replacement_path.replace(wheel_path)


def get_requires_for_build_wheel(config_settings=None):
    return scikit_build.get_requires_for_build_wheel(config_settings)


def get_requires_for_build_editable(config_settings=None):
    return scikit_build.get_requires_for_build_editable(config_settings)


def prepare_metadata_for_build_wheel(metadata_directory, config_settings=None):
    return scikit_build.prepare_metadata_for_build_wheel(metadata_directory, config_settings)


def build_wheel(wheel_directory, config_settings=None, metadata_directory=None):
    _prepare_native_build()
    wheel_filename = scikit_build.build_wheel(wheel_directory, config_settings, metadata_directory)
    wheel_path = Path(wheel_directory) / wheel_filename
    _fix_wheel_stubs(wheel_path)
    _strip_wheel_native_libraries(wheel_path)
    return wheel_filename


def build_editable(wheel_directory, config_settings=None, metadata_directory=None):
    _prepare_native_build()
    return scikit_build.build_editable(wheel_directory, config_settings, metadata_directory)


def build_sdist(sdist_directory, config_settings=None):
    return scikit_build.build_sdist(sdist_directory)
