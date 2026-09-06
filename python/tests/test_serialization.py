from copy import deepcopy
from types import MappingProxyType

import pytest
from tabulate import tabulate

import pyyggdrasil
import pyyggdrasil.serialization as serialization
from pyyggdrasil.serialization import EnumEntry, Row, render_table


def test_serialization_submodule_is_public() -> None:
    assert serialization is pyyggdrasil.serialization


def test_render_table_preserves_rows_and_adds_references() -> None:
    rows: list[Row] = [
        {"id": 7, "label": "001", "items": ["a", "b"], "expression": {"kind": "@0", "value": True}},
        {"id": 8, "note": "café"},
    ]
    original = deepcopy(rows)

    rendered = render_table((MappingProxyType(row) for row in rows), prefix="x")

    assert rows == original
    lines = rendered.splitlines()
    assert lines[0].split() == ["id", "label", "items", "expression", "note"]
    assert lines[2].split() == ["x0", "7", "001", '["a","b"]', '{"kind":"@0","value":true}']
    assert lines[3].split() == ["x1", "8", "café"]


@pytest.mark.parametrize("tablefmt", ["simple", "github"])
def test_render_table_keeps_tabulate_scalar_behavior(tablefmt: str) -> None:
    rows: list[Row] = [{"text": "001|café\nnext", "missing": None, "enabled": True, "cost": 1.5}]

    assert render_table(rows, tablefmt=tablefmt) == tabulate(
        rows, headers="keys", tablefmt=tablefmt, disable_numparse=True,
    )

    entries: list[EnumEntry] = [{"ref": "@0", "id": 1, "name": "EXTERNAL"}]
    assert render_table(entries, tablefmt=tablefmt) == tabulate(
        entries, headers="keys", tablefmt=tablefmt, disable_numparse=True,
    )


@pytest.mark.parametrize("prefix", [None, "s"])
def test_render_empty_table(prefix: str | None) -> None:
    assert render_table(iter(()), prefix=prefix) == ""
