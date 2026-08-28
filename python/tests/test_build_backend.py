from collections.abc import Callable
from pathlib import Path
from typing import cast

import pytest

from python import pyyggdrasil_build_backend as backend


def _configure_and_install_dependencies() -> None:
    configure = cast(Callable[[], None], getattr(backend, "_configure_and_install_dependencies"))
    configure()


def test_native_build_cleans_generated_prefix_and_rejects_dirty_custom_prefix(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    def fake_which(name: str) -> str:
        return f"/usr/bin/{name}"

    def fake_run(*args: object, **kwargs: object) -> None:
        return None

    monkeypatch.setattr(backend, "ROOT_DIR", tmp_path)
    monkeypatch.setattr(backend, "YGGDRASIL_BUILD_DIR", tmp_path / "dependencies-build")
    monkeypatch.setattr(backend.shutil, "which", fake_which)
    monkeypatch.setattr(backend.subprocess, "run", fake_run)
    monkeypatch.delenv("YGGDRASIL_NATIVE_PREFIX", raising=False)
    monkeypatch.delenv("YGGDRASIL_BUILD_NATIVE", raising=False)

    stale_header = tmp_path / "dependencies-install/include/valla/valla.hpp"
    stale_header.parent.mkdir(parents=True)
    stale_header.touch()
    stale_stamp = backend.YGGDRASIL_BUILD_DIR / "valla/stamp"
    stale_stamp.mkdir(parents=True)

    _configure_and_install_dependencies()

    assert not stale_header.exists()
    assert not stale_stamp.exists()

    custom_prefix = tmp_path / "custom-prefix"
    custom_prefix.mkdir()
    (custom_prefix / "unrelated-file").touch()
    monkeypatch.setenv("YGGDRASIL_NATIVE_PREFIX", str(custom_prefix))

    with pytest.raises(RuntimeError, match="must be empty"):
        _configure_and_install_dependencies()
    assert (custom_prefix / "unrelated-file").is_file()

    monkeypatch.delenv("YGGDRASIL_NATIVE_PREFIX")
    backend.YGGDRASIL_BUILD_DIR.rmdir()
    protected_dir = tmp_path / "protected"
    protected_dir.mkdir()
    (protected_dir / "keep").touch()
    backend.YGGDRASIL_BUILD_DIR.symlink_to(protected_dir, target_is_directory=True)

    with pytest.raises(RuntimeError, match="refusing to clean symlinked"):
        _configure_and_install_dependencies()
    assert (protected_dir / "keep").is_file()
