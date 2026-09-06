from copy import deepcopy
from types import MappingProxyType

import pytest
from tabulate import tabulate

import pyyggdrasil
import pyyggdrasil.serialization as serialization
from pyyggdrasil.serialization import Row, render_table


def test_serialization_submodule_is_public() -> None:
    assert serialization is pyyggdrasil.serialization
    assert serialization.table.render_table is render_table


def test_render_table_preserves_rows_and_adds_references() -> None:
    rows: list[Row] = [
        {"id": 7, "label": "001", "items": ["a", "b"], "expression": {"kind": "constant", "value": True}},
        {"id": 8, "note": "café"},
    ]
    original = deepcopy(rows)

    rendered = render_table((MappingProxyType(row) for row in rows), prefix="x")

    assert rows == original
    lines = [[cell.strip() for cell in line.split("|")] for line in rendered.splitlines()]
    assert len(lines) == 5
    assert lines[0] == ["", "", "", "", "expression", "", ""]
    assert lines[1] == ["", "id", "label", "items", "kind", "value", "note"]
    assert lines[3] == ["x0", "7", "001", '["a","b"]', "constant", "True", ""]
    assert lines[4] == ["x1", "8", "", "", "", "", "café"]


def test_recursive_headers_keep_groups_together_and_lists_intact() -> None:
    rows: list[Row] = [
        {
            "name": "grasp",
            "condition": {"variables": [{"name": "?h"}], "expression": {"kind": "constant", "value": 1}},
            "effect": {"kind": "empty", "value": None},
        },
        {"name": "leave", "condition": {"extra": False}, "empty": {}},
    ]

    lines = [[cell.strip() for cell in line.split("|")] for line in render_table(rows, prefix="A").splitlines()]

    assert len(lines) == 6
    assert lines[0] == ["", "", "condition", "", "", "", "effect", ""]
    assert lines[1] == ["", "", "", "expression", "", "", "", ""]
    assert lines[2] == ["", "name", "variables", "kind", "value", "extra", "kind", "value"]
    assert lines[4] == ["A0", "grasp", '[{"name":"?h"}]', "constant", "1", "", "empty", ""]
    assert lines[5] == ["A1", "leave", "", "", "", "False", "", ""]


@pytest.mark.parametrize("tablefmt", ["presto", "simple", "github"])
def test_render_table_keeps_tabulate_scalar_behavior(tablefmt: str) -> None:
    rows: list[Row] = [{"text": "001|café\nnext", "missing": None, "enabled": True, "cost": 1.5}]

    assert render_table(rows, tablefmt=tablefmt) == tabulate(
        rows, headers="keys", tablefmt=tablefmt, disable_numparse=True,
    )


@pytest.mark.parametrize("prefix", [None, "s"])
def test_render_empty_table(prefix: str | None) -> None:
    assert render_table(iter(()), prefix=prefix) == ""
