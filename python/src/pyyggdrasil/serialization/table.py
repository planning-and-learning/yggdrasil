"""Shared serialization snapshots and table rendering."""

import json
from collections.abc import Iterable, Iterator, Mapping
from typing import TypeAlias, TypedDict, cast

JSONValue: TypeAlias = None | bool | int | float | str | list["JSONValue"] | dict[str, "JSONValue"]
Row: TypeAlias = dict[str, JSONValue]


class Table(TypedDict):
    prefix: str
    rows: list[Row]


def _flatten(
    row: Mapping[str, object], path: tuple[str, ...] = (),
) -> Iterator[tuple[tuple[str, ...], object]]:
    for name, value in row.items():
        column = (*path, name)
        if isinstance(value, dict):
            yield from _flatten(cast(dict[str, object], value), column)
        else:
            yield column, json.dumps(value, ensure_ascii=False, separators=(",", ":")) if isinstance(value, list) else value


def render_table(
    rows: Iterable[Mapping[str, object]],
    *,
    prefix: str | None = None,
    tablefmt: str = "presto",
) -> str:
    """Expand dictionaries into grouped headers; render lists as compact JSON cells.

    Leaf labels share the bottom header row. Empty dictionaries have no columns.
    """
    from tabulate import tabulate

    cells = [dict(_flatten(row)) for row in rows]
    columns = list(dict.fromkeys(column for row in cells for column in row))

    # Keep siblings together even when later rows introduce additional fields.
    order: dict[tuple[str, ...], int] = {}
    for column in columns:
        for depth in range(1, len(column) + 1):
            order.setdefault(column[:depth], len(order))
    columns.sort(key=lambda column: tuple(order[column[:depth]] for depth in range(1, len(column) + 1)))

    height = max(map(len, columns), default=1)
    groups: set[tuple[str, ...]] = set()
    headers: list[str] = []
    for column in columns:
        labels = [""] * height
        for depth, name in enumerate(column[:-1]):
            group = column[:depth + 1]
            if group not in groups:
                labels[depth] = name
                groups.add(group)
        labels[-1] = column[-1]
        headers.append("\n".join(labels))

    return tabulate(
        [[row.get(column) for column in columns] for row in cells],
        headers=headers,
        tablefmt=tablefmt,
        disable_numparse=True,
        showindex=False if prefix is None else [f"{prefix}{index}" for index in range(len(cells))],
    )


__all__ = ["JSONValue", "Row", "Table", "render_table"]
