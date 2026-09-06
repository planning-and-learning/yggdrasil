"""Shared serialization snapshots and table rendering."""

import json
from collections.abc import Iterable, Mapping
from typing import TypeAlias, TypedDict

JSONValue: TypeAlias = None | bool | int | float | str | list["JSONValue"] | dict[str, "JSONValue"]
Row: TypeAlias = dict[str, JSONValue]


class Table(TypedDict):
    prefix: str
    rows: list[Row]


class EnumEntry(TypedDict):
    ref: str
    id: int
    name: str


def render_table(
    rows: Iterable[Mapping[str, object]],
    *,
    prefix: str | None = None,
    tablefmt: str = "simple",
) -> str:
    """Render rows with compact JSON cells and optional prefix-plus-index references."""
    from tabulate import tabulate

    cells = [
        {
            name: json.dumps(value, ensure_ascii=False, separators=(",", ":"))
            if isinstance(value, (list, dict)) else value
            for name, value in row.items()
        }
        for row in rows
    ]
    return tabulate(
        cells,
        headers="keys",
        tablefmt=tablefmt,
        disable_numparse=True,
        showindex=False if prefix is None else [f"{prefix}{index}" for index in range(len(cells))],
    )


__all__ = ["JSONValue", "Row", "Table", "EnumEntry", "render_table"]
