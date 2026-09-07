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
            yield column, value


def _cell(value: object) -> str:
    if value is None:
        return ""
    if isinstance(value, list):
        values = cast(list[object], value)
        items = [str(item) for item in values]
        if items and all(isinstance(item, (str, int, float, bool)) for item in values) and all(
            item and item == item.strip() and not any(char in item for char in ',[]{}"\\|\r\n\t')
            for item in items
        ):
            text = ",".join(items)
        else:
            text = json.dumps(values, ensure_ascii=False, separators=(",", ":"))
    else:
        text = str(value)
    return text.replace("\\", r"\\").replace("|", r"\|").replace("\r", r"\r").replace("\n", r"\n").replace("\t", r"\t")


def render_table(
    rows: Iterable[Mapping[str, object]],
    *,
    prefix: str | None = None,
    aligned: bool = False,
) -> str:
    """Render pipe-separated columns, optionally padded to align their contents.

    Leaf labels share the bottom header row. Empty dictionaries have no columns.
    Simple lists use commas (a singleton has no brackets); empty, nested, or
    ambiguous lists use compact JSON. Backslashes, pipes, and line breaks are
    escaped. Neither layout adds decorative lines.
    """
    cells = [dict(_flatten(row)) for row in rows]
    if not cells:
        return ""
    columns = list(dict.fromkeys(column for row in cells for column in row))

    # Keep siblings together even when later rows introduce additional fields.
    order: dict[tuple[str, ...], int] = {}
    for column in columns:
        for depth in range(1, len(column) + 1):
            order.setdefault(column[:depth], len(order))
    columns.sort(key=lambda column: tuple(order[column[:depth]] for depth in range(1, len(column) + 1)))

    height = max(map(len, columns), default=1)
    groups: set[tuple[str, ...]] = set()
    headers: list[list[str]] = []
    for column in columns:
        labels = [""] * height
        for depth, name in enumerate(column[:-1]):
            group = column[:depth + 1]
            if group not in groups:
                labels[depth] = _cell(name)
                groups.add(group)
        labels[-1] = _cell(column[-1])
        headers.append(labels)

    body = [[_cell(row.get(column)) for column in columns] for row in cells]
    if prefix is not None:
        headers.insert(0, [""] * (height - 1) + ["id"])
        for index, row in enumerate(body):
            row.insert(0, _cell(f"{prefix}{index}"))
    if not headers:
        return ""
    grid = [list(row) for row in zip(*headers, strict=True)] + body
    if not aligned:
        return "\n".join("|".join(row) for row in grid)

    # ponytail: code-point widths; use terminal display widths if wide Unicode alignment is needed.
    widths = [max(map(len, column)) for column in zip(*grid, strict=True)]
    widths[-1] = 0  # No alignment padding after the last column.
    return "\n".join(
        " | ".join(cell.ljust(width) for cell, width in zip(row, widths, strict=True))
        for row in grid
    )


__all__ = ["JSONValue", "Row", "Table", "render_table"]
