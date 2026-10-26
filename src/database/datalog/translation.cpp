/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/datalog/details/program.hpp"
#include "yggdrasil/database/datalog/details/stratification.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <vector>

/// Translates a program's records into tables and rules over variable slots, and groups them into strata.
namespace ygg::database::datalog::detail
{
namespace
{
class Translator
{
public:
    [[nodiscard]] CompiledProgram& program() noexcept { return m_program; }
    [[nodiscard]] const std::vector<Dependency>& dependencies() const noexcept { return m_dependencies; }

    size_t table_of(RelationView relation)
    {
        auto& map = relation.is_input() ? m_input_tables : m_derived_tables;
        const auto key = relation.is_input() ? relation.get_input_slot() : relation.get_id();
        if (const auto it = map.find(key); it != map.end())
            return it->second;
        const auto table = add_table(relation.get_arity());
        map.emplace(key, table);
        if (relation.is_input())
        {
            m_program.tables[table]->slot = key;
            if (m_program.slot_tables.size() <= key)
                m_program.slot_tables.resize(key + 1, npos);
            m_program.slot_tables[key] = table;
        }
        return table;
    }

    void add_rule(RuleView view)
    {
        auto rule = CompiledRule {};
        auto slot_of = std::map<uint_t, uint_t>();
        // Positive atoms first, so that the outer variables an aggregate groups by are known.
        auto positives = std::vector<Literal>();
        body(view.get_atoms(), std::span<const AtomView>(), {}, slot_of, positives);
        auto outer = std::set<uint_t>();
        for (const auto atom : view.get_atoms())
            for (const auto t : atom.get_terms())
                if (t.is_variable())
                    outer.insert(t.value);
        auto aggregate_literals = std::vector<Literal>();
        for (const auto aggregate : view.get_aggregates())
            aggregate_literals.push_back(add_aggregate(aggregate, outer, slot_of));
        auto others = std::vector<Literal>();
        body(std::span<const AtomView>(), view.get_negated(), view.get_comparisons(), slot_of, others);
        rule.literals = std::move(positives);
        rule.literals.insert(rule.literals.end(), aggregate_literals.begin(), aggregate_literals.end());
        rule.literals.insert(rule.literals.end(), others.begin(), others.end());
        const auto head = view.get_head();
        rule.head = table_of(head.get_relation());
        for (const auto t : head.get_terms())
            rule.head_terms.push_back(Term::variable(slot_of.at(t.value)));
        rule.slots = slot_of.size();
        finish_rule(std::move(rule));
    }

private:
    CompiledProgram m_program;
    std::vector<Dependency> m_dependencies;
    std::map<size_t, size_t> m_derived_tables;
    std::map<size_t, size_t> m_input_tables;

    size_t add_table(size_t arity)
    {
        m_program.tables.push_back(std::make_unique<Table>(arity));
        return m_program.tables.size() - 1;
    }

    /// The literals of a body; variables get slots of `slot_of`.
    template<typename Atoms, typename Negated>
    void body(const Atoms& atoms, const Negated& negated, std::span<const Comparison> comparisons, std::map<uint_t, uint_t>& slot_of, std::vector<Literal>& out)
    {
        const auto term = [&](Term t)
        {
            if (!t.is_variable())
                return t;
            const auto [it, inserted] = slot_of.try_emplace(t.value, to_uint_t(slot_of.size()));
            return Term::variable(it->second);
        };
        const auto literal = [&](Literal::Kind kind, AtomView atom)
        {
            auto result = Literal { kind, table_of(atom.get_relation()), {} };
            for (const auto t : atom.get_terms())
                result.terms.push_back(term(t));
            return result;
        };
        for (const auto atom : atoms)
            out.push_back(literal(Literal::Kind::POSITIVE, atom));
        for (const auto atom : negated)
            out.push_back(literal(Literal::Kind::NEGATED, atom));
        for (const auto& comparison : comparisons)
        {
            auto result = Literal { Literal::Kind::COMPARISON, npos, { term(comparison.lhs), term(comparison.rhs) } };
            result.comparison = comparison.kind;
            out.push_back(std::move(result));
        }
    }

    void finish_rule(CompiledRule rule)
    {
        for (const auto& literal : rule.literals)
            if (literal.table != npos)
                m_dependencies.push_back(Dependency { rule.head, literal.table, literal.kind != Literal::Kind::POSITIVE });
        m_program.rules.push_back(std::move(rule));
    }

    /// An aggregate reads the table of its body's satisfying assignments (a rule of its own) and keeps the groups of
    /// that table; the enclosing rule reads its groups with the variables both bind.
    Literal add_aggregate(AggregateView view, const std::set<uint_t>& outer, std::map<uint_t, uint_t>& outer_slots)
    {
        auto inner_slots = std::map<uint_t, uint_t>();
        auto inner = CompiledRule {};
        body(view.get_atoms(), view.get_negated(), view.get_comparisons(), inner_slots, inner.literals);
        auto variables = std::vector<uint_t>();  // In slot order.
        variables.resize(inner_slots.size());
        for (const auto& [variable, slot] : inner_slots)
            variables[slot] = variable;
        const auto source = add_table(variables.size());
        inner.head = source;
        for (uint_t slot = 0; slot < variables.size(); ++slot)
            inner.head_terms.push_back(Term::variable(slot));
        inner.slots = inner_slots.size();
        finish_rule(std::move(inner));

        auto aggregate = AggregateTable {};
        aggregate.source = source;
        aggregate.function = view.get_function();
        auto literal = Literal { Literal::Kind::AGGREGATE, npos, {} };
        for (size_t column = 0; column < variables.size(); ++column)
            if (outer.contains(variables[column]))
            {
                aggregate.group.push_back(column);
                literal.terms.push_back(Term::variable(outer_slots.at(variables[column])));
            }
        if (view.get_function() != AggregateFunction::COUNT)
            aggregate.value = inner_slots.at(view.get_value());
        aggregate.table = add_table(aggregate.group.size() + 1);
        aggregate.groups = std::make_unique<RawArraySet<uint_t>>(aggregate.group.size());
        if (view.get_function() == AggregateFunction::COUNT || view.get_function() == AggregateFunction::SUM)
            m_program.tables[aggregate.table]->fallback = 0;
        const auto [result, inserted] = outer_slots.try_emplace(view.get_result(), to_uint_t(outer_slots.size()));
        literal.terms.push_back(Term::variable(result->second));
        literal.table = aggregate.table;
        m_dependencies.push_back(Dependency { aggregate.table, source, true });
        m_program.aggregates.push_back(std::move(aggregate));
        return literal;
    }
};

/// The tables the outputs depend on.
std::vector<bool> needed_tables(const CompiledProgram& program, std::span<const Dependency> dependencies)
{
    auto needed = std::vector<bool>(program.tables.size());
    auto pending = program.outputs;
    for (const auto table : pending)
        needed[table] = true;
    while (!pending.empty())
    {
        const auto table = pending.back();
        pending.pop_back();
        for (const auto& dependency : dependencies)
            if (dependency.head == table && !needed[dependency.body])
            {
                needed[dependency.body] = true;
                pending.push_back(dependency.body);
            }
    }
    return needed;
}
}  // namespace

CompiledProgram translate(ProgramView view, std::span<const RelationView> outputs, std::span<const size_t> static_slots)
{
    auto translator = Translator {};
    for (const auto rule : view.get_rules())
        translator.add_rule(rule);
    auto& program = translator.program();
    for (const auto relation : outputs)
        program.outputs.push_back(translator.table_of(relation));

    // Only the relations the outputs depend on are evaluated, and only the inputs they read are read.
    const auto needed = needed_tables(program, translator.dependencies());
    std::erase_if(program.rules, [&](const CompiledRule& rule) { return !needed[rule.head]; });
    for (auto& table : program.slot_tables)
        if (table != npos && !needed[table])
            table = npos;
    for (const auto slot : static_slots)
        if (slot < program.slot_tables.size() && program.slot_tables[slot] != npos)
            program.tables[program.slot_tables[slot]]->is_static = true;

    const auto strata = stratify(program.tables.size(), translator.dependencies());

    auto stratum_of = std::vector<size_t>(program.tables.size());
    program.recursive_tables.assign(program.tables.size(), false);
    auto aggregate_of = std::map<size_t, size_t>();
    for (size_t a = 0; a < program.aggregates.size(); ++a)
        aggregate_of.emplace(program.aggregates[a].table, a);
    for (const auto& stratum : strata)
    {
        // A stratum's relations depend on each other, so either all of them are needed or none.
        if (!needed[stratum.relations.front()])
            continue;
        // Input relations have no rules and no stratum.
        if (std::ranges::all_of(stratum.relations, [&](size_t t) { return program.tables[t]->slot.has_value(); }))
            continue;
        auto info = StratumInfo { StratumInfo::Kind::PLAIN, stratum.relations, {}, {}, npos };
        for (const auto table : info.tables)
            stratum_of[table] = program.strata.size();
        if (info.tables.size() == 1 && aggregate_of.contains(info.tables.front()))
        {
            info.kind = StratumInfo::Kind::AGGREGATE;
            info.aggregate = aggregate_of.at(info.tables.front());
            info.reads.push_back(program.aggregates[info.aggregate].source);
        }
        else if (stratum.recursive)
        {
            info.kind = StratumInfo::Kind::RECURSIVE;
            for (const auto table : info.tables)
                program.recursive_tables[table] = true;
        }
        else
            for (const auto table : info.tables)
                program.tables[table]->counted = true;
        program.strata.push_back(std::move(info));
    }
    for (size_t r = 0; r < program.rules.size(); ++r)
    {
        auto& stratum = program.strata[stratum_of[program.rules[r].head]];
        stratum.rules.push_back(r);
        for (const auto& literal : program.rules[r].literals)
            if (literal.table != npos && std::ranges::find(stratum.reads, literal.table) == stratum.reads.end())
                stratum.reads.push_back(literal.table);
    }
    plan_rules(program);
    return std::move(program);
}
}  // namespace ygg::database::datalog::detail
