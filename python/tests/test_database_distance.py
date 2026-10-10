import gc
import math

import pytest

from pyyggdrasil import database


def columns(*labels):
    return tuple(database.ColumnIndex(label) for label in labels)


def relation(labels, rows=(), types=()):
    result = database.Relation(list(columns(*labels)), types)
    for row in rows:
        result.insert(row)
    return result


def rows(value):
    return {tuple(row) for row in value}


def plan_for(source, edges, target):
    return database.DistancePlan(source.columns(), edges.columns(), target.columns(), database.ColumnIndex(99))


@pytest.mark.parametrize("interned_mask", range(8))
def test_distance_mixed_inputs_and_independent_output(interned_mask):
    types = [database.ColumnType.INT32, database.ColumnType.FLOAT64]
    a, b, c, d = (-7, 0.0), (3, 1.5), (5, 2.5), (9, 3.5)
    source = relation([10, 11], [(-7, -0.0), c], types)
    edges = relation([20, 21, 22, 23], [a + b, b + c, c + c], types * 2)
    target = relation([30, 31], [a, c, d], types)
    plan = plan_for(source, edges, target)
    assert plan.arity() == 2
    assert tuple(plan.output_columns()) == columns(20, 21, 22, 23, 99)
    repository = database.RelationRepositoryFactory().create()
    inputs = [source, edges, target]
    for index, value in enumerate(inputs):
        if interned_mask & (1 << index):
            inputs[index] = database.insert(repository, value)[0]
    if 0 < interned_mask < 7:  # Interned inputs are accepted without copying only when all are interned.
        inputs = [database.assign(database.Relation(), value) if isinstance(value, database.RelationView) else value for value in inputs]
    result = database.distance(*inputs, plan)
    assert isinstance(result, database.Relation)
    expected = {a + a + (0,), a + c + (2,), c + c + (0,)}
    assert rows(result) == expected
    assert tuple(result.columns()) == columns(20, 21, 22, 23, 99)
    assert [result.columns().type(i) for i in range(5)] == types * 2 + [database.ColumnType.UINT32]

    other = database.distance(*inputs, plan)
    other.clear()
    source.clear()
    edges.clear()
    target.clear()
    del inputs, repository, plan, source, edges, target
    gc.collect()
    assert rows(result) == expected


def test_distance_canonical_nan_boolean_and_zero_length_paths():
    types = [database.ColumnType.BOOL, database.ColumnType.FLOAT64]
    source = relation([10, 11], [(True, float("nan")), (False, -0.0)], types)
    edges = relation([20, 21, 22, 23], [(True, -float("nan"), False, 0.0)], types * 2)
    target = relation([30, 31], [(True, -float("nan")), (False, 0.0)], types)
    result = database.distance(source, edges, target, plan_for(source, edges, target))
    normalized = {
        tuple("nan" if isinstance(value, float) and math.isnan(value) else value for value in row)
        for row in result
    }
    assert normalized == {
        (True, "nan", True, "nan", 0),
        (True, "nan", False, 0.0, 1),
        (False, 0.0, False, 0.0, 0),
    }


@pytest.mark.parametrize("intern_added", [False, True])
def test_incremental_distance_exact_deltas(intern_added):
    source = relation([10], [(0,), (2,)])
    edges = relation([20, 21], [(0, 1), (1, 2), (2, 3), (0, 3)])
    target = relation([30], [(0,), (2,), (3,)])
    plan = plan_for(source, edges, target)
    evaluator = database.DistanceEvaluator(plan)
    repository = database.RelationRepositoryFactory().create()
    evaluator.initialize(source, database.assign(database.Relation(), database.insert(repository, edges)[0]), target)
    expected = {(0, 0, 0), (0, 2, 2), (0, 3, 1), (2, 2, 0), (2, 3, 1)}
    assert rows(evaluator.get_result()) == expected
    assert evaluator.get_delta().added.empty()
    assert evaluator.get_delta().removed.empty()
    assert evaluator.memory_usage() > 0

    def update(source_added=(), source_removed=(), edges_added=(), edges_removed=(), target_added=(), target_removed=()):
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
                changes[index] = database.assign(database.Relation(), database.insert(repository, changes[index])[0])
        evaluator.update((changes[0], changes[1]), (changes[2], changes[3]), (changes[4], changes[5]))

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
    ["plan_columns", "result", "delta", "result_row", "added_row", "removed_row", "result_columns", "added_columns", "removed_columns", "iterator"],
)
def test_evaluator_borrowed_results_are_read_only_and_retain_owners(leaf):
    source = relation([10], [(0,)])
    edges = relation([20, 21], [(0, 1), (1, 2)])
    target = relation([30], [(2,)])
    plan = plan_for(source, edges, target)
    evaluator = database.DistanceEvaluator(plan)
    evaluator.initialize(source, edges, target)
    evaluator.update((relation([10]), relation([10])), (relation([20, 21], [(0, 2)]), relation([20, 21])), (relation([30]), relation([30])))
    result = evaluator.get_result()
    delta = evaluator.get_delta()
    assert isinstance(result, database.BorrowedRelation)
    assert isinstance(delta, database.RelationDelta)
    for value in (result, delta.added, delta.removed):
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
    with pytest.raises(AttributeError):
        delta.added = source
    with pytest.raises(TypeError):
        database.BorrowedRelation()
    with pytest.raises(TypeError):
        database.RelationDelta()
    retained = {
        "plan_columns": lambda: plan.output_columns(),
        "result": lambda: result,
        "delta": lambda: delta,
        "result_row": lambda: result[-1],
        "added_row": lambda: delta.added.at(0),
        "removed_row": lambda: delta.removed[0],
        "result_columns": lambda: result.columns(),
        "added_columns": lambda: delta.added.columns(),
        "removed_columns": lambda: delta.removed.columns(),
        "iterator": lambda: iter(result),
    }[leaf]()
    del evaluator, delta, result, plan, source, edges, target, value
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


def test_borrowed_relations_can_feed_another_distance_evaluation():
    source = relation([10], [(0,)])
    edges = relation([20, 21], [(0, 1)])
    target = relation([30], [(1,)])
    evaluator = database.DistanceEvaluator(plan_for(source, edges, target))
    evaluator.initialize(source, edges, target)
    borrowed = evaluator.get_result()
    next_edges = relation([40, 41, 42, 43, 44, 45])
    next_target = relation([50, 51, 52], [(0, 1, 1)])
    plan = database.DistancePlan(borrowed.columns(), next_edges.columns(), next_target.columns(), database.ColumnIndex(100))
    assert rows(database.distance(borrowed, next_edges, next_target, plan)) == {(0, 1, 1, 0, 1, 1, 0)}


def test_distance_rejects_invalid_schema_and_input_types():
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
        database.distance(relation([11]), edges, target, plan)
    with pytest.raises(ValueError):
        database.distance(source, edges, relation([30], types=[database.ColumnType.INT32]), plan)
    with pytest.raises(TypeError):
        database.distance([], edges, target, plan)
    evaluator = database.DistanceEvaluator(plan)
    with pytest.raises(RuntimeError):
        evaluator.update((source, relation([10])), (edges, relation([20, 21])), (target, relation([30])))
    evaluator.initialize(source, edges, target)
    before = rows(evaluator.get_result())
    with pytest.raises(ValueError):
        evaluator.update((relation([11]), relation([10])), (relation([20, 21]), relation([20, 21])), (relation([30]), relation([30])))
    assert rows(evaluator.get_result()) == before
