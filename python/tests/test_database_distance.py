import gc
import math
from collections.abc import Iterable, Sequence
from typing import Any, cast

import pytest

from pyyggdrasil import database

Readable = database.Relation | database.BorrowedRelation | database.RelationView


def columns(*labels: int) -> tuple[database.ColumnIndex, ...]:
    return tuple(database.ColumnIndex(label) for label in labels)


def relation(
    labels: Sequence[int], rows: Iterable[Sequence[object]] = (), types: Sequence[database.ColumnType] = ()
) -> database.Relation:
    schema = database.Columns()
    for index, label in enumerate(labels):
        schema.push_back(database.ColumnIndex(label), types[index] if types else database.ColumnType.UINT32)
    result = database.Relation(schema)
    for row in rows:
        result.insert(row)
    return result


def rows(value: Iterable[database.RelationRow]) -> set[tuple[object, ...]]:
    return {tuple(row) for row in value}


def borrow(
    source: database.Relation, edges: database.Relation, target: database.Relation
) -> tuple[database.BorrowedRelation, database.BorrowedRelation, database.BorrowedRelation]:
    return database.BorrowedRelation(source), database.BorrowedRelation(edges), database.BorrowedRelation(target)


def change(
    added: database.Relation, removed: database.Relation
) -> tuple[database.BorrowedRelation, database.BorrowedRelation]:
    return database.BorrowedRelation(added), database.BorrowedRelation(removed)


def plan_for(source: Readable, edges: Readable, target: Readable) -> database.DistancePlan:
    return database.DistancePlan(source.columns(), edges.columns(), target.columns(), database.ColumnIndex(99))


@pytest.mark.parametrize("interned_mask", range(8))
def test_distance_mixed_inputs_and_independent_output(interned_mask: int) -> None:
    types = [database.ColumnType.INT32, database.ColumnType.FLOAT64]
    a, b, c, d = (-7, 0.0), (3, 1.5), (5, 2.5), (9, 3.5)
    source = relation([10, 11], [(-7, -0.0), c], types)
    edges = relation([20, 21, 22, 23], [a + b, b + c, c + c], types * 2)
    target = relation([30, 31], [a, c, d], types)
    plan = plan_for(source, edges, target)
    assert plan.arity() == 2
    assert tuple(plan.output_columns()) == columns(20, 21, 22, 23, 99)
    repository = database.RelationRepositoryFactory().create()
    interned = [repository.insert(value)[0] for value in (source, edges, target)]
    # Interned inputs are accepted without copying only when all are interned; otherwise they are copied out.
    copied = [
        database.assign(database.Relation(), view) if interned_mask & (1 << index) else value
        for index, (value, view) in enumerate(zip((source, edges, target), interned, strict=True))
    ]
    borrowed = borrow(copied[0], copied[1], copied[2])

    # Two independent outputs of the same call; `other` is cleared below and must not affect `result`.
    if interned_mask == 7:
        result, other = (database.distance(interned[0], interned[1], interned[2], plan) for _ in range(2))
    else:
        result, other = (database.distance(borrowed[0], borrowed[1], borrowed[2], plan) for _ in range(2))
    assert isinstance(result, database.Relation)
    expected = {a + a + (0,), a + c + (2,), c + c + (0,)}
    assert rows(result) == expected
    assert tuple(result.columns()) == columns(20, 21, 22, 23, 99)
    assert [result.columns().type(i) for i in range(5)] == types * 2 + [database.ColumnType.UINT32]

    other.clear()
    source.clear()
    edges.clear()
    target.clear()
    del interned, copied, borrowed, repository, plan, source, edges, target
    gc.collect()
    assert rows(result) == expected


def test_distance_canonical_nan_boolean_and_zero_length_paths() -> None:
    types = [database.ColumnType.BOOL, database.ColumnType.FLOAT64]
    source = relation([10, 11], [(True, float("nan")), (False, -0.0)], types)
    edges = relation([20, 21, 22, 23], [(True, -float("nan"), False, 0.0)], types * 2)
    target = relation([30, 31], [(True, -float("nan")), (False, 0.0)], types)
    result = database.distance(*borrow(source, edges, target), plan_for(source, edges, target))
    normalized = {
        tuple("nan" if isinstance(value, float) and math.isnan(value) else value for value in row) for row in result
    }
    assert normalized == {
        (True, "nan", True, "nan", 0),
        (True, "nan", False, 0.0, 1),
        (False, 0.0, False, 0.0, 0),
    }


@pytest.mark.parametrize("intern_added", [False, True])
def test_incremental_distance_exact_deltas(intern_added: bool) -> None:
    source = relation([10], [(0,), (2,)])
    edges = relation([20, 21], [(0, 1), (1, 2), (2, 3), (0, 3)])
    target = relation([30], [(0,), (2,), (3,)])
    plan = plan_for(source, edges, target)
    evaluator = database.DistanceEvaluator(plan)
    repository = database.RelationRepositoryFactory().create()
    evaluator.initialize(*borrow(source, database.assign(database.Relation(), repository.insert(edges)[0]), target))
    expected = {(0, 0, 0), (0, 2, 2), (0, 3, 1), (2, 2, 0), (2, 3, 1)}
    assert rows(evaluator.get_result()) == expected
    assert evaluator.get_delta().added.empty()
    assert evaluator.get_delta().removed.empty()
    assert evaluator.memory_usage() > 0

    def update(
        source_added: Iterable[Sequence[object]] = (),
        source_removed: Iterable[Sequence[object]] = (),
        edges_added: Iterable[Sequence[object]] = (),
        edges_removed: Iterable[Sequence[object]] = (),
        target_added: Iterable[Sequence[object]] = (),
        target_removed: Iterable[Sequence[object]] = (),
    ) -> None:
        changes = [
            relation([10], source_added),
            relation([10], source_removed),
            relation([20, 21], edges_added),
            relation([20, 21], edges_removed),
            relation([30], target_added),
            relation([30], target_removed),
        ]
        for index in range(6):
            if (index % 2 == 0) == intern_added:
                changes[index] = database.assign(database.Relation(), repository.insert(changes[index])[0])
        evaluator.update(change(changes[0], changes[1]), change(changes[2], changes[3]), change(changes[4], changes[5]))

    before = expected
    update(edges_added=[(0, 2)], edges_removed=[(0, 3)])
    expected = {(0, 0, 0), (0, 2, 1), (0, 3, 2), (2, 2, 0), (2, 3, 1)}
    assert rows(evaluator.get_result()) == expected
    assert rows(evaluator.get_delta().added) == expected - before
    assert rows(evaluator.get_delta().removed) == before - expected

    before = expected
    update(source_added=[(1,)], source_removed=[(2,)], target_added=[(1,)], target_removed=[(0,)])
    expected = {(0, 1, 1), (0, 2, 1), (0, 3, 2), (1, 1, 0), (1, 2, 1), (1, 3, 2)}
    assert rows(evaluator.get_result()) == expected
    assert rows(evaluator.get_delta().added) == expected - before
    assert rows(evaluator.get_delta().removed) == before - expected
    update()
    assert rows(evaluator.get_result()) == expected
    assert evaluator.get_delta().added.empty()
    assert evaluator.get_delta().removed.empty()


@pytest.mark.parametrize(
    "leaf",
    [
        "plan_columns",
        "result",
        "delta",
        "result_row",
        "added_row",
        "removed_row",
        "result_columns",
        "added_columns",
        "removed_columns",
        "iterator",
    ],
)
def test_evaluator_borrowed_results_are_read_only_and_retain_owners(leaf: str) -> None:
    source = relation([10], [(0,)])
    edges = relation([20, 21], [(0, 1), (1, 2)])
    target = relation([30], [(2,)])
    plan = plan_for(source, edges, target)
    evaluator = database.DistanceEvaluator(plan)
    evaluator.initialize(*borrow(source, edges, target))
    evaluator.update(
        change(relation([10]), relation([10])),
        change(relation([20, 21], [(0, 2)]), relation([20, 21])),
        change(relation([30]), relation([30])),
    )
    result = evaluator.get_result()
    delta = evaluator.get_delta()
    assert isinstance(result, database.BorrowedRelation)
    assert isinstance(delta, database.RelationDelta)

    def check_read_only(value: database.BorrowedRelation) -> None:
        assert not hasattr(value, "insert")
        assert not hasattr(value, "clear")
        assert not hasattr(value, "erase")
        assert value.arity() == 3
        assert tuple(value.columns()) == columns(20, 21, 99)
        with pytest.raises(IndexError):
            value.at(len(value))
        for index in (-len(value) - 1, len(value)):
            with pytest.raises(IndexError):
                value[index]

    for value in (result, delta.added, delta.removed):
        check_read_only(value)
    # Deliberate misuse: the type hints reject these calls, the bindings must too.
    with pytest.raises(AttributeError):
        cast(Any, delta).added = source
    with pytest.raises(TypeError):
        cast(Any, database.BorrowedRelation)()
    with pytest.raises(TypeError):
        database.RelationDelta()
    retained: Any  # each leaf is a different borrowed type
    match leaf:
        case "plan_columns":
            retained = plan.output_columns()
        case "result":
            retained = result
        case "delta":
            retained = delta
        case "result_row":
            retained = result[-1]
        case "added_row":
            retained = delta.added.at(0)
        case "removed_row":
            retained = delta.removed[0]
        case "result_columns":
            retained = result.columns()
        case "added_columns":
            retained = delta.added.columns()
        case "removed_columns":
            retained = delta.removed.columns()
        case "iterator":
            retained = iter(result)
        case _:
            raise AssertionError(leaf)
    del evaluator, delta, result, plan, source, edges, target
    gc.collect()
    if leaf.endswith("columns"):
        assert tuple(retained) == columns(20, 21, 99)
    elif leaf.endswith("row"):
        assert tuple(retained) == (0, 2, 2 if leaf == "removed_row" else 1)
    elif leaf == "delta":
        assert rows(retained.added) == {(0, 2, 1)}
        assert rows(retained.removed) == {(0, 2, 2)}
    else:
        assert [tuple(row) for row in retained] == [(0, 2, 1)]


def test_borrowed_relations_can_feed_another_distance_evaluation() -> None:
    source = relation([10], [(0,)])
    edges = relation([20, 21], [(0, 1)])
    target = relation([30], [(1,)])
    evaluator = database.DistanceEvaluator(plan_for(source, edges, target))
    evaluator.initialize(*borrow(source, edges, target))
    borrowed = evaluator.get_result()
    next_edges = relation([40, 41, 42, 43, 44, 45])
    next_target = relation([50, 51, 52], [(0, 1, 1)])
    plan = database.DistancePlan(
        borrowed.columns(), next_edges.columns(), next_target.columns(), database.ColumnIndex(100)
    )
    edges_view, target_view = change(next_edges, next_target)
    assert rows(database.distance(borrowed, edges_view, target_view, plan)) == {(0, 1, 1, 0, 1, 1, 0)}


def test_distance_rejects_invalid_schema_and_input_types() -> None:
    source = relation([10], [(0,)])
    edges = relation([20, 21], [(0, 1)])
    target = relation([30], [(1,)])
    plan = plan_for(source, edges, target)
    with pytest.raises(ValueError):
        plan_for(source, relation([20]), target)
    with pytest.raises(ValueError):
        plan_for(source, edges, relation([30, 31]))
    with pytest.raises(ValueError):
        plan_for(source, relation([20, 21], types=[database.ColumnType.INT32] * 2), target)
    with pytest.raises(ValueError):
        database.DistancePlan(source.columns(), edges.columns(), target.columns(), database.ColumnIndex(20))
    with pytest.raises(ValueError):
        database.distance(*borrow(relation([11]), edges, target), plan)
    with pytest.raises(ValueError):
        database.distance(*borrow(source, edges, relation([30], types=[database.ColumnType.INT32])), plan)
    _, edges_view, target_view = borrow(source, edges, target)
    with pytest.raises(TypeError):
        database.distance(cast(Any, []), edges_view, target_view, plan)
    evaluator = database.DistanceEvaluator(plan)
    with pytest.raises(RuntimeError):
        evaluator.update(
            change(source, relation([10])), change(edges, relation([20, 21])), change(target, relation([30]))
        )
    evaluator.initialize(*borrow(source, edges, target))
    before = rows(evaluator.get_result())
    with pytest.raises(ValueError):
        evaluator.update(
            change(relation([11]), relation([10])),
            change(relation([20, 21]), relation([20, 21])),
            change(relation([30]), relation([30])),
        )
    assert rows(evaluator.get_result()) == before
