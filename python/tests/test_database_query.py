import gc

import pytest

from pyyggdrasil import database as db


# Repositories from one factory have distinct identities.
FACTORY = db.QueryRepositoryFactory()


def columns(*labels):
    return [db.ColumnIndex(label) for label in labels]


def relation(labels, values=(), types=()):
    schema = db.Columns()
    for index, label in enumerate(labels):
        schema.push_back(db.ColumnIndex(label), types[index] if types else db.ColumnType.UINT32)
    result = db.Relation(schema)
    for value in values:
        result.insert(value)
    return result


def query(repo, data):
    """Interns the concrete record and returns its type-erased query."""
    concrete, _ = repo.insert(data)
    return repo.insert(db.QueryData(concrete))[0]


def inputs_of(repo, relations, slots=None):
    slots = range(len(relations)) if slots is None else slots
    return [query(repo, db.QueryInputData(slot, value.columns())) for slot, value in zip(slots, relations)]


def rows(value):
    return {tuple(row) for row in value}


def changes(schemas, additions, removals):
    return [(relation(s, a), relation(s, r)) for s, a, r in zip(schemas, additions, removals)]


@pytest.mark.parametrize("measured", (True, False))
def test_optimizer_preserves_typed_shared_roots_and_output_order(measured):
    types = [db.ColumnType.INT32, db.ColumnType.FLOAT64]
    left = relation([0, 1], [(1, 2.5), (2, 3.5), (3, 2.5)], types)
    right = relation([1, 2], [(2.5, 7), (3.5, 8)], types[::-1])
    repo = FACTORY.create()
    a, b = inputs_of(repo, [left, right])
    shared = query(repo, db.QueryJoinData(a, b))
    projected = query(repo, db.QueryProjectData(shared, columns(2, 0)))
    selected = query(repo, db.QuerySelectValueData(shared, db.ColumnIndex(0), db.ColumnType.INT32, 1))
    roots = [projected, selected, query(repo, db.QueryRenameData(projected, columns(8, 9)))]
    assert query(repo, db.QueryJoinData(a, b)) == shared
    assert hash(query(repo, db.QueryJoinData(a, b))) == hash(shared)
    original = db.compile(roots)
    optimized = db.optimize(roots, db.collect_statistics([left, right]) if measured else db.Statistics())
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


def test_records_views_and_insertion_identity():
    repo = FACTORY.create()
    a, b = inputs_of(repo, [relation([0, 1]), relation([1, 2])])
    data = db.QueryJoinData(a, b)
    assert data == db.QueryJoinData(a, b)
    assert data.lhs == a.get_index() and data.rhs == b.get_index()
    join, created = repo.insert(data)
    assert created
    assert isinstance(join, db.QueryJoin)
    assert isinstance(join.get_index(), db.QueryJoinIndex)
    assert join.get_lhs() == a and join.get_rhs() == b
    assert tuple(join.columns()) == tuple(columns(0, 1, 2))
    assert repo.insert(db.QueryJoinData(a, b)) == (join, False)
    erased, _ = repo.insert(db.QueryData(join))
    assert erased.get_variant() == join
    selected, _ = repo.insert(db.QuerySelectEqualData(erased, db.ColumnIndex(0), db.ColumnIndex(2)))
    assert selected.get_arg() == erased
    assert selected.get_lhs_column() == db.ColumnIndex(0)
    distance, _ = repo.insert(db.QueryDistanceData(a, inputs_of(repo, [relation([5, 6, 7, 8])])[0], a, db.ColumnIndex(9)))
    assert distance.get_distance_column() == db.ColumnIndex(9)
    generic, _ = repo.insert(db.QueryGenericJoinData([a, b], columns(1, 0, 2)))
    assert tuple(generic.columns()) == tuple(columns(0, 1, 2))
    empty, _ = repo.insert(db.QueryEmptyData(a.columns()))
    assert tuple(empty.columns()) == tuple(columns(0, 1))
    assert isinstance(repo.insert(db.QueryUnionData(a, a))[0], db.QueryUnion)
    assert isinstance(repo.insert(db.QueryDifferenceData(a, a))[0], db.QueryDifference)


def test_difference_projection_barrier_and_nullary_support():
    source = relation([0, 1], [(1, 7), (1, 8)])
    removed = relation([0, 1], [(1, 7)])
    repo = FACTORY.create()
    source_query, removed_query = inputs_of(repo, [source, removed])
    surviving = query(repo, db.QueryProjectData(query(repo, db.QueryDifferenceData(source_query, removed_query)), columns(0)))
    roots = [surviving, query(repo, db.QueryProjectData(surviving, columns()))]
    evaluation = db.QueryEvaluator(db.optimize(roots, db.collect_statistics([source, removed])))
    evaluation.evaluate([source, removed])
    assert rows(evaluation.get_result()) == {(1,)}
    assert rows(evaluation.get_result(1)) == {()}


def test_incremental_batch_changes_projection_union_and_difference():
    schemas = [[0, 1], [1, 2], [0, 2]]
    current = [{(1, 7), (1, 8), (2, 8)}, {(7, 9), (8, 9)}, {(2, 9)}]
    inputs = [relation(schema, values) for schema, values in zip(schemas, current)]
    repo = FACTORY.create()
    a, b, blocked = inputs_of(repo, inputs)
    projected = query(repo, db.QueryProjectData(query(repo, db.QueryJoinData(a, b)), columns(0, 2)))
    visible = query(repo, db.QueryDifferenceData(projected, blocked))
    combined = query(repo, db.QueryUnionData(visible, query(repo, db.QueryEmptyData(inputs[2].columns()))))
    roots = [combined, query(repo, db.QueryProjectData(visible, columns()))]
    full, maintained = db.QueryEvaluator(db.compile(roots)), db.IncrementalQueryEvaluator(db.optimize(roots, db.collect_statistics(inputs)))
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
        maintained.update(changes(schemas, additions, removals))
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
    a, b, c = inputs_of(repo, inputs)
    triangle = query(repo, db.QueryJoinData(query(repo, db.QueryJoinData(a, b)), c))
    roots = [triangle, query(repo, db.QueryJoinData(a, a))]
    evaluator = db.QueryEvaluator(db.optimize(roots, db.collect_statistics(inputs)))
    evaluator.evaluate(inputs)
    assert rows(evaluator.get_result()) == {(1, 2, 3), (2, 3, 1), (3, 1, 2)}
    assert rows(evaluator.get_result(1)) == rows(inputs[0])


def test_borrowed_and_interned_inputs_statistics_and_owners():
    original = relation([0], [(1,), (2,)])
    store = db.RelationRepositoryFactory().create()
    interned = store.insert(original)[0]
    repo = FACTORY.create()
    root = inputs_of(repo, [original])[0]
    evaluator = db.QueryEvaluator(db.compile([root]))
    evaluator.evaluate([interned])
    borrowed = evaluator.get_result()
    independent = db.assign(db.Relation(), borrowed)
    statistics = db.collect_statistics([borrowed])
    assert statistics.inputs[0].rows == 2
    assert statistics.inputs[0].distinct == {db.ColumnIndex(0): 2}
    assert db.collect_statistics([interned]).inputs[0].rows == 2
    second = db.QueryEvaluator(db.compile([root]))
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
    handle = query(repo, db.QueryInputData(0, relation([4]).columns()))
    other = query(FACTORY.create(), db.QueryInputData(0, relation([4]).columns()))
    assert handle != other
    assert len({handle, other}) == 2
    plan = db.compile([handle])
    del repo
    gc.collect()
    assert tuple(handle.columns()) == tuple(columns(4))
    del handle
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
    s, e, t = inputs_of(repo, [source, edges, target], slots=[1, 3, 4])
    distance = query(repo, db.QueryDistanceData(s, e, t, db.ColumnIndex(99)))
    optimized = db.optimize([distance])
    unused = db.Relation()
    if not incremental:
        evaluator = db.QueryEvaluator(optimized)
        evaluator.evaluate([unused, source, unused, edges, target])
    else:
        evaluator = db.IncrementalQueryEvaluator(optimized)
        evaluator.initialize([unused, source, unused, edges, target])
    assert rows(evaluator.get_result()) == {(0, 2, 2)}
    if incremental:
        no_change = (db.Relation(), db.Relation())
        evaluator.update([no_change, (relation([10]), relation([10])), no_change,
                          (relation([20, 21], [(0, 2)]), relation([20, 21])), (relation([30]), relation([30]))])
        assert rows(evaluator.get_result()) == {(0, 2, 1)}
        assert rows(evaluator.get_delta().added) == {(0, 2, 1)}
        assert rows(evaluator.get_delta().removed) == {(0, 2, 2)}


def test_statistics_and_input_validation():
    value = relation([0], [(1,)])
    repo = FACTORY.create()
    source = inputs_of(repo, [value])[0]
    assert db.optimize([source]).root_count == 1
    known = db.Statistics(objects=4, inputs={0: db.RelationStatistics(1, {db.ColumnIndex(0): 1})})
    assert db.optimize([source], known).root_count == 1
    with pytest.raises(ValueError):
        db.optimize([source], db.Statistics(inputs={0: db.RelationStatistics(-1, {db.ColumnIndex(0): 1})}))
    evaluator = db.QueryEvaluator(db.compile([source]))
    with pytest.raises(ValueError):
        evaluator.evaluate([relation([1])])
    with pytest.raises(TypeError):
        evaluator.evaluate([object()])
    with pytest.raises(IndexError):
        repo.insert(db.QueryProjectData(source, columns(5)))
    incremental = db.IncrementalQueryEvaluator(db.compile([source]))
    incremental.initialize([value])
    with pytest.raises(ValueError):
        incremental.update([])


@pytest.mark.parametrize("measured", (True, False))
def test_planning_is_deterministic(measured):
    inputs = [relation([i, i + 1], [(1, 1), (2, 2)]) for i in range(4)]
    repo = FACTORY.create()
    queries = inputs_of(repo, inputs)
    root = queries[0]
    for other in queries[1:]:
        root = query(repo, db.QueryJoinData(root, other))
    known = db.collect_statistics(inputs) if measured else db.Statistics()
    first = db.optimize([root], known)
    second = db.optimize([root], known)
    assert first.explain() == second.explain()
    evaluator = db.QueryEvaluator(first)
    evaluator.evaluate(inputs)
    assert rows(evaluator.get_result()) == {(1, 1, 1, 1, 1), (2, 2, 2, 2, 2)}


@pytest.mark.parametrize("column_type, constant", [(db.ColumnType.INT32, -3), (db.ColumnType.FLOAT64, -3.0), (db.ColumnType.BOOL, True)])
def test_selection_constants_follow_column_types_and_equality(column_type, constant):
    other = {db.ColumnType.INT32: 7, db.ColumnType.FLOAT64: 7.0, db.ColumnType.BOOL: False}[column_type]
    source = relation([0, 1], [(constant, constant), (constant, other), (other, other)], [column_type] * 2)
    repo = FACTORY.create()
    source_query = inputs_of(repo, [source])[0]
    equal = query(repo, db.QuerySelectEqualData(source_query, db.ColumnIndex(0), db.ColumnIndex(1)))
    selected = query(repo, db.QuerySelectValueData(equal, db.ColumnIndex(0), column_type, constant))
    evaluator = db.QueryEvaluator(db.optimize([selected]))
    evaluator.evaluate([source])
    assert rows(evaluator.get_result()) == {(constant, constant)}
    with pytest.raises(TypeError):
        db.QuerySelectValueData(source_query, db.ColumnIndex(0), column_type, "invalid")
    with pytest.raises(ValueError):
        repo.insert(db.QuerySelectValueData(source_query, db.ColumnIndex(0), db.ColumnType.UINT64, 1))
