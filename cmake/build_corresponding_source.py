from __future__ import annotations

import argparse
import gzip
import json
import shutil
import sys
import tarfile
import tempfile
from pathlib import Path, PurePosixPath


def _source_names(manifest: Path) -> list[str]:
    document = json.loads(manifest.read_text())
    if document.get("schema_version") != 1 or not isinstance(document.get("sources"), dict):
        raise ValueError("invalid source manifest")

    names = sorted(document["sources"])
    if any(Path(name).name != name or name in {".", ".."} for name in names):
        raise ValueError("source names must be plain path components")
    return names


def _normalize(info: tarfile.TarInfo) -> tarfile.TarInfo:
    info.uid = 0
    info.gid = 0
    info.uname = ""
    info.gname = ""
    info.mtime = 0
    if info.isdir():
        info.mode = 0o755
    elif info.issym():
        info.mode = 0o777
    else:
        info.mode = 0o755 if info.mode & 0o111 else 0o644
    return info


def _extract_sdist(archive: tarfile.TarFile, destination: Path) -> None:
    for member in archive.getmembers():
        path = PurePosixPath(member.name)
        if path.is_absolute() or ".." in path.parts or not (member.isfile() or member.isdir()):
            raise ValueError(f"unsafe source-distribution member: {member.name}")
    if sys.version_info >= (3, 12):
        archive.extractall(destination, filter="fully_trusted")
    else:
        archive.extractall(destination)


def build_bundle(*, manifest: Path, sdist: Path, sources: Path, output: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="pyyggdrasil-sources-") as temporary_directory:
        temporary = Path(temporary_directory)
        with tarfile.open(sdist) as archive:
            _extract_sdist(archive, temporary)

        roots = [path for path in temporary.iterdir() if path.is_dir()]
        if len(roots) != 1:
            raise ValueError("source distribution must contain exactly one root directory")

        bundle = roots[0].with_name(f"{roots[0].name}-corresponding-source")
        roots[0].rename(bundle)
        upstream = bundle / "upstream"
        upstream.mkdir()

        for name in _source_names(manifest):
            source = sources / name / "src" / name
            if not source.is_dir():
                raise FileNotFoundError(f"downloaded source is missing: {source}")
            shutil.copytree(source, upstream / name, symlinks=True, ignore=shutil.ignore_patterns(".git"))

        output.parent.mkdir(parents=True, exist_ok=True)
        with output.open("wb") as compressed_file:
            with gzip.GzipFile(filename="", mode="wb", fileobj=compressed_file, mtime=0) as compressed:
                with tarfile.open(fileobj=compressed, mode="w") as archive:
                    archive.add(bundle, arcname=bundle.name, filter=_normalize)


def main() -> None:
    parser = argparse.ArgumentParser(description="Bundle Yggdrasil and its exact upstream sources.")
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--sdist", type=Path, required=True)
    parser.add_argument("--sources", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    build_bundle(manifest=arguments.manifest, sdist=arguments.sdist, sources=arguments.sources, output=arguments.output)


if __name__ == "__main__":
    main()
