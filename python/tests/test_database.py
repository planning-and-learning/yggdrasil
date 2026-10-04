import gc

import pytest

import pyyggdrasil
from pyyggdrasil import database


def test_relation_rows_and_validation() -> None:
    assert database is pyyggdrasil.database
    assert all(hasattr(database, name) for name in database.__all__)
    relation = database.Relation([10, 20])
    labels = relation.columns()
    assert isinstance(labels, database.ColumnIndices)
    assert len(labels) == 2
    assert tuple(labels) == (10, 20)
    assert all(type(label) is int for label in labels)
    assert labels[-1] == 20
    assert labels[-2] == 10
    for index in (-3, 2):
        with pytest.raises(IndexError):
            _ = labels[index]
    with pytest.raises(TypeError):
        labels[0] = 7
    assert relation.arity() == 2
    assert relation.empty()
    assert relation.insert([3, 4]) == relation.insert([3, 4])
    row = relation.at(0)
    assert isinstance(row, database.RelationRow)
    assert len(relation) == 1
    assert not relation.empty()
    assert tuple(row) == (3, 4)
    assert tuple(relation[0]) == tuple(relation[-1]) == (3, 4)
    for index in (-2, 1):
        with pytest.raises(IndexError):
            _ = relation[index]
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
    del row
    gc.collect()
    assert tuple(labels) == (10, 20)


def test_nullary_relation() -> None:
    relation = database.Relation()
    assert relation.arity() == 0
    assert len(relation) == 0
    assert isinstance(relation.columns(), database.ColumnIndices)
    assert tuple(relation.columns()) == ()
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
    labels = relation.columns()
    assert isinstance(labels, database.ColumnIndices)
    del relation, handle
    gc.collect()

    other = pool.get_or_allocate([10, 20])
    assert other.get().empty()
    other.get().insert([5, 6])
    assert tuple(row) == (3, 4)
    del other
    gc.collect()
    with pytest.raises(ValueError):
        pool.get_or_allocate([10, 10])
    reused = pool.get_or_allocate([30, 40])
    assert reused.get().empty()
    reused.get().insert([7, 8])
    assert tuple(row) == (3, 4)
    del reused, pool
    gc.collect()
    assert tuple(row) == (3, 4)
    assert tuple(labels) == (0, 1)


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


def test_interned_relations_use_typed_identity_and_independent_storage() -> None:
    factory = database.RelationRepositoryFactory()
    repository = factory.create()
    builder = database.Relation([10, 20])
    builder.insert([3, 4])
    builder.insert([5, 6])
    result = database.insert(repository, builder)
    assert isinstance(result, tuple)
    relation, created = result
    duplicate, duplicate_created = database.insert(repository, builder)
    assert created is True
    assert duplicate_created is False
    del result
    assert isinstance(relation, database.RelationView)
    assert isinstance(relation.get_index(), database.RelationIndex)
    assert relation == duplicate
    assert hash(relation) == hash(duplicate)
    assert len({relation, duplicate}) == 1
    assert len(repository) == 1
    assert not hasattr(relation, "insert")
    assert not hasattr(relation, "clear")

    other_repository = factory.create()
    assert database.insert(other_repository, builder)[0] != relation
    namespaced = database.insert(repository, builder, schema_namespace=1)[0]
    assert namespaced != relation
    assert len(repository) == 2
    assert repository.rename(namespaced, [10, 20]) == namespaced
    assert repository.rename(namespaced, [10, 20], schema_namespace=0) == relation
    with pytest.raises(ValueError):
        other_repository.rename(relation, [30, 40])

    builder.clear()
    builder.insert([8, 9])
    assert [tuple(row) for row in relation] == [(3, 4), (5, 6)]
    assert [tuple(row) for row in builder] == [(8, 9)]
    assert relation.arity() == 2
    assert not relation.empty()
    assert tuple(relation[-1]) == (5, 6)
    for index in (-3, 2):
        with pytest.raises(IndexError):
            _ = relation[index]
    with pytest.raises(IndexError):
        relation.at(2)

    renamed = repository.rename(relation, [30, 40])
    assert tuple(renamed.columns()) == (30, 40)
    assert [tuple(row) for row in renamed] == [(3, 4), (5, 6)]
    assert renamed != relation
    assert repository.rename(renamed, [10, 20]) == relation
    with pytest.raises(ValueError):
        repository.rename(relation, [30])
    with pytest.raises(ValueError):
        repository.rename(relation, [30, 30])

    # Every borrowed layer must keep the repository alive, without keeping the
    # mutable builder alive or depending on its buffers.
    row = relation.at(0)
    labels = renamed.columns()
    assert isinstance(labels, database.ColumnIndices)
    assert all(type(label) is int for label in labels)
    rows = iter(renamed)
    indexed_row = relation[1]
    del relation, duplicate, namespaced, renamed, repository, other_repository, factory, builder
    gc.collect()
    assert tuple(row) == (3, 4)
    assert tuple(labels) == (30, 40)
    assert [tuple(other) for other in rows] == [(3, 4), (5, 6)]
    assert tuple(indexed_row) == (5, 6)
    del row, indexed_row, rows
    gc.collect()
    assert tuple(labels) == (30, 40)


def test_interned_nullary_relations_and_repository_reset() -> None:
    factory = database.RelationRepositoryFactory()
    repository = factory.create()
    builder = database.Relation()
    false = database.insert(repository, builder)[0]
    builder.insert([])
    true = database.insert(repository, builder)[0]
    assert false.empty()
    assert list(false) == []
    assert [tuple(row) for row in true] == [()]
    assert false != true
    assert len(repository) == 2
    del false, true
    repository.clear()
    assert len(repository) == 0
    assert [tuple(row) for row in database.insert(repository, builder)[0]] == [()]


def test_interning_ignores_row_insertion_order() -> None:
    repository = database.RelationRepositoryFactory().create()
    seed = database.Relation([10, 20])
    seed.insert([9, 8])
    database.insert(repository, seed)[0]

    builder = database.Relation([10, 20])
    builder.insert([3, 4])
    builder.insert([9, 8])
    forward = database.insert(repository, builder)[0]
    builder.clear()
    builder.insert([9, 8])
    builder.insert([3, 4])
    builder.insert([3, 4])
    reverse = database.insert(repository, builder)[0]

    assert forward == reverse
    assert forward.get_index() == reverse.get_index()
    assert hash(forward) == hash(reverse)
    assert len(repository) == 2
    # Row IDs follow the repository's row-interning history; canonical
    # relations enumerate the sorted IDs, independent of builder order.
    assert [tuple(row) for row in forward] == [(9, 8), (3, 4)]
    assert [tuple(row) for row in reverse] == [(9, 8), (3, 4)]


def test_copy_and_assign_remap_relations_and_retain_unpacked_owner() -> None:
    source_factory = database.RelationRepositoryFactory()
    target_factory = database.RelationRepositoryFactory()
    source_repository = source_factory.create()
    target_repository = target_factory.create()
    builder = database.Relation([7, 3])
    builder.insert([4, 5])
    source, created = database.insert(source_repository, builder, schema_namespace=9)
    assert created is True
    target_result = database.copy(source, target_repository)
    target, copied = target_result
    assert copied is True
    assert database.copy(source, target_repository) == (target, False)
    assert database.copy(target, target_repository) == (target, False)
    assert tuple(target.columns()) == (7, 3)
    output = database.Relation([7, 3])
    assert database.assign(output, target) is output
    assert [tuple(row) for row in output] == [(4, 5)]
    with pytest.raises(ValueError):
        database.assign(output, output)
    with pytest.raises(ValueError):
        database.assign(database.Relation([3, 7]), target)
    del target_result, source, source_repository, target_repository
    del source_factory, target_factory, builder, output
    gc.collect()
    assert [tuple(row) for row in target] == [(4, 5)]
    assert not hasattr(database, "intern_relation")
