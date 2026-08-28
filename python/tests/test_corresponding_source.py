from __future__ import annotations

import hashlib
import json
import subprocess
import sys
import tarfile
from pathlib import Path


def _write_sdist(path: Path) -> None:
    source = path.parent / "pyyggdrasil-1.2.3"
    source.mkdir()
    (source / "LICENSE").write_text("license")
    with tarfile.open(path, "w:gz") as archive:
        archive.add(source, arcname=source.name)


def test_corresponding_source_bundle_is_complete_and_reproducible(tmp_path: Path) -> None:
    manifest = tmp_path / "SOURCES.json"
    manifest.write_text(json.dumps({"schema_version": 1, "sources": {"alpha": {}, "beta": {}}}))
    sdist = tmp_path / "pyyggdrasil-1.2.3.tar.gz"
    _write_sdist(sdist)

    sources = tmp_path / "sources"
    for name in ("alpha", "beta"):
        source = sources / name / "src" / name
        source.mkdir(parents=True)
        (source / f"{name}.txt").write_text(name)
        executable = source / "configure"
        executable.write_text("#!/bin/sh\n")
        executable.chmod(0o700)
        (source / ".git").mkdir()
        (source / ".git" / "config").write_text("not source")

    outputs = [tmp_path / "first.tar.gz", tmp_path / "second.tar.gz"]
    script = Path(__file__).parents[2] / "cmake" / "build_corresponding_source.py"
    for index, output in enumerate(outputs):
        subprocess.run(
            [
                sys.executable,
                str(script),
                "--manifest",
                str(manifest),
                "--sdist",
                str(sdist),
                "--sources",
                str(sources),
                "--output",
                str(output),
            ],
            check=True,
        )
        if index == 0:
            (sources / "alpha" / "src" / "alpha").chmod(0o700)
            (sources / "alpha" / "src" / "alpha" / "alpha.txt").chmod(0o600)

    assert hashlib.sha256(outputs[0].read_bytes()).digest() == hashlib.sha256(outputs[1].read_bytes()).digest()
    with tarfile.open(outputs[0]) as archive:
        names = set(archive.getnames())
        root = "pyyggdrasil-1.2.3-corresponding-source"
        assert archive.getmember(f"{root}/upstream/alpha/alpha.txt").mode == 0o644
        assert archive.getmember(f"{root}/upstream/alpha/configure").mode == 0o755
    assert f"{root}/LICENSE" in names
    assert f"{root}/upstream/alpha/alpha.txt" in names
    assert f"{root}/upstream/beta/beta.txt" in names
    assert all("/.git/" not in name for name in names)
