/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <algorithm>
#include <functional>
#include <gtest/gtest.h>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <stdexcept>
#include <tuple>
#include <vector>
#include <yggdrasil/database/datalog/construction.hpp>
#include <yggdrasil/database/datalog/details/compile.hpp>
#include <yggdrasil/database/datalog/evaluator.hpp>
#include <yggdrasil/database/semantics/incremental/query.hpp>
#include <yggdrasil/database/semantics/query_evaluation.hpp>
#include <yggdrasil/database/semantics/relation_repository.hpp>

namespace
{
namespace dl = ygg::database::datalog;
using ygg::uint_t;
using Tuple = std::vector<uint_t>;
using Tuples = std::set<Tuple>;
using Relation = ygg::Builder<ygg::database::Relation>;
using Change = std::pair<ygg::database::BorrowedRelationView, ygg::database::BorrowedRelationView>;
using Function = ygg::database::datalog::AggregateFunction;

dl::Term var(uint_t variable) { return dl::Term::variable(variable); }
dl::Term obj(uint_t object) { return dl::Term::constant(object); }

/// Interns programs with short calls.
struct Programs
{
    dl::DatalogRepository repository = dl::DatalogRepositoryFactory {}.create();
    dl::DatalogBuilder builder;

    dl::RelationView relation(size_t id, size_t arity, size_t slot = ygg::Data<dl::Relation>::derived)
    {
        auto data = dl::checkout<dl::Relation>(builder);
        data->id = id;
        data->arity = arity;
        data->input_slot = slot;
        return dl::insert(repository, *data).first;
    }
    dl::AtomView atom(dl::RelationView relation, std::initializer_list<dl::Term> terms)
    {
        auto data = dl::checkout<dl::Atom>(builder);
        data->relation = relation.get_index();
        for (const auto term : terms)
            data->terms.push_back(term);
        return dl::insert(repository, *data).first;
    }
    dl::AggregateView
    aggregate(Function function, uint_t result, uint_t value, std::initializer_list<dl::AtomView> atoms, std::initializer_list<dl::AtomView> negated = {})
    {
        auto data = dl::checkout<dl::Aggregate>(builder);
        data->function = function;
        data->result = result;
        data->value = value;
        for (const auto atom : atoms)
            data->atoms.push_back(atom.get_index());
        for (const auto atom : negated)
            data->negated.push_back(atom.get_index());
        return dl::insert(repository, *data).first;
    }
    dl::RuleView rule(dl::AtomView head,
                      std::initializer_list<dl::AtomView> atoms,
                      std::initializer_list<dl::AtomView> negated = {},
                      std::initializer_list<dl::Comparison> comparisons = {},
                      std::initializer_list<dl::AggregateView> aggregates = {})
    {
        auto data = dl::checkout<dl::Rule>(builder);
        data->head = head.get_index();
        for (const auto atom : atoms)
            data->atoms.push_back(atom.get_index());
        for (const auto atom : negated)
            data->negated.push_back(atom.get_index());
        for (const auto& comparison : comparisons)
            data->comparisons.push_back(comparison);
        for (const auto aggregate : aggregates)
            data->aggregates.push_back(aggregate.get_index());
        return dl::insert(repository, *data).first;
    }
    dl::ProgramView program(std::initializer_list<dl::RuleView> rules)
    {
        auto data = dl::checkout<dl::Program>(builder);
        for (const auto rule : rules)
            data->rules.push_back(rule.get_index());
        return dl::insert(repository, *data).first;
    }
};

/// Naive bottom-up evaluation, stratum by stratum, independent of the evaluator.
class Interpreter
{
    dl::ProgramView m_program;
    std::map<size_t, Tuples> m_relations;
    const std::vector<Tuples>* m_inputs;

    [[nodiscard]] const Tuples& rows(dl::RelationView relation) const
    {
        static const Tuples none;
        if (relation.is_input())
            return (*m_inputs)[relation.get_input_slot()];
        const auto it = m_relations.find(relation.get_id());
        return it == m_relations.end() ? none : it->second;
    }
    static std::optional<uint_t> value(dl::Term term, const std::map<uint_t, uint_t>& assignment)
    {
        if (!term.is_variable())
            return term.value;
        const auto it = assignment.find(term.value);
        return it == assignment.end() ? std::nullopt : std::optional(it->second);
    }
    bool matches(dl::AtomView atom, const Tuple& tuple, std::map<uint_t, uint_t>& assignment) const
    {
        const auto terms = atom.get_terms();
        for (size_t i = 0; i < terms.size(); ++i)
        {
            if (const auto bound = value(terms[i], assignment))
            {
                if (*bound != tuple[i])
                    return false;
            }
            else
                assignment[terms[i].value] = tuple[i];
        }
        return true;
    }
    template<typename Atoms>
    void
    enumerate(const Atoms& atoms, size_t i, const std::map<uint_t, uint_t>& assignment, const std::function<void(const std::map<uint_t, uint_t>&)>& f) const
    {
        if (i == atoms.size())
            return f(assignment);
        for (const auto& tuple : rows(atoms[i].get_relation()))
        {
            auto extended = assignment;
            if (matches(atoms[i], tuple, extended))
                enumerate(atoms, i + 1, extended, f);
        }
    }
    template<typename Negated>
    [[nodiscard]] bool passes(const Negated& negated, std::span<const dl::Comparison> comparisons, const std::map<uint_t, uint_t>& assignment) const
    {
        for (const auto& comparison : comparisons)
            if ((value(comparison.lhs, assignment).value() == value(comparison.rhs, assignment).value()) != (comparison.kind == dl::Comparison::Kind::EQUAL))
                return false;
        for (const auto atom : negated)
        {
            auto tuple = Tuple();
            for (const auto term : atom.get_terms())
                tuple.push_back(value(term, assignment).value());
            if (rows(atom.get_relation()).contains(tuple))
                return false;
        }
        return true;
    }
    /// The aggregate over the assignments of its variables that extend `outer` on the variables both bind.
    [[nodiscard]] std::optional<uint_t> aggregate(dl::AggregateView aggregate, const std::map<uint_t, uint_t>& outer) const
    {
        const auto atoms = std::vector<dl::AtomView>(aggregate.get_atoms().begin(), aggregate.get_atoms().end());
        const auto negated = std::vector<dl::AtomView>(aggregate.get_negated().begin(), aggregate.get_negated().end());
        std::set<std::map<uint_t, uint_t>> assignments;
        enumerate(atoms,
                  0,
                  {},
                  [&](const auto& inner)
                  {
                      for (const auto& [variable, object] : inner)
                          if (const auto it = outer.find(variable); it != outer.end() && it->second != object)
                              return;
                      if (passes(negated, aggregate.get_comparisons(), inner))
                          assignments.insert(inner);
                  });
        if (aggregate.get_function() == Function::COUNT)
            return static_cast<uint_t>(assignments.size());
        std::vector<uint_t> values;
        values.reserve(assignments.size());
        for (const auto& assignment : assignments)
            values.push_back(assignment.at(aggregate.get_value()));
        if (aggregate.get_function() == Function::SUM)
        {
            uint_t sum = 0;
            for (const auto v : values)
                sum += v;
            return sum;
        }
        if (values.empty())
            return std::nullopt;
        return aggregate.get_function() == Function::MIN ? *std::ranges::min_element(values) : *std::ranges::max_element(values);
    }
    bool apply(dl::RuleView rule)
    {
        const auto atoms = std::vector<dl::AtomView>(rule.get_atoms().begin(), rule.get_atoms().end());
        const auto negated = std::vector<dl::AtomView>(rule.get_negated().begin(), rule.get_negated().end());
        const auto head = rule.get_head();
        auto derived = Tuples();
        enumerate(atoms,
                  0,
                  {},
                  [&](const auto& base)
                  {
                      auto assignment = base;
                      for (const auto aggregate : rule.get_aggregates())
                      {
                          const auto result = this->aggregate(aggregate, base);
                          if (!result)
                              return;
                          assignment[aggregate.get_result()] = *result;
                      }
                      if (!passes(negated, rule.get_comparisons(), assignment))
                          return;
                      auto tuple = Tuple();
                      for (const auto term : head.get_terms())
                          tuple.push_back(value(term, assignment).value());
                      derived.insert(tuple);
                  });
        auto& target = m_relations[head.get_relation().get_id()];
        const auto size = target.size();
        target.insert(derived.begin(), derived.end());
        return target.size() != size;
    }

public:
    Interpreter(dl::ProgramView program, const std::vector<Tuples>& inputs) : m_program(program), m_inputs(&inputs)
    {
        // A relation's stratum is at least its positive dependencies' and above its negative and aggregated ones.
        std::map<size_t, size_t> stratum;
        for (bool changed = true; changed;)
        {
            changed = false;
            for (const auto rule : program.get_rules())
            {
                auto& level = stratum[rule.get_head().get_relation().get_id()];
                const auto require = [&](dl::AtomView atom, size_t above)
                {
                    if (atom.get_relation().is_input())
                        return;
                    const auto needed = stratum[atom.get_relation().get_id()] + above;
                    if (needed > level)
                    {
                        level = needed;
                        changed = true;
                    }
                };
                for (const auto atom : rule.get_atoms())
                    require(atom, 0);
                for (const auto atom : rule.get_negated())
                    require(atom, 1);
                for (const auto aggregate : rule.get_aggregates())
                    for (const auto atom : aggregate.get_atoms())
                        require(atom, 1);
            }
        }
        size_t top = 0;
        for (const auto& [relation, level] : stratum)
            top = std::max(top, level);
        for (size_t level = 0; level <= top; ++level)
            for (bool changed = true; changed;)
            {
                changed = false;
                for (const auto rule : program.get_rules())
                    if (stratum[rule.get_head().get_relation().get_id()] == level)
                        changed = apply(rule) || changed;
            }
    }
    [[nodiscard]] const Tuples& get(size_t relation) const
    {
        static const Tuples none;
        const auto it = m_relations.find(relation);
        return it == m_relations.end() ? none : it->second;
    }
};

ygg::database::BorrowedRelationView borrow(const Relation& relation)
{
    static const auto context = ygg::database::RelationRepositoryFactory {}.create();
    return { relation, context };
}

std::vector<ygg::Index<ygg::database::Column>> labels(size_t arity)
{
    std::vector<ygg::Index<ygg::database::Column>> result;
    result.reserve(arity);
    for (size_t i = 0; i < arity; ++i)
        result.emplace_back(ygg::to_uint_t(i));
    return result;
}

Relation relation_of(const Tuples& tuples, size_t arity)
{
    const auto columns = labels(arity);
    Relation result { std::span<const ygg::Index<ygg::database::Column>>(columns) };
    for (const auto& tuple : tuples)
    {
        std::vector<std::byte> bytes(arity * ygg::ByteCodec<uint_t>::size);
        for (size_t i = 0; i < arity; ++i)
            ygg::ByteCodec<uint_t>::encode(tuple[i], std::span(bytes).subspan(i * ygg::ByteCodec<uint_t>::size, ygg::ByteCodec<uint_t>::size));
        result.insert(std::span<const std::byte>(bytes));
    }
    return result;
}

Tuples tuples_of(const Relation& relation)
{
    Tuples result;
    for (size_t i = 0; i < relation.size(); ++i)
    {
        const ygg::database::Row row(relation.row(i), relation.columns().span());
        Tuple tuple;
        for (size_t c = 0; c < relation.arity(); ++c)
            tuple.push_back(row.get<uint_t>(c));
        result.insert(tuple);
    }
    return result;
}

/// The test programs over the inputs e (binary, slot 0) and v (unary, slot 1).
struct Fixture
{
    Programs p;
    dl::RelationView e = p.relation(100, 2, 0), v = p.relation(101, 1, 1);
    dl::RelationView tc = p.relation(0, 2), tc2 = p.relation(1, 2), odd = p.relation(2, 2), even = p.relation(3, 2), unreachable = p.relation(4, 2),
                     loops = p.relation(5, 1), from3 = p.relation(6, 1), distinct = p.relation(7, 2), into2 = p.relation(8, 1), degree = p.relation(9, 2),
                     edges = p.relation(10, 1), largest = p.relation(11, 2), smallest = p.relation(12, 2), weight = p.relation(13, 2),
                     reach = p.relation(14, 2), isolated = p.relation(15, 1), has_out = p.relation(16, 1), triangle = p.relation(17, 3),
                     cycle = p.relation(18, 2);
    std::vector<dl::RelationView> outputs { tc,     tc2,   odd,     even,     unreachable, loops, from3,    distinct, into2,
                                            degree, edges, largest, smallest, weight,      reach, isolated, triangle, cycle };
    dl::ProgramView program = make();

    dl::ProgramView make()
    {
        constexpr uint_t x = 0, y = 1, z = 2, n = 3;
        using enum dl::Comparison::Kind;
        return p.program({
            // Transitive closure, linear and nonlinear.
            p.rule(p.atom(tc, { var(x), var(y) }), { p.atom(e, { var(x), var(y) }) }),
            p.rule(p.atom(tc, { var(x), var(z) }), { p.atom(tc, { var(x), var(y) }), p.atom(e, { var(y), var(z) }) }),
            p.rule(p.atom(tc2, { var(x), var(y) }), { p.atom(e, { var(x), var(y) }) }),
            p.rule(p.atom(tc2, { var(x), var(z) }), { p.atom(tc2, { var(x), var(y) }), p.atom(tc2, { var(y), var(z) }) }),
            // Mutual recursion.
            p.rule(p.atom(odd, { var(x), var(y) }), { p.atom(e, { var(x), var(y) }) }),
            p.rule(p.atom(odd, { var(x), var(z) }), { p.atom(even, { var(x), var(y) }), p.atom(e, { var(y), var(z) }) }),
            p.rule(p.atom(even, { var(x), var(z) }), { p.atom(odd, { var(x), var(y) }), p.atom(e, { var(y), var(z) }) }),
            // Stratified negation.
            p.rule(p.atom(unreachable, { var(x), var(y) }), { p.atom(v, { var(x) }), p.atom(v, { var(y) }) }, { p.atom(tc, { var(x), var(y) }) }),
            p.rule(p.atom(has_out, { var(x) }), { p.atom(e, { var(x), var(y) }) }),
            p.rule(p.atom(isolated, { var(x) }), { p.atom(v, { var(x) }) }, { p.atom(has_out, { var(x) }) }),
            // Repeated variables, constants and comparisons.
            p.rule(p.atom(loops, { var(x) }), { p.atom(e, { var(x), var(x) }) }),
            p.rule(p.atom(from3, { var(y) }), { p.atom(e, { obj(3), var(y) }) }),
            p.rule(p.atom(distinct, { var(x), var(y) }), { p.atom(e, { var(x), var(y) }) }, {}, { dl::Comparison { NOT_EQUAL, var(x), var(y) } }),
            p.rule(p.atom(into2, { var(x) }), { p.atom(e, { var(x), var(y) }) }, {}, { dl::Comparison { EQUAL, var(y), obj(2) } }),
            // Cyclic bodies (Generic Join), also recursive.
            p.rule(p.atom(triangle, { var(x), var(y), var(z) }),
                   { p.atom(e, { var(x), var(y) }), p.atom(e, { var(y), var(z) }), p.atom(e, { var(z), var(x) }) }),
            p.rule(p.atom(cycle, { var(x), var(y) }), { p.atom(e, { var(x), var(y) }), p.atom(e, { var(y), var(x) }) }),
            p.rule(p.atom(cycle, { var(x), var(z) }), { p.atom(cycle, { var(x), var(y) }), p.atom(e, { var(y), var(z) }), p.atom(tc, { var(z), var(x) }) }),
            // Aggregates, grouped by the rule's other variables, also over a lower stratum's recursive relation.
            p.rule(p.atom(degree, { var(x), var(n) }),
                   { p.atom(v, { var(x) }) },
                   {},
                   {},
                   { p.aggregate(Function::COUNT, n, 0, { p.atom(e, { var(x), var(y) }) }) }),
            p.rule(p.atom(edges, { var(n) }), {}, {}, {}, { p.aggregate(Function::COUNT, n, 0, { p.atom(e, { var(x), var(y) }) }) }),
            p.rule(p.atom(largest, { var(x), var(n) }),
                   { p.atom(v, { var(x) }) },
                   {},
                   {},
                   { p.aggregate(Function::MAX, n, y, { p.atom(e, { var(x), var(y) }) }) }),
            p.rule(p.atom(smallest, { var(x), var(n) }),
                   { p.atom(v, { var(x) }) },
                   {},
                   {},
                   { p.aggregate(Function::MIN, n, y, { p.atom(tc, { var(x), var(y) }) }) }),
            p.rule(p.atom(weight, { var(x), var(n) }),
                   { p.atom(v, { var(x) }) },
                   {},
                   {},
                   { p.aggregate(Function::SUM, n, y, { p.atom(e, { var(x), var(y) }) }) }),
            p.rule(p.atom(reach, { var(x), var(n) }),
                   { p.atom(v, { var(x) }) },
                   {},
                   {},
                   { p.aggregate(Function::COUNT, n, 0, { p.atom(tc, { var(x), var(y) }) }) }),
        });
    }
};
}  // namespace

TEST(DatabaseDatalog, EvaluatorEqualsNaiveEvaluation)
{
    Fixture fixture;
    dl::Evaluator evaluator(fixture.program, fixture.outputs);
    std::mt19937 random(5);
    for (size_t trial = 0; trial < 60; ++trial)
    {
        std::vector<Tuples> inputs(2);
        for (size_t i = 0, count = random() % 14; i < count; ++i)
            inputs[0].insert({ static_cast<uint_t>(random() % 6), static_cast<uint_t>(random() % 6) });
        for (uint_t vertex = 0; vertex < 6; ++vertex)
            if (random() % 4 != 0)
                inputs[1].insert({ vertex });
        const auto e = relation_of(inputs[0], 2), v = relation_of(inputs[1], 1);
        evaluator.evaluate(std::array { borrow(e), borrow(v) });
        const Interpreter expected(fixture.program, inputs);
        for (size_t i = 0; i < fixture.outputs.size(); ++i)
            EXPECT_EQ(tuples_of(evaluator.get_result(i)), expected.get(fixture.outputs[i].get_id()))
                << "trial " << trial << ", relation " << fixture.outputs[i].get_id();
    }
}

TEST(DatabaseDatalog, CompiledQueriesEqualNaiveEvaluation)
{
    Fixture fixture;
    const auto& outputs = fixture.outputs;
    const auto compiled = dl::detail::compile(fixture.program, outputs);
    ASSERT_EQ(compiled.unit_slot, 2);
    ygg::database::QueryEvaluator full(compiled.plan);
    ygg::database::incremental::QueryEvaluator incremental(compiled.plan);
    auto unit = Relation(std::span<const ygg::Index<ygg::database::Column>>());
    unit.insert(std::span<const std::byte>());
    std::mt19937 random(5);
    std::vector<Tuples> current(2);
    incremental.initialize(std::array { borrow(relation_of(current[0], 2)), borrow(relation_of(current[1], 1)), borrow(unit) });
    for (size_t trial = 0; trial < 60; ++trial)
    {
        std::vector<Tuples> inputs(2);
        for (size_t i = 0, count = random() % 14; i < count; ++i)
            inputs[0].insert({ static_cast<uint_t>(random() % 6), static_cast<uint_t>(random() % 6) });
        for (uint_t vertex = 0; vertex < 6; ++vertex)
            if (random() % 4 != 0)
                inputs[1].insert({ vertex });
        const auto e = relation_of(inputs[0], 2), v = relation_of(inputs[1], 1);
        full.evaluate(std::array { borrow(e), borrow(v), borrow(unit) });
        std::vector<Tuples> added(2), removed(2);
        for (size_t slot = 0; slot < 2; ++slot)
        {
            std::ranges::set_difference(inputs[slot], current[slot], std::inserter(added[slot], added[slot].end()));
            std::ranges::set_difference(current[slot], inputs[slot], std::inserter(removed[slot], removed[slot].end()));
        }
        const auto ea = relation_of(added[0], 2), er = relation_of(removed[0], 2), va = relation_of(added[1], 1), vr = relation_of(removed[1], 1);
        const auto none = Relation(std::span<const ygg::Index<ygg::database::Column>>());
        incremental.update(std::array { Change { borrow(ea), borrow(er) }, Change { borrow(va), borrow(vr) }, Change { borrow(none), borrow(none) } });
        current = inputs;
        const Interpreter expected(fixture.program, inputs);
        for (size_t i = 0; i < outputs.size(); ++i)
        {
            EXPECT_EQ(tuples_of(full.get_result(i)), expected.get(outputs[i].get_id())) << "trial " << trial << ", relation " << outputs[i].get_id();
            EXPECT_EQ(tuples_of(incremental.get_result(i)), expected.get(outputs[i].get_id())) << "trial " << trial << ", relation " << outputs[i].get_id();
        }
    }
}

TEST(DatabaseDatalog, EvaluatorUpdatesEqualNaiveEvaluationAfterEveryBatch)
{
    Fixture fixture;
    dl::Evaluator evaluator(fixture.program, fixture.outputs);
    std::vector<Tuples> current(2);
    evaluator.initialize(std::array { borrow(relation_of(current[0], 2)), borrow(relation_of(current[1], 1)) });
    std::mt19937 random(9);
    for (size_t batch = 0; batch < 400; ++batch)
    {
        std::vector<Tuples> added(2), removed(2);
        const size_t removing = random() % 4;
        for (size_t i = 0; i < 3; ++i)
        {
            const size_t slot = random() % 2;
            const size_t arity = slot == 0 ? 2 : 1;
            if (removing == slot && !current[slot].empty())
            {
                auto it = current[slot].begin();
                std::advance(it, random() % current[slot].size());
                if (!added[slot].contains(*it))
                    removed[slot].insert(*it);
                continue;
            }
            Tuple tuple;
            for (size_t c = 0; c < arity; ++c)
                tuple.push_back(static_cast<uint_t>(random() % 6));
            if (!current[slot].contains(tuple))
                added[slot].insert(tuple);
        }
        std::vector<Tuples> before;
        before.reserve(fixture.outputs.size());
        for (size_t i = 0; i < fixture.outputs.size(); ++i)
            before.push_back(tuples_of(evaluator.get_result(i)));
        const auto ea = relation_of(added[0], 2), er = relation_of(removed[0], 2), va = relation_of(added[1], 1), vr = relation_of(removed[1], 1);
        evaluator.update(std::array { Change { borrow(ea), borrow(er) }, Change { borrow(va), borrow(vr) } });
        for (size_t slot = 0; slot < 2; ++slot)
        {
            for (const auto& tuple : removed[slot])
                current[slot].erase(tuple);
            current[slot].insert(added[slot].begin(), added[slot].end());
        }
        const Interpreter expected(fixture.program, current);
        for (size_t i = 0; i < fixture.outputs.size(); ++i)
        {
            const auto relation = fixture.outputs[i].get_id();
            const auto& after = expected.get(relation);
            ASSERT_EQ(tuples_of(evaluator.get_result(i)), after) << "batch " << batch << ", relation " << relation;
            Tuples gained, lost;
            std::ranges::set_difference(after, before[i], std::inserter(gained, gained.end()));
            std::ranges::set_difference(before[i], after, std::inserter(lost, lost.end()));
            EXPECT_EQ(tuples_of(evaluator.get_delta(i).added), gained) << "batch " << batch << ", relation " << relation;
            EXPECT_EQ(tuples_of(evaluator.get_delta(i).removed), lost) << "batch " << batch << ", relation " << relation;
        }
    }
}
TEST(DatabaseDatalog, EvaluatorReadsOnlyWhatItsOutputsDependOn)
{
    // tc reads e alone: v's slot is not read, so a relation of the wrong arity there is fine.
    Fixture fixture;
    dl::Evaluator evaluator(fixture.program, std::array { fixture.tc });
    const std::vector<Tuples> inputs { { { 0, 1 }, { 1, 2 }, { 2, 0 } }, {} };
    const auto e = relation_of(inputs[0], 2), wrong = relation_of({ { 0, 0, 0 } }, 3);
    evaluator.evaluate(std::array { borrow(e), borrow(wrong) });
    EXPECT_EQ(tuples_of(evaluator.get_result()), Interpreter(fixture.program, inputs).get(fixture.tc.get_id()));
}

TEST(DatabaseDatalog, DeleteAndRederiveDropsRowsThatOnlySupportEachOther)
{
    // reach(x) <- start(x);  reach(y) <- reach(x), e(x, y), over a <-> cycle b -> c -> b entered from a.
    Programs p;
    const auto e = p.relation(100, 2, 0), start = p.relation(101, 1, 1), reach = p.relation(0, 1);
    constexpr uint_t x = 0, y = 1, a = 0, b = 1, c = 2, d = 3;
    const auto program = p.program({ p.rule(p.atom(reach, { var(x) }), { p.atom(start, { var(x) }) }),
                                     p.rule(p.atom(reach, { var(y) }), { p.atom(reach, { var(x) }), p.atom(e, { var(x), var(y) }) }) });
    dl::Evaluator evaluator(program, std::array { reach });
    const auto edges = relation_of({ { a, b }, { b, c }, { c, b }, { d, c } }, 2), starts = relation_of({ { a } }, 1);
    evaluator.initialize(std::array { borrow(edges), borrow(starts) });
    EXPECT_EQ(tuples_of(evaluator.get_result()), (Tuples { { a }, { b }, { c } }));
    const auto none2 = relation_of({}, 2), none1 = relation_of({}, 1);
    // Removing a -> b leaves b and c supporting each other only: both go.
    const auto cut = relation_of({ { a, b } }, 2);
    evaluator.update(std::array { Change { borrow(none2), borrow(cut) }, Change { borrow(none1), borrow(none1) } });
    EXPECT_EQ(tuples_of(evaluator.get_result()), (Tuples { { a } }));
    EXPECT_EQ(tuples_of(evaluator.get_delta().removed), (Tuples { { b }, { c } }));
    // Starting from d as well, then cutting a's other edge: c and b are rederived through d.
    const auto from_d = relation_of({ { d } }, 1);
    evaluator.update(std::array { Change { borrow(cut), borrow(none2) }, Change { borrow(from_d), borrow(none1) } });
    EXPECT_EQ(tuples_of(evaluator.get_result()), (Tuples { { a }, { b }, { c }, { d } }));
    evaluator.update(std::array { Change { borrow(none2), borrow(cut) }, Change { borrow(none1), borrow(none1) } });
    EXPECT_EQ(tuples_of(evaluator.get_result()), (Tuples { { a }, { b }, { c }, { d } }));
    EXPECT_TRUE(evaluator.get_delta().added.empty());
    EXPECT_TRUE(evaluator.get_delta().removed.empty());
}

TEST(DatabaseDatalog, RejectsInvalidRules)
{
    Programs p;
    const auto e = p.relation(100, 2, 0);
    const auto r = p.relation(0, 2), s = p.relation(1, 1);
    constexpr uint_t x = 0, y = 1, z = 2, n = 3;
    EXPECT_THROW(p.atom(e, { var(x) }), std::invalid_argument);                                                     // arity
    EXPECT_THROW(p.rule(p.atom(e, { var(x), var(y) }), { p.atom(e, { var(x), var(y) }) }), std::invalid_argument);  // input head
    EXPECT_THROW(p.rule(p.atom(r, { var(x), var(x) }), { p.atom(e, { var(x), var(x) }) }), std::invalid_argument);  // repeated head
    EXPECT_THROW(p.rule(p.atom(r, { var(x), obj(1) }), { p.atom(e, { var(x), var(y) }) }), std::invalid_argument);  // constant head
    EXPECT_THROW(p.rule(p.atom(r, { var(x), var(z) }), { p.atom(e, { var(x), var(y) }) }), std::invalid_argument);  // unsafe head
    EXPECT_THROW(p.rule(p.atom(s, { var(x) }), { p.atom(e, { var(x), var(y) }) }, { p.atom(e, { var(z), var(x) }) }),
                 std::invalid_argument);                                                                         // unsafe negation
    EXPECT_THROW(p.aggregate(Function::SUM, n, z, { p.atom(e, { var(x), var(y) }) }), std::invalid_argument);    // unbound value
    EXPECT_THROW(p.aggregate(Function::COUNT, x, 0, { p.atom(e, { var(x), var(y) }) }), std::invalid_argument);  // result in body

    // Not stratified: r depends on itself through negation.
    const auto program = p.program({ p.rule(p.atom(s, { var(x) }), { p.atom(e, { var(x), var(y) }) }, { p.atom(s, { var(y) }) }) });
    EXPECT_THROW(dl::Evaluator(program, std::array { s }), std::invalid_argument);
}
