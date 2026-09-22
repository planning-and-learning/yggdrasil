import gc

import pytest

import pyyggdrasil
from pyyggdrasil import database


def test_relation_rows_and_validation() -> None:
    assert database is pyyggdrasil.database
    assert database.__all__ == ["Relation", "RelationPool", "RelationPtr", "RelationRow"]
    relation = database.Relation([10, 20])
    assert relation.arity() == 2
    assert relation.empty()
    assert relation.insert([3, 4]) == relation.insert([3, 4])
    row = relation.at(0)
    assert isinstance(row, database.RelationRow)
    assert len(relation) == 1
    assert not relation.empty()
    assert tuple(row) == (3, 4)
    assert row[-1] == 4
    assert row[-2] == 3
    for index in (-3, 2):
        with pytest.raises(IndexError):
            _ = row[index]
    with pytest.raises(IndexError):
        relation.at(1)
    with pytest.raises(TypeError):
        row[0] = 7
    with pytest.raises(ValueError):
        relation.insert([1])
    with pytest.raises(ValueError):
        database.Relation([1, 1])

    for value in range(5000):
        relation.insert([value, 0])
    del relation
    gc.collect()
    assert tuple(row) == (3, 4)


def test_nullary_relation() -> None:
    relation = database.Relation()
    assert relation.arity() == 0
    assert len(relation) == 0
    assert list(relation) == []
    relation.insert([])
    relation.insert([])
    assert len(relation) == 1
    row = relation.at(0)
    assert tuple(row) == ()
    assert [tuple(row) for row in relation] == [()]
    for index in (-1, 0):
        with pytest.raises(IndexError):
            _ = row[index]


def test_pooled_rows_keep_their_owners_alive() -> None:
    pool = database.RelationPool()
    handle = pool.get_or_allocate([0, 1])
    assert isinstance(handle, database.RelationPtr)
    relation = handle.get()
    assert handle.get() is relation
    relation.insert([3, 4])
    row = relation.at(0)
    del relation, handle
    gc.collect()

    other = pool.get_or_allocate([10, 20])
    assert other.get().empty()
    other.get().insert([5, 6])
    assert tuple(row) == (3, 4)
    del other
    gc.collect()
    reused = pool.get_or_allocate([30, 40])
    assert reused.get().empty()
    reused.get().insert([7, 8])
    assert tuple(row) == (3, 4)
    del reused, pool
    gc.collect()
    assert tuple(row) == (3, 4)


def test_iterators_keep_pooled_rows_alive() -> None:
    pool = database.RelationPool()
    handle = pool.get_or_allocate([0, 1])
    handle.get().insert([3, 4])
    handle.get().insert([5, 6])
    rows = iter(handle.get())
    assert iter(rows) is rows
    del handle, pool
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
