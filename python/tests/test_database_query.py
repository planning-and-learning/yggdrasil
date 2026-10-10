import gc

import pytest

from pyyggdrasil import database as db


# Repositories from one factory have distinct identities.
FACTORY = db.QueryRepositoryFactory()


def columns(*labels):
    return [db.ColumnIndex(label) for label in labels]


def relation(labels, values=(), types=()):
    result = db.Relation(columns(*labels), types)
    for value in values:
        result.insert(value)
    return result


def rows(value):
    return {tuple(row) for row in value}


def statistics(inputs):
    return db.collect_statistics(inputs)


@pytest.mark.parametrize("measured", (True, False))
def test_optimizer_preserves_typed_shared_roots_and_output_order(measured):
    types = [db.ColumnType.INT32, db.ColumnType.FLOAT64]
    left = relation([0, 1], [(1, 2.5), (2, 3.5), (3, 2.5)], types)
    right = relation([1, 2], [(2.5, 7), (3.5, 8)], types[::-1])
    repo = FACTORY.create()
    a, b = repo.input(0, left.columns()), repo.input(1, right.columns())
    shared = repo.join(a, b)
    projected = repo.project(shared, columns(2, 0))
    root_queries = [projected, repo.select_value(shared, db.ColumnIndex(0), 1), repo.rename(projected, columns(8, 9))]
    assert repo.join(a, b) == shared
    assert hash(repo.join(a, b)) == hash(shared)
    original = repo.compile(root_queries)
    optimized = db.optimize(root_queries, statistics([left, right]) if measured else None)
    baseline, actual = db.QueryEvaluator(original), db.QueryEvaluator(optimized)
    baseline.evaluate([left, right])
    actual.evaluate([left, right])
    assert optimized.root_count == 3
    assert optimized.node_count > 0
    assert optimized.explain()
    for index in range(3):
        assert tuple(actual.get_result(index).columns()) == tuple(baseline.get_result(index).columns())
        assert rows(actual.get_result(index)) == rows(baseline.get_result(index))
    assert rows(actual.get_result()) == {(7, 1), (8, 2), (7, 3)}
    assert actual.memory_usage() > 0


def test_difference_projection_barrier_and_nullary_support():
    source = relation([0, 1], [(1, 7), (1, 8)])
    removed = relation([0, 1], [(1, 7)])
    repo = FACTORY.create()
    source_query, removed_query = repo.input(0, source.columns()), repo.input(1, removed.columns())
    surviving = repo.project(repo.difference(source_query, removed_query), columns(0))
    root_queries = [surviving, repo.project(surviving, columns())]
    optimized = db.optimize(root_queries, statistics([source, removed]))
    evaluation = db.QueryEvaluator(optimized)
    evaluation.evaluate([source, removed])
    assert rows(evaluation.get_result()) == {(1,)}
    assert rows(evaluation.get_result(1)) == {()}


def test_incremental_batch_changes_projection_union_and_difference():
    schemas = [[0, 1], [1, 2], [0, 2]]
    current = [{(1, 7), (1, 8), (2, 8)}, {(7, 9), (8, 9)}, {(2, 9)}]
    inputs = [relation(schema, values) for schema, values in zip(schemas, current)]
    repo = FACTORY.create()
    a, b, blocked = [repo.input(i, value.columns()) for i, value in enumerate(inputs)]
    projected = repo.project(repo.join(a, b), columns(0, 2))
    visible = repo.difference(projected, blocked)
    combined = repo.union(visible, repo.empty(inputs[2].columns()))
    root_queries = [combined, repo.project(visible, columns())]
    original = repo.compile(root_queries)
    optimized = db.optimize(root_queries, statistics(inputs))
    full, maintained = db.QueryEvaluator(original), db.IncrementalQueryEvaluator(optimized)
    full.evaluate(inputs)
    maintained.initialize(inputs)
    for index in range(2):
        assert rows(maintained.get_result(index)) == rows(full.get_result(index))
        assert maintained.get_delta(index).added.empty()
        assert maintained.get_delta(index).removed.empty()
    assert maintained.memory_usage() > 0
    steps = [
        ([set(), set(), set()], [{(1, 7)}, set(), set()]),
        ([{(3, 7)}, {(7, 10)}, set()], [{(1, 8)}, {(7, 9)}, {(2, 9)}]),
        ([set(), set(), {(3, 10), (2, 9)}], [set(), set(), set()]),
        ([set(), set(), set()], [set(), set(), set()]),
    ]
    for additions, removals in steps:
        before = [rows(maintained.get_result(i)) for i in range(2)]
        for i in range(3):
            current[i] = (current[i] - removals[i]) | additions[i]
        maintained.update([relation(s, v) for s, v in zip(schemas, additions)],
                          [relation(s, v) for s, v in zip(schemas, removals)])
        full.evaluate([relation(s, v) for s, v in zip(schemas, current)])
        for index in range(2):
            after = rows(full.get_result(index))
            assert rows(maintained.get_result(index)) == after
            assert rows(maintained.get_delta(index).added) == after - before[index]
            assert rows(maintained.get_delta(index).removed) == before[index] - after


def test_triangle_and_self_join():
    inputs = [relation([0, 1], [(1, 2), (2, 3), (3, 1)]),
              relation([1, 2], [(2, 3), (3, 1), (1, 2)]),
              relation([2, 0], [(3, 1), (1, 2), (2, 3)])]
    repo = FACTORY.create()
    a, b, c = [repo.input(i, value.columns()) for i, value in enumerate(inputs)]
    triangle = repo.join(repo.join(a, b), c)
    roots = [triangle, repo.join(a, a)]
    optimized = db.optimize(roots, statistics(inputs))
    evaluator = db.QueryEvaluator(optimized)
    evaluator.evaluate(inputs)
    assert rows(evaluator.get_result()) == {(1, 2, 3), (2, 3, 1), (3, 1, 2)}
    assert rows(evaluator.get_result(1)) == rows(inputs[0])


def test_borrowed_inputs_statistics_snapshot_and_owners():
    original = relation([0], [(1,), (2,)])
    store = db.RelationRepositoryFactory().create()
    interned = db.insert(store, original)[0]
    repo = FACTORY.create()
    root = repo.input(0, original.columns())
    evaluator = db.QueryEvaluator(repo.compile(root))
    evaluator.evaluate([interned])
    borrowed = evaluator.get_result()
    independent = db.snapshot(borrowed)
    statistics = db.collect_statistics([borrowed])
    assert statistics.inputs[0].rows == 2
    assert statistics.inputs[0].distinct == {db.ColumnIndex(0): 2}
    second = db.QueryEvaluator(repo.compile(root))
    second.evaluate([borrowed])
    row = second.get_result()[0]
    assert not hasattr(second.get_result(), "clear")
    del root, repo, evaluator, second, borrowed, original, store, interned
    gc.collect()
    assert tuple(row) in {(1,), (2,)}
    assert rows(independent) == {(1,), (2,)}


def test_query_handle_retains_repository_and_compiled_plan_is_independent():
    source = relation([4], [(9,)])
    repo = FACTORY.create()
    query = repo.input(0, columns(4))
    other = FACTORY.create().input(0, columns(4))
    assert query != other
    assert len({query, other}) == 2
    plan = repo.compile(query)
    del repo
    gc.collect()
    assert tuple(query.columns()) == tuple(columns(4))
    del query
    gc.collect()
    evaluator = db.QueryEvaluator(plan)
    del plan
    evaluator.evaluate([source])
    assert rows(evaluator.get_result()) == {(9,)}


@pytest.mark.parametrize("incremental", (False, True))
def test_distance_boundary_and_sparse_input_slots(incremental):
    source = relation([10], [(0,)])
    edges = relation([20, 21], [(0, 1), (1, 2)])
    target = relation([30], [(2,)])
    repo = FACTORY.create()
    s, e, t = [repo.input(i, value.columns()) for i, value in zip([1, 3, 4], [source, edges, target])]
    query = repo.distance(s, e, t, db.ColumnIndex(99))
    optimized = db.optimize(query)
    if not incremental:
        evaluator = db.QueryEvaluator(optimized)
        evaluator.evaluate([None, source, None, edges, target])
    else:
        evaluator = db.IncrementalQueryEvaluator(optimized)
        evaluator.initialize([None, source, None, edges, target])
    assert rows(evaluator.get_result()) == {(0, 2, 2)}
    if incremental:
        evaluator.update([None, relation([10]), None, relation([20, 21], [(0, 2)]), relation([30])],
                         [None, relation([10]), None, relation([20, 21]), relation([30])])
        assert rows(evaluator.get_result()) == {(0, 2, 1)}
        assert rows(evaluator.get_delta().added) == {(0, 2, 1)}
        assert rows(evaluator.get_delta().removed) == {(0, 2, 2)}


def test_statistics_and_input_validation():
    value = relation([0], [(1,)])
    repo = FACTORY.create()
    query = repo.input(0, value.columns())
    assert db.optimize(query).root_count == 1
    assert db.optimize(query, db.Statistics(objects=4, inputs={0: db.RelationStatistics(1, {db.ColumnIndex(0): 1})})).root_count == 1
    with pytest.raises(ValueError):
        db.optimize(query, db.Statistics(inputs={0: db.RelationStatistics(-1, {db.ColumnIndex(0): 1})}))
    evaluator = db.QueryEvaluator(repo.compile(query))
    with pytest.raises(ValueError):
        evaluator.evaluate([relation([1])])
    with pytest.raises(TypeError):
        evaluator.evaluate([object()])
    with pytest.raises(IndexError):
        repo.project(query, columns(5))
    incremental = db.IncrementalQueryEvaluator(repo.compile(query))
    incremental.initialize([value])
    with pytest.raises(ValueError):
        incremental.update([relation([0])], [])


@pytest.mark.parametrize("measured", (True, False))
def test_planning_is_deterministic(measured):
    inputs = [relation([i, i + 1], [(1, 1), (2, 2)]) for i in range(4)]
    repo = FACTORY.create()
    queries = [repo.input(i, value.columns()) for i, value in enumerate(inputs)]
    query = queries[0]
    for other in queries[1:]:
        query = repo.join(query, other)
    known = statistics(inputs) if measured else None
    first = db.optimize(query, known)
    second = db.optimize(query, known)
    assert first.explain() == second.explain()
    evaluator = db.QueryEvaluator(first)
    evaluator.evaluate(inputs)
    assert rows(evaluator.get_result()) == {(1, 1, 1, 1, 1), (2, 2, 2, 2, 2)}


def test_selection_constants_follow_column_types_and_equality():
    source = relation([0, 1], [(-3, -3), (-3, 7), (7, 7)], [db.ColumnType.INT32] * 2)
    repo = FACTORY.create()
    query = repo.input(0, source.columns())
    selected = repo.select_value(repo.select_equal(query, db.ColumnIndex(0), db.ColumnIndex(1)), db.ColumnIndex(0), -3)
    evaluator = db.QueryEvaluator(db.optimize(selected))
    evaluator.evaluate([source])
    assert rows(evaluator.get_result()) == {(-3, -3)}
    with pytest.raises(TypeError):
        repo.select_value(query, db.ColumnIndex(0), "invalid")
