# Copyright (C) 2026 Dominik Drexler
# SPDX-License-Identifier: GPL-3.0-or-later

import random
from collections.abc import Iterable
from typing import cast

import pytest

from pyyggdrasil import database as db

DATALOG = db.DatalogRepositoryFactory()
Rows = set[tuple[int, ...]]


def relation(arity: int, values: Iterable[tuple[int, ...]] = ()) -> db.Relation:
    result = db.Relation([db.ColumnIndex(i) for i in range(arity)])
    for value in values:
        result.insert(value)
    return result


def rows(value: db.BorrowedRelation) -> Rows:
    return {tuple(cast("int", value[i][j]) for j in range(value.arity())) for i in range(len(value))}


def closure(edges: Iterable[tuple[int, int]]) -> Rows:
    result = set(edges)
    while True:
        derived = {(a, d) for a, b in result for c, d in result if b == c} - result
        if not derived:
            return set[tuple[int, ...]](result)
        result |= derived


class Programs:
    """Interns Datalog records with short calls."""

    def __init__(self) -> None:
        self.repository = DATALOG.create()

    def relation(self, relation_id: int, arity: int, input_slot: int | None = None) -> db.DatalogRelation:
        return self.repository.insert(db.DatalogRelationData(relation_id, arity, input_slot))[0]

    def atom(self, relation: db.DatalogRelation, *variables: int) -> db.DatalogAtom:
        return self.repository.insert(db.DatalogAtomData(relation, [db.DatalogTerm.variable(v) for v in variables]))[0]

    def rule(
        self,
        head: db.DatalogAtom,
        atoms: list[db.DatalogAtom],
        negated: list[db.DatalogAtom] | None = None,
        aggregates: list[db.DatalogAggregate] | None = None,
    ) -> db.DatalogRule:
        return self.repository.insert(db.DatalogRuleData(head, atoms, negated or [], [], aggregates or []))[0]

    def program(self, rules: list[db.DatalogRule]) -> db.DatalogProgram:
        return self.repository.insert(db.DatalogProgramData(rules))[0]


def test_closure_degree_and_negation() -> None:
    p = Programs()
    e, v = p.relation(100, 2, 0), p.relation(101, 1, 1)
    tc, degree, sink = p.relation(0, 2), p.relation(1, 2), p.relation(2, 1)
    has_out = p.relation(3, 1)
    x, y, z, n = 0, 1, 2, 3
    count = p.repository.insert(db.DatalogAggregateData(db.DatalogAggregateFunction.COUNT, n, 0, [p.atom(e, x, y)]))[0]
    program = p.program(
        [
            p.rule(p.atom(tc, x, y), [p.atom(e, x, y)]),
            p.rule(p.atom(tc, x, z), [p.atom(tc, x, y), p.atom(e, y, z)]),
            p.rule(p.atom(degree, x, n), [p.atom(v, x)], aggregates=[count]),
            p.rule(p.atom(has_out, x), [p.atom(e, x, y)]),
            p.rule(p.atom(sink, x), [p.atom(v, x)], negated=[p.atom(has_out, x)]),
        ]
    )
    full = db.DatalogEvaluator(program, [tc, degree, sink])
    generator = random.Random(3)
    for _ in range(20):
        edges = {(generator.randrange(6), generator.randrange(6)) for _ in range(generator.randrange(10))}
        full.evaluate(
            [db.BorrowedRelation(relation(2, edges)), db.BorrowedRelation(relation(1, {(i,) for i in range(6)}))]
        )
        assert rows(full.get_result(0)) == closure(edges)
        assert rows(full.get_result(1)) == {(i, sum(1 for a, _ in edges if a == i)) for i in range(6)}
        assert rows(full.get_result(2)) == {(i,) for i in range(6) if all(a != i for a, _ in edges)}


def test_arithmetic_and_order() -> None:
    # sums(x, y, x + y) for edges x < y; the sum is computed before the comparison reads it.
    p = Programs()
    e, sums = p.relation(100, 2, 0), p.relation(0, 3)
    x, y, n = 0, 1, 2
    var = db.DatalogTerm.variable
    rule = p.repository.insert(
        db.DatalogRuleData(
            p.atom(sums, x, y, n),
            [p.atom(e, x, y)],
            comparisons=[db.DatalogComparison(db.DatalogComparisonKind.LESS, var(x), var(y))],
            arithmetic=[db.DatalogArithmetic(db.DatalogArithmeticOperation.ADD, var(x), var(y), n)],
        )
    )[0]
    evaluator = db.DatalogEvaluator(p.program([rule]), [sums])
    edges = {(1, 2), (3, 1), (2, 5), (4, 4)}
    evaluator.evaluate([db.BorrowedRelation(relation(2, edges))])
    assert rows(evaluator.get_result(0)) == {(a, b, a + b) for a, b in edges if a < b}


def test_rejects_unsafe_rules() -> None:
    p = Programs()
    e, r = p.relation(100, 2, 0), p.relation(0, 2)
    with pytest.raises(ValueError, match="not bound by a positive atom"):
        p.rule(p.atom(r, 0, 2), [p.atom(e, 0, 1)])
    with pytest.raises(ValueError, match="do not match its relation's arity"):
        p.repository.insert(db.DatalogAtomData(e, [db.DatalogTerm.variable(0)]))


def test_evaluator_full_and_updates() -> None:
    p = Programs()
    e, start = p.relation(100, 2, 0), p.relation(101, 1, 1)
    reach = p.relation(0, 1)
    x, y = 0, 1
    program = p.program(
        [
            p.rule(p.atom(reach, x), [p.atom(start, x)]),
            p.rule(p.atom(reach, y), [p.atom(reach, x), p.atom(e, x, y)]),
        ]
    )
    evaluator = db.DatalogEvaluator(program, [reach], static_slots=[1])
    starts = relation(1, {(0,)})
    edges = {(0, 1), (1, 2), (2, 1), (3, 2)}
    evaluator.initialize([db.BorrowedRelation(relation(2, edges)), db.BorrowedRelation(starts)])
    assert rows(evaluator.get_result()) == {(0,), (1,), (2,)}
    none2, none1 = relation(2), relation(1)
    # Removing 0 -> 1 leaves 1 and 2 supporting each other only.
    evaluator.update(
        [
            (db.BorrowedRelation(none2), db.BorrowedRelation(relation(2, {(0, 1)}))),
            (db.BorrowedRelation(none1), db.BorrowedRelation(none1)),
        ]
    )
    assert rows(evaluator.get_result()) == {(0,)}
    delta = evaluator.get_delta()
    assert rows(delta.removed) == {(1,), (2,)}
    assert rows(delta.added) == set()

    full = db.DatalogEvaluator(program, [reach])
    full.evaluate([db.BorrowedRelation(relation(2, edges - {(0, 1)})), db.BorrowedRelation(starts)])
    assert rows(full.get_result()) == {(0,)}
