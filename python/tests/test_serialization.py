from copy import deepcopy
from types import MappingProxyType

import pytest

import pyyggdrasil
import pyyggdrasil.serialization as serialization
from pyyggdrasil.serialization.table import Row, render_table


def test_serialization_submodule_is_public() -> None:
    assert serialization is pyyggdrasil.serialization
    assert serialization.table.render_table is render_table


@pytest.mark.parametrize("aligned", [False, True])
def test_render_table_preserves_rows_and_adds_references(aligned: bool) -> None:
    rows: list[Row] = [
        {"id": 7, "label": "001", "items": ["a", "b"], "expression": {"kind": "constant", "value": True}},
        {"id": 8, "note": "café"},
    ]
    original = deepcopy(rows)

    rendered = render_table((MappingProxyType(row) for row in rows), prefix="x", aligned=aligned)

    assert rows == original
    lines = [[cell.strip() for cell in line.split("|")] for line in rendered.splitlines()]
    assert len(lines) == 4
    assert lines[0] == ["", "", "", "", "expression", "", ""]
    assert lines[1] == ["id", "id", "label", "items", "kind", "value", "note"]
    assert lines[2] == ["x0", "7", "001", "[a,b]", "constant", "True", ""]
    assert lines[3] == ["x1", "8", "", "", "", "", "café"]


@pytest.mark.parametrize("aligned", [False, True])
def test_recursive_headers_keep_groups_together_and_lists_intact(aligned: bool) -> None:
    rows: list[Row] = [
        {
            "name": "grasp",
            "condition": {"variables": [{"name": "?h"}], "expression": {"kind": "constant", "value": 1}},
            "effect": {"kind": "empty", "value": None},
        },
        {"name": "leave", "condition": {"extra": False}, "empty": {}},
    ]

    lines = [[cell.strip() for cell in line.split("|")] for line in render_table(rows, prefix="A", aligned=aligned).splitlines()]

    assert len(lines) == 5
    assert lines[0] == ["", "", "condition", "", "", "", "effect", ""]
    assert lines[1] == ["", "", "", "expression", "", "", "", ""]
    assert lines[2] == ["id", "name", "variables", "kind", "value", "extra", "kind", "value"]
    assert lines[3] == ["A0", "grasp", '[{"name":"?h"}]', "constant", "1", "", "empty", ""]
    assert lines[4] == ["A1", "leave", "", "", "", "False", "", ""]


def test_compact_and_aligned_layouts() -> None:
    rows: list[Row] = [{"id": "a", "items": ["x", "yy"]}, {"id": "long", "items": []}]
    assert render_table(rows) == "id|items\na|[x,yy]\nlong|[]"
    assert render_table(rows, aligned=True) == "id   | items\na    | [x,yy]\nlong | []"


@pytest.mark.parametrize("aligned", [False, True])
def test_escaping_preserves_one_physical_row_and_scalar_text(aligned: bool) -> None:
    rows: list[Row] = [{"text\nlabel": "001|café\nnext\\n\r\t", "missing": None, "enabled": True, "cost": 1.5}]
    lines = render_table(rows, prefix="s|", aligned=aligned).splitlines()
    assert len(lines) == 2
    assert r"text\nlabel" in lines[0]
    assert r"s\|0" in lines[1]
    assert r"001\|café\nnext\\n\r\t" in lines[1]
    assert lines[1].endswith("1.5")


def test_empty_singleton_and_ambiguous_lists() -> None:
    rows: list[Row] = [
        {"items": []},
        {"items": ["a"]},
        {"items": ["a", "b"]},
        {"items": [1, 2.5, True]},
        {"items": ["a,b", "c"]},
        {"items": [""]},
        {"items": [" a "]},
        {"items": [["a", "b"], ["c"]]},
        {"items": [None]},
    ]
    assert render_table(rows).splitlines() == [
        "items", "[]", "[a]", "[a,b]", "[1,2.5,True]", '["a,b","c"]', '[""]', '[" a "]', '[["a","b"],["c"]]', "[null]",
    ]


def test_empty_rows_keep_their_references() -> None:
    assert render_table([{}, {}], prefix="s") == "id\ns0\ns1"
    assert render_table([{}, {}]) == ""


@pytest.mark.parametrize("prefix", [None, "s"])
@pytest.mark.parametrize("aligned", [False, True])
def test_render_empty_table(prefix: str | None, aligned: bool) -> None:
    assert render_table(iter(()), prefix=prefix, aligned=aligned) == ""
