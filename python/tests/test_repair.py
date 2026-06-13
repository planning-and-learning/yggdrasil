"""Unit tests for pyyggdrasil.repair (provider library-dir discovery)."""

import sys
import types

import pyyggdrasil.repair as repair


def _fake_provider(name, prefix, monkeypatch):
    monkeypatch.setitem(
        sys.modules, name, types.SimpleNamespace(native_prefix=lambda prefix=prefix: prefix)
    )


def test_provider_library_dirs_collects_lib_and_lib64(tmp_path, monkeypatch):
    prefix_a = tmp_path / "a"
    (prefix_a / "lib").mkdir(parents=True)
    (prefix_a / "lib64").mkdir()
    (prefix_a / "include").mkdir()  # must be ignored (not a lib* dir)
    prefix_b = tmp_path / "b"
    (prefix_b / "lib").mkdir(parents=True)

    _fake_provider("fake_a", prefix_a, monkeypatch)
    _fake_provider("fake_b", prefix_b, monkeypatch)

    dirs = repair.provider_library_dirs(["fake_a", "fake_b"])

    assert dirs == [prefix_a / "lib", prefix_a / "lib64", prefix_b / "lib"]
    # include/ and any non-directory are excluded.
    assert all(d.name.startswith("lib") and d.is_dir() for d in dirs)


def test_provider_library_dirs_empty_when_no_lib_dirs(tmp_path, monkeypatch):
    prefix = tmp_path / "p"
    (prefix / "include").mkdir(parents=True)
    _fake_provider("fake_empty", prefix, monkeypatch)

    assert repair.provider_library_dirs(["fake_empty"]) == []
