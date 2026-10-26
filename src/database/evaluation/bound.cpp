/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/evaluation/bound.hpp"

namespace ygg::database::bound
{
std::vector<ConjunctivePlan> QueryEvaluator::plans(const std::vector<bool>& bound)
{
    auto measured = std::vector<std::optional<RelationStatistics>>(input_count());
    const auto statistics = [&](size_t slot) -> const RelationStatistics&
    {
        if (!measured[slot])
            measured[slot] = measure_relation(*m_inputs[slot]);
        return *measured[slot];
    };
    auto compiled = std::vector<ConjunctivePlan>();
    for (const auto& branch : m_query.branches)
    {
        auto& plan = compiled.emplace_back(plan_branch(m_query, branch, bound, statistics));
        resolve(plan);
    }
    return compiled;
}
}

namespace ygg::database::bound
{
bool QueryEvaluator::bind_atom(const ConjunctiveAtom& atom, std::span<const std::byte> row)
{
    const auto& columns = m_query.input_columns[atom.slot];
    for (size_t c = 0; c < atom.vars.size(); ++c)
    {
        const auto bytes = database::detail::column_bytes(row, columns[c]);
        if (static_cast<size_t>(std::ranges::find(atom.vars, atom.vars[c]) - atom.vars.begin()) == c)
            std::ranges::copy(bytes, variable(atom.vars[c]).begin());
        else if (!std::ranges::equal(bytes, variable(atom.vars[c])))
            return false;
    }
    return true;
}
}

namespace ygg::database::bound
{
size_t QueryEvaluator::index_id(size_t slot, std::span<const std::pair<size_t, uint32_t>> key)
{
    auto positions = std::vector<size_t>();
    for (const auto& [c, var] : key)
        positions.push_back(c);
    for (size_t id = 0; id < m_indexes.size(); ++id)
        if (m_indexes[id].slot == slot && m_indexes[id].positions == positions)
            return id;
    auto& entry = m_indexes.emplace_back();
    entry.slot = slot;
    for (const auto position : positions)
        entry.keys.push_back(database::detail::column_slice(m_query.input_columns[slot][position]));
    entry.positions = std::move(positions);
    return m_indexes.size() - 1;
}
}

namespace ygg::database::bound
{
void QueryEvaluator::resolve(ConjunctivePlan& plan)
{
    for (auto& step : plan.steps)
    {
        auto& lookup = m_lookups.emplace_back();
        lookup.index = index_id(step.slot, step.key);
        for (const auto& [c, var] : step.key)
        {
            const auto& variable = m_query.variables[var];
            lookup.probe.push_back(ColumnSlice { m_query.input_columns[step.slot][c].type, variable.offset, variable.size });
        }
        step.index = m_lookups.size() - 1;
    }
    for (auto& plans : plan.negation_plans)
        for (auto& inner : plans)
            resolve(inner);
}
}

namespace ygg::database::bound
{
bool QueryEvaluator::accepts(const ConjunctivePlan& plan, const ConjunctiveFilters& filters)
{
    for (const auto& [lhs, rhs] : filters.equal)
        if (!std::ranges::equal(variable(lhs), variable(rhs)))
            return false;
    for (const auto& [var, constant] : filters.value)
        if (!std::ranges::equal(variable(var), constant))
            return false;
    // A negation rejects the row if any of its branches matches (an anti-join, its outer variables bound).
    for (const auto i : filters.negations)
        for (const auto& inner : plan.negation_plans[i])
            if (accepts(inner, inner.filters) && !run(inner, 0, [] { return false; }))
                return false;
    return true;
}
}
