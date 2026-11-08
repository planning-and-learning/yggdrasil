# Copyright (C) 2026 Dominik Drexler
# SPDX-License-Identifier: GPL-3.0-or-later

import gc
import math
from collections.abc import Iterable
from typing import Any, cast

import pytest

import pyyggdrasil
from pyyggdrasil import database


def columns(*labels: int) -> list[database.ColumnIndex]:
    return [database.ColumnIndex(label) for label in labels]


def typed(labels: Iterable[database.ColumnIndex], types: Iterable[database.ColumnType]) -> list[database.ColumnLayout]:
    return [database.ColumnLayout(label, column_type) for label, column_type in zip(labels, types, strict=True)]


def layouts(*labels: int) -> list[database.ColumnLayout]:
    return typed(columns(*labels), [database.ColumnType.UINT32] * len(labels))


def test_relation_rows_and_validation() -> None:
    assert database is pyyggdrasil.database
    exported: list[str] = vars(database)["__all__"]  # the generated type hints do not list __all__
    assert all(hasattr(database, name) for name in exported)
    relation = database.Relation(columns(10, 20))
    labels = relation.columns()
    assert isinstance(labels, database.ColumnIndices)
    assert len(labels) == 2
    assert tuple(labels) == tuple(columns(10, 20))
    assert all(type(label) is database.ColumnIndex for label in labels)
    assert labels[-1] == database.ColumnIndex(20)
    assert labels[-2] == database.ColumnIndex(10)
    for index in (-3, 2):
        with pytest.raises(IndexError):
            _ = labels[index]
    with pytest.raises(TypeError):  # read-only: no __setitem__ in the type hints either
        cast("Any", labels)[0] = 7
    assert relation.arity() == 2
    assert relation.empty()
    assert relation.insert([3, 4]) == relation.insert([3, 4])
    assert not hasattr(relation, "at")  # a builder's rows are read through a repository
    repository = database.RelationRepository()
    view = repository.view(relation)
    assert isinstance(view, database.BorrowedRelation)
    row = view.at(0)
    assert isinstance(row, database.RelationRow)
    assert len(relation) == 1
    assert not relation.empty()
    assert tuple(row) == (3, 4)
    assert tuple(view[0]) == tuple(view[-1]) == (3, 4)
    for index in (-2, 1):
        with pytest.raises(IndexError):
            _ = view[index]
    assert row[-1] == 4
    assert row[-2] == 3
    for index in (-3, 2):
        with pytest.raises(IndexError):
            _ = row[index]
    with pytest.raises(IndexError):
        view.at(1)
    with pytest.raises(TypeError):  # read-only: no __setitem__ in the type hints either
        cast("Any", row)[0] = 7
    with pytest.raises(ValueError, match="Relation: row arity does not match schema"):
        relation.insert([1])
    with pytest.raises(ValueError, match="Columns: duplicate column label"):
        database.Relation(columns(1, 1))

    for value in range(5000):
        relation.insert([value, 0])
    del relation, view, repository
    gc.collect()
    assert tuple(row) == (3, 4)
    del row
    gc.collect()
    assert tuple(labels) == tuple(columns(10, 20))


def test_nullary_relation() -> None:
    relation = database.Relation()
    assert relation.arity() == 0
    assert len(relation) == 0
    assert isinstance(relation.columns(), database.ColumnIndices)
    assert tuple(relation.columns()) == ()
    repository = database.RelationRepository()
    view = repository.view(relation)
    assert list(view) == []
    relation.insert([])
    relation.insert([])
    assert len(relation) == 1
    row = view.at(0)
    assert tuple(row) == ()
    assert [tuple(row) for row in view] == [()]
    for index in (-1, 0):
        with pytest.raises(IndexError):
            _ = row[index]


def test_pooled_rows_keep_their_owners_alive() -> None:
    repository = database.RelationRepository()
    relation = repository.get_relation_builder(layouts(0, 1))
    assert isinstance(relation, database.Relation)
    relation.insert([3, 4])
    row = repository.view(relation).at(0)
    labels = relation.columns()
    assert isinstance(labels, database.ColumnIndices)
    del relation
    gc.collect()

    other = repository.get_relation_builder(layouts(10, 20))
    assert other.empty()
    other.insert([5, 6])
    assert tuple(row) == (3, 4)
    del other
    gc.collect()
    with pytest.raises(ValueError, match="Columns: duplicate column label"):
        repository.get_relation_builder(layouts(10, 10))
    reused = repository.get_relation_builder(layouts(30, 40))
    assert reused.empty()
    reused.insert([7, 8])
    assert tuple(row) == (3, 4)
    del reused, repository
    gc.collect()
    assert tuple(row) == (3, 4)
    assert tuple(labels) == tuple(columns(0, 1))


def test_iterators_keep_pooled_rows_alive() -> None:
    repository = database.RelationRepository()
    relation = repository.get_relation_builder(layouts(0, 1))
    relation.insert([3, 4])
    relation.insert([5, 6])
    rows = iter(repository.view(relation))
    assert iter(rows) is rows
    del relation, repository
    gc.collect()

    row = next(rows)
    assert tuple(row) == (3, 4)
    assert [tuple(other) for other in rows] == [(5, 6)]
    with pytest.raises(StopIteration):
        next(rows)
    del rows
    gc.collect()
    assert tuple(row) == (3, 4)

    values = iter(row)
    assert iter(values) is values
    del row
    gc.collect()
    assert list(values) == [3, 4]
    with pytest.raises(StopIteration):
        next(values)


def test_interned_relations_use_typed_identity_and_independent_storage() -> None:
    repository = database.RelationRepository()
    builder = database.Relation(columns(10, 20))
    builder.insert([3, 4])
    builder.insert([5, 6])
    result = repository.insert(builder)
    assert isinstance(result, tuple)
    relation, created = result
    duplicate, duplicate_created = repository.insert(builder)
    assert created is True
    assert duplicate_created is False
    del result
    assert isinstance(relation, database.RelationView)
    assert isinstance(relation.index(), database.RelationIndex)
    assert relation == duplicate
    assert hash(relation) == hash(duplicate)
    assert len({relation, duplicate}) == 1
    assert len(repository) == 1
    assert not hasattr(relation, "insert")
    assert not hasattr(relation, "clear")

    other_repository = database.RelationRepository()
    assert other_repository.insert(builder)[0] != relation
    assert repository.rename(relation, columns(10, 20)) == relation
    with pytest.raises(ValueError, match="Repository: rename requires a source in this repository"):
        other_repository.rename(relation, columns(30, 40))

    builder.clear()
    builder.insert([8, 9])
    assert [tuple(row) for row in relation] == [(3, 4), (5, 6)]
    assert [tuple(row) for row in repository.view(builder)] == [(8, 9)]
    assert relation.arity() == 2
    assert not relation.empty()
    assert tuple(relation[-1]) == (5, 6)
    for index in (-3, 2):
        with pytest.raises(IndexError):
            _ = relation[index]
    with pytest.raises(IndexError):
        relation.at(2)

    renamed = repository.rename(relation, columns(30, 40))
    assert tuple(renamed.columns()) == tuple(columns(30, 40))
    assert [tuple(row) for row in renamed] == [(3, 4), (5, 6)]
    assert renamed != relation
    assert repository.rename(renamed, columns(10, 20)) == relation
    with pytest.raises(ValueError, match="Columns: renaming must preserve arity"):
        repository.rename(relation, columns(30))
    with pytest.raises(ValueError, match="Repository: a rename gives distinct labels"):
        repository.rename(relation, columns(30, 30))

    # Every borrowed layer must keep the repository alive, without keeping the
    # mutable builder alive or depending on its buffers.
    row = relation.at(0)
    labels = renamed.columns()
    assert isinstance(labels, database.ColumnIndices)
    assert all(type(label) is database.ColumnIndex for label in labels)
    rows = iter(renamed)
    indexed_row = relation[1]
    del relation, duplicate, renamed, repository, other_repository, builder
    gc.collect()
    assert tuple(row) == (3, 4)
    assert tuple(labels) == tuple(columns(30, 40))
    assert [tuple(other) for other in rows] == [(3, 4), (5, 6)]
    assert tuple(indexed_row) == (5, 6)
    del row, indexed_row, rows
    gc.collect()
    assert tuple(labels) == tuple(columns(30, 40))


def test_interned_nullary_relations_and_repository_reset() -> None:
    repository = database.RelationRepository()
    builder = database.Relation()
    false = repository.insert(builder)[0]
    builder.insert([])
    true = repository.insert(builder)[0]
    assert false.empty()
    assert list(false) == []
    assert [tuple(row) for row in true] == [()]
    assert false != true
    assert len(repository) == 2
    del false, true
    repository.clear()
    assert len(repository) == 0
    assert [tuple(row) for row in repository.insert(builder)[0]] == [()]


def test_interning_ignores_row_insertion_order() -> None:
    repository = database.RelationRepository()
    seed = database.Relation(columns(10, 20))
    seed.insert([9, 8])
    repository.insert(seed)[0]

    builder = database.Relation(columns(10, 20))
    builder.insert([3, 4])
    builder.insert([9, 8])
    forward = repository.insert(builder)[0]
    builder.clear()
    builder.insert([9, 8])
    builder.insert([3, 4])
    builder.insert([3, 4])
    reverse = repository.insert(builder)[0]

    assert forward == reverse
    assert forward.index() == reverse.index()
    assert hash(forward) == hash(reverse)
    assert len(repository) == 2
    # Row IDs follow the repository's row-interning history; canonical
    # relations enumerate the sorted IDs, independent of builder order.
    assert [tuple(row) for row in forward] == [(9, 8), (3, 4)]
    assert [tuple(row) for row in reverse] == [(9, 8), (3, 4)]


def test_copy_and_assign_remap_relations_and_retain_unpacked_owner() -> None:
    source_repository = database.RelationRepository()
    target_repository = database.RelationRepository()
    builder = database.Relation(columns(7, 3))
    builder.insert([4, 5])
    source, created = source_repository.insert(builder)
    assert created is True
    target_result = target_repository.copy(source)
    target, copied = target_result
    assert copied is True
    assert target_repository.copy(source) == (target, False)
    assert target_repository.copy(target) == (target, False)
    assert tuple(target.columns()) == tuple(columns(7, 3))
    output = database.Relation(columns(3, 7))
    output.insert([99, 88])
    assert database.assign(output, target) is output
    assert tuple(output.columns()) == tuple(columns(7, 3))
    assert [tuple(row) for row in target_repository.view(output)] == [(4, 5)]
    assert database.assign(output, target_repository.view(output)) is output
    assert tuple(output.columns()) == tuple(columns(7, 3))
    assert [tuple(row) for row in target_repository.view(output)] == [(4, 5)]
    unary = database.Relation(columns(99))
    unary.insert([77])
    assert database.assign(unary, target_repository.view(output)) is unary
    assert tuple(unary.columns()) == tuple(columns(7, 3))
    assert [tuple(row) for row in target_repository.view(unary)] == [(4, 5)]
    del target_result, source, source_repository, target_repository
    del builder, output, unary
    gc.collect()
    assert [tuple(row) for row in target] == [(4, 5)]
    assert not hasattr(database, "intern_relation")


def test_mixed_column_types_and_float_canonicalization() -> None:
    types = [database.ColumnType.BOOL, database.ColumnType.FLOAT64, database.ColumnType.INT32]
    relation = database.Relation(typed(columns(10, 20, 30), types))
    assert [relation.columns().type(i) for i in range(3)] == types
    first = relation.insert([True, -0.0, -7])
    assert relation.insert([True, 0.0, -7]) == first
    repository = database.RelationRepository()
    row = repository.view(relation)[first]
    assert tuple(row) == (True, 0.0, -7)
    assert [type(value) for value in row] == [bool, float, int]
    nan = relation.insert([False, float("nan"), 9])
    assert relation.insert([False, -float("nan"), 9]) == nan
    value = repository.view(relation)[nan][1]
    assert isinstance(value, float)
    assert math.isnan(value)
    interned, _ = repository.insert(relation)
    renamed = repository.rename(interned, columns(1, 2, 3))
    assert [renamed.columns().type(i) for i in range(3)] == types
    output = database.Relation()
    database.assign(output, renamed)
    assert tuple(repository.view(output)[0]) == (True, 0.0, -7)
    with pytest.raises(ValueError, match="Columns: duplicate column label"):
        database.Relation(typed(columns(1, 1), [database.ColumnType.BOOL] * 2))
    del relation, interned, renamed, repository
    gc.collect()
    assert tuple(row) == (True, 0.0, -7)


def test_operations_take_plans_outputs_and_workspaces() -> None:
    repository = database.RelationRepository()
    lhs = database.Relation(columns(0, 1))
    rhs = database.Relation(columns(1, 2))
    for row in ([1, 2], [3, 4]):
        lhs.insert(row)
    rhs.insert([2, 5])
    left, right = repository.view(lhs), repository.insert(rhs)[0]
    workspace = database.Workspace()

    join = database.JoinPlan(left.columns(), right.columns())
    joined = database.Relation(join.output_columns())
    database.join(left, right, join, joined, workspace)
    assert [tuple(row) for row in repository.view(joined)] == [(1, 2, 5)]
    semi = database.Relation(join.lhs_columns())
    database.semijoin(left, right, join, semi, workspace)
    assert [tuple(row) for row in repository.view(semi)] == [(1, 2)]
    database.antijoin(left, right, join, semi, workspace)
    assert [tuple(row) for row in repository.view(semi)] == [(3, 4)]

    projection = database.ProjectionPlan(left.columns(), columns(1))
    projected = database.Relation(projection.output_columns())
    database.project(left, projection, projected, workspace)
    assert sorted(tuple(row) for row in repository.view(projected)) == [(2,), (4,)]

    other = database.Relation(columns(0, 1))
    other.insert([3, 4])
    other.insert([7, 7])
    sets = database.SetPlan(left.columns(), other.columns())
    union = database.Relation(sets.output_columns())
    database.union(left, repository.view(other), sets, union)
    assert len(union) == 3
    database.difference(left, repository.view(other), sets, union)
    assert [tuple(row) for row in repository.view(union)] == [(1, 2)]

    equal = database.Relation(other.columns())
    database.select_equal_columns(repository.view(other), database.SelectEqualPlan(other.columns(), *columns(0, 1)), equal)
    assert [tuple(row) for row in repository.view(equal)] == [(7, 7)]
    value = database.SelectValuePlan(other.columns(), database.ColumnIndex(0), database.ColumnType.UINT32, 3)
    database.select_equal_value(repository.view(other), value, equal)
    assert [tuple(row) for row in repository.view(equal)] == [(3, 4)]

    with pytest.raises(ValueError, match="Relational operation: input schemas must have the same column order"):
        database.SetPlan(left.columns(), right.columns())
    with pytest.raises(ValueError, match="output aliases an input"):
        database.select_equal_columns(repository.view(equal), database.SelectEqualPlan(equal.columns(), *columns(0, 1)), equal)
    with pytest.raises(ValueError, match="columns do not match the plan"):
        database.join(left, right, join, database.Relation(), workspace)
