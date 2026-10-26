/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "yggdrasil/database/datalog/details/compile.hpp"

#include "yggdrasil/database/datalog/details/stratification.hpp"
#include "yggdrasil/database/optimization/optimization.hpp"
#include "yggdrasil/database/syntax/operator_builder.hpp"
#include "yggdrasil/database/syntax/query_construction.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace ygg::database::datalog::detail
{
namespace
{
using Label = Index<Column>;

/// Column i of a relation; the variables of a rule are labeled past them.
Label position_label(size_t position) { return Label(to_uint_t(position)); }
constexpr uint_t variable_base = uint_t { 1 } << 16;
Label variable_label(uint_t variable) { return Label(variable_base + variable); }

std::vector<Label> positions(size_t arity)
{
    auto result = std::vector<Label>();
    for (size_t i = 0; i < arity; ++i)
        result.push_back(position_label(i));
    return result;
}

class Compiler
{
public:
    Compiler(ProgramView program, size_t unit_slot, QueryRepository& queries, QueryBuilder& builder) : m_build(queries, builder), m_unit_slot(unit_slot)
    {
        for (const auto rule : program.get_rules())
            m_rules[rule.get_head().get_relation().get_index()].push_back(rule);
        find_components(program);
    }

    /// The rows of a relation, columns labeled by position.
    QueryView<> relation(RelationView relation)
    {
        if (relation.is_input())
            return input(relation.get_input_slot(), positions(relation.get_arity()));
        if (m_scope && m_scope->contains(relation.get_index()))
            return recursion(relation);
        if (const auto it = m_derived.find(relation.get_index()); it != m_derived.end())
            return it->second;
        if (const auto it = m_component_of.find(relation.get_index()); it != m_component_of.end())
        {
            fixpoint(it->second);
            return m_derived.at(relation.get_index());
        }
        return m_derived.emplace(relation.get_index(), rules(relation)).first->second;
    }

private:
    OperatorBuilder m_build;
    size_t m_unit_slot;
    std::map<Index<Relation>, std::vector<RuleView>> m_rules;
    std::map<Index<Relation>, QueryView<>> m_derived;
    /// The recursive components (strongly connected, with a cycle) and each member's component.
    std::vector<std::vector<RelationView>> m_components;
    std::map<Index<Relation>, size_t> m_component_of;
    /// While a component's bodies are compiled: its members, read through recursion leaves.
    struct Scope
    {
        size_t id;
        std::map<Index<Relation>, size_t> variables;
        [[nodiscard]] bool contains(Index<Relation> relation) const { return variables.contains(relation); }
    };
    std::optional<Scope> m_scope;

    void find_components(ProgramView program)
    {
        auto ids = std::map<Index<Relation>, size_t>();
        auto relations = std::vector<RelationView>();
        const auto id = [&](RelationView relation)
        {
            const auto [it, inserted] = ids.try_emplace(relation.get_index(), relations.size());
            if (inserted)
                relations.push_back(relation);
            return it->second;
        };
        auto dependencies = std::vector<Dependency>();
        const auto depend = [&](size_t head, auto atoms, bool negative)
        {
            for (const auto a : atoms)
                if (!a.get_relation().is_input())
                    dependencies.push_back(Dependency { head, id(a.get_relation()), negative });
        };
        for (const auto rule : program.get_rules())
        {
            const auto head = id(rule.get_head().get_relation());
            depend(head, rule.get_atoms(), false);
            depend(head, rule.get_negated(), true);
            for (const auto aggregate : rule.get_aggregates())
            {
                depend(head, aggregate.get_atoms(), true);
                depend(head, aggregate.get_negated(), true);
            }
        }
        for (const auto& stratum : stratify(relations.size(), dependencies))
            if (stratum.recursive)
            {
                for (const auto member : stratum.relations)
                    m_component_of.emplace(relations[member].get_index(), m_components.size());
                auto& component = m_components.emplace_back();
                for (const auto member : stratum.relations)
                    component.push_back(relations[member]);
            }
    }

    /// The union of a relation's rules (empty without rules).
    QueryView<> rules(RelationView relation)
    {
        const auto it = m_rules.find(relation.get_index());
        auto result = std::optional<QueryView<>>();
        if (it != m_rules.end())
            for (const auto rule : it->second)
                result = result ? m_build.binary<QueryUnionTag>(*result, this->rule(rule)) : this->rule(rule);
        if (result)
            return *result;
        const auto columns = Builder<Columns>(std::span<const Label>(positions(relation.get_arity())));
        return m_build.empty(columns.span());
    }

    /// A member of the component being compiled, as of the previous iteration.
    QueryView<> recursion(RelationView relation)
    {
        const auto columns = Builder<Columns>(std::span<const Label>(positions(relation.get_arity())));
        return m_build.make<QueryRecursionTag>(
            [&](auto& data) { data = Data<Query<QueryRecursionTag>>(m_scope->id, m_scope->variables.at(relation.get_index()), columns.span()); });
    }

    /// A recursive component as one fixpoint per member over the members' bodies.
    void fixpoint(size_t component)
    {
        const auto& members = m_components[component];
        auto scope = Scope { component, {} };
        for (size_t v = 0; v < members.size(); ++v)
            scope.variables.emplace(members[v].get_index(), v);
        // Bodies may read lower components, compiled with their own scope meanwhile.
        auto enclosing = std::exchange(m_scope, scope);
        auto bodies = std::vector<Index<Query<>>>();
        for (const auto member : members)
            bodies.push_back(rules(member).get_index());
        m_scope = std::move(enclosing);
        for (size_t v = 0; v < members.size(); ++v)
            m_derived.emplace(members[v].get_index(),
                              m_build.make<QueryFixpointTag>([&](auto& data) { data = Data<Query<QueryFixpointTag>>(component, bodies, v); }));
    }

    QueryView<> input(size_t slot, std::span<const Label> labels)
    {
        const auto columns = Builder<Columns>(labels);
        return m_build.make<QueryInputTag>(
            [&](auto& data)
            {
                data.input_slot = slot;
                data.columns.set(columns.span().begin(), columns.span().end());
            });
    }

    QueryView<> select_value(QueryView<> query, Label column, uint_t value)
    {
        return m_build.make<QuerySelectValueTag>([&](auto& data) { data = Data<Query<QuerySelectValueTag>>(query.get_index(), column, value); });
    }
    QueryView<> select_equal(QueryView<> query, Label lhs, Label rhs)
    {
        return m_build.make<QuerySelectEqualTag>([&](auto& data) { data = Data<Query<QuerySelectEqualTag>>(query.get_index(), lhs, rhs); });
    }

    /// An atom's rows over its variables: constants and repeated variables select, the first occurrence of each
    /// variable is kept and labeled by it.
    QueryView<> atom(AtomView atom)
    {
        auto query = relation(atom.get_relation());
        auto kept = std::vector<Label>();
        auto variables = std::vector<Label>();
        auto first = std::map<uint_t, size_t>();
        const auto terms = atom.get_terms();
        for (size_t c = 0; c < terms.size(); ++c)
        {
            if (!terms[c].is_variable())
                query = select_value(query, position_label(c), terms[c].value);
            else if (const auto [it, inserted] = first.try_emplace(terms[c].value, c); !inserted)
                query = select_equal(query, position_label(it->second), position_label(c));
            else
            {
                kept.push_back(position_label(c));
                variables.push_back(variable_label(terms[c].value));
            }
        }
        return m_build.relabel<QueryRenameTag>(m_build.project(query, kept), variables);
    }

    /// The natural join of atoms, each next one sharing a variable with the joined ones where possible.
    template<typename Atoms>
    std::optional<QueryView<>> join(const Atoms& atoms)
    {
        auto pending = std::vector<QueryView<>>();
        for (const auto a : atoms)
            pending.push_back(atom(a));
        if (pending.empty())
            return std::nullopt;
        auto result = pending.front();
        pending.erase(pending.begin());
        const auto shares = [&](QueryView<> query)
        {
            return std::ranges::any_of(query.columns(),
                                       [&](const ColumnLayout& column)
                                       { return std::ranges::find(result.columns(), column.label, &ColumnLayout::label) != result.columns().end(); });
        };
        while (!pending.empty())
        {
            const auto next = std::ranges::find_if(pending, shares);
            const auto chosen = next == pending.end() ? pending.begin() : next;
            result = m_build.join(result, *chosen);
            pending.erase(chosen);
        }
        return result;
    }

    QueryView<> unit()
    {
        const auto none = std::vector<Label>();
        return input(m_unit_slot, none);
    }

    /// Restricts a body by negated atoms (antijoins) and comparisons.
    template<typename Negated>
    QueryView<> restrict(QueryView<> body, const Negated& negated, std::span<const Comparison> comparisons)
    {
        for (const auto a : negated)
            body = m_build.binary<QueryAntijoinTag>(body, atom(a));
        for (const auto& comparison : comparisons)
        {
            const auto lhs = comparison.lhs, rhs = comparison.rhs;
            auto selected = std::optional<QueryView<>>();
            if (lhs.is_variable() && rhs.is_variable())
                selected = select_equal(body, variable_label(lhs.value), variable_label(rhs.value));
            else if (lhs.is_variable() || rhs.is_variable())
                selected = select_value(body, variable_label(lhs.is_variable() ? lhs.value : rhs.value), lhs.is_variable() ? rhs.value : lhs.value);
            else if ((lhs.value == rhs.value) == (comparison.kind == Comparison::Kind::EQUAL))
                continue;
            else
            {
                body = m_build.empty(body.columns());
                continue;
            }
            body = comparison.kind == Comparison::Kind::EQUAL ? *selected : m_build.binary<QueryDifferenceTag>(body, *selected);
        }
        return body;
    }

    /// The rows of the rule body extended by the aggregate's result: the aggregate of its own body, grouped by its
    /// variables that the rule's positive atoms bind (`outer`).
    QueryView<> aggregate(AggregateView aggregate, QueryView<> body, const std::set<uint_t>& outer)
    {
        auto arg = join(aggregate.get_atoms());
        if (!arg)
            throw std::invalid_argument("Datalog compiler: an aggregate needs a positive atom.");
        const auto inner = restrict(*arg, aggregate.get_negated(), aggregate.get_comparisons());
        auto group = std::vector<Label>();
        for (const auto& column : inner.columns())
            if (outer.contains(column.label.get_value() - variable_base))
                group.push_back(column.label);
        const auto groups = m_build.project(body, group);
        const auto value = aggregate.get_function() == AggregateFunction::COUNT ? Label() : variable_label(aggregate.get_value());
        const auto result = m_build.make<QueryAggregateTag>(
            [&](auto& data)
            {
                data.arg = inner.get_index();
                data.groups = groups.get_index();
                data.function = aggregate.get_function();
                data.value = value;
                data.result = variable_label(aggregate.get_result());
            });
        return m_build.join(body, result);
    }

    QueryView<> rule(RuleView rule)
    {
        auto outer = std::set<uint_t>();
        for (const auto a : rule.get_atoms())
            for (const auto t : a.get_terms())
                if (t.is_variable())
                    outer.insert(t.value);
        auto body = join(rule.get_atoms()).value_or(unit());
        for (const auto a : rule.get_aggregates())
            body = aggregate(a, body, outer);
        body = restrict(body, rule.get_negated(), rule.get_comparisons());
        auto head = std::vector<Label>();
        for (const auto t : rule.get_head().get_terms())
            head.push_back(variable_label(t.value));
        return m_build.relabel<QueryRenameTag>(m_build.project(body, head), positions(head.size()));
    }
};
}  // namespace

QueryProgram compile(ProgramView program, std::span<const RelationView> outputs)
{
    size_t unit_slot = 0;
    for (const auto rule : program.get_rules())
    {
        const auto atoms = [&](auto range)
        {
            for (const auto a : range)
                if (a.get_relation().is_input())
                    unit_slot = std::max(unit_slot, a.get_relation().get_input_slot() + 1);
        };
        atoms(rule.get_atoms());
        atoms(rule.get_negated());
        for (const auto aggregate : rule.get_aggregates())
        {
            atoms(aggregate.get_atoms());
            atoms(aggregate.get_negated());
        }
    }
    auto repository = QueryRepositoryFactory {}.create_shared();
    auto builder = QueryBuilder();
    auto compiler = Compiler(program, unit_slot, *repository, builder);
    auto roots = std::vector<QueryView<>>();
    for (const auto output : outputs)
    {
        if (output.is_input())
            throw std::invalid_argument("Datalog compiler: outputs are derived relations.");
        roots.push_back(compiler.relation(output));
    }
    return QueryProgram { optimize(roots), unit_slot };
}
}  // namespace ygg::database::datalog::detail
