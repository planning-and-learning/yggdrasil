/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/optimization/details/optimization.hpp"

namespace ygg::database::detail
{
void validate(std::span<const QueryView<>> roots, const Statistics& statistics)
{
    for (const auto query : reachable(roots))
    {
        if (is<QueryInputTag>(query))
            if (const auto it = statistics.inputs.find(as<QueryInputTag>(query).get_input_slot()); it != statistics.inputs.end())
                validate_statistics(it->second, query.columns());
        if (const auto it = statistics.expressions.find(query.get_index()); it != statistics.expressions.end())
            validate_statistics(it->second, query.columns());
    }
}
}

namespace ygg::database::detail
{
QueryPlan optimize(std::span<const QueryView<>> roots, std::span<const Evaluation> evaluations, const Statistics& statistics)
{
    if (evaluations.size() != roots.size())
        throw std::invalid_argument("optimize: one evaluation per root is required.");
    validate(roots, statistics);
    if (roots.empty())
        return {};
    const auto& source = roots.front().get_repository();
    QueryBuilder builder;
    auto normalized = source.get_factory().create();
    Normalizer normalize(normalized, builder, source.size());
    std::vector<std::pair<Index<Query<>>, Index<Query<>>>> normalized_queries;
    for (const auto query : reachable(roots))
        normalized_queries.emplace_back(normalize(query).get_index(), query.get_index());
    // The equivalent caller query of each normalized query; the first one wins.
    std::vector<std::optional<Index<Query<>>>> sources(normalized.size());
    for (const auto& [normalized_query, caller] : normalized_queries)
        if (auto& source_of = sources[normalized_query.get_value()]; !source_of)
            source_of = caller;
    auto plans = source.get_factory().create();
    Planner planner(statistics, sources, normalized.size(), plans, builder);
    std::vector<QueryView<>> outputs;
    for (size_t i = 0; i < roots.size(); ++i)
    {
        const auto root = roots[i];
        // The bound evaluator orders each binding's joins itself, so a bound root keeps its normalized join structure.
        outputs.push_back(evaluations[i] == Evaluation::bound ? copy(normalize(root), [](size_t slot) { return slot; }, plans, builder) :
                                                                planner.plan(normalize(root)));
        assert(std::ranges::equal(outputs.back().columns(), root.columns()) && "Plans preserve ordered schemas.");
    }
    return compile(std::span<const QueryView<>>(outputs));
}
}

namespace ygg::database::detail
{
bool Planner::known(QueryView<> plan)
{
    if (const auto cached = slot(m_known, plan))
        return *cached;
    bool result = false;
    if (const auto caller = source(plan); caller && m_statistics->expressions.contains(*caller))
        result = true;
    else if (is<QueryDistanceTag>(plan))
        result = false;
    else if (is<QueryInputTag>(plan))
        result = m_statistics->inputs.contains(as<QueryInputTag>(plan).get_input_slot());
    else
    {
        result = true;
        for_each_child(plan, [&](QueryView<> child) { result = result && known(child); });
    }
    slot(m_known, plan) = result;
    return result;
}
}

namespace ygg::database::detail
{
QueryView<> Planner::plan_block(QueryView<> join, std::span<const ColumnLayout> output)
{
    JoinBlock block { {}, ygg::canonicalized(column_labels(output)) };
    UnorderedSet<QueryIndex> seen;
    collect(join, block.atoms, seen);
    if (block.atoms.empty())  // The empty join is the nullary relation with one row.
        return m_build.generic_join(block.atoms, {});
    const bool measured = block.atoms.size() <= max_cost_based_atoms && std::ranges::all_of(block.atoms, [&](auto atom) { return known(atom); });
    const auto result = block.atoms.size() == 1 ? block.atoms.front() :
                        measured ? CostBasedPlanner(block, m_build, [&](QueryView<> atom) { return estimate(atom); }).plan() :
                                   structural_plan(block, m_build);
    return m_build.project(result, column_labels(output));
}
}

namespace ygg::database::detail
{
QueryView<> Planner::plan(QueryView<> normalized)
{
    if (const auto& planned = m_planned.at(normalized.get_index().get_value()))
        return *planned;
    QueryView<> result = [&]
    {
        if (joins(normalized))
            return plan_block(normalized, normalized.columns());
        if (is<QueryProjectTag>(normalized))
        {
            if (const auto projection = as<QueryProjectTag>(normalized); joins(projection.get_arg()))
                return plan_block(projection.get_arg(), normalized.columns());
        }
        std::vector<QueryView<>> children;
        for_each_child(normalized, [&](QueryView<> child) { children.push_back(plan(child)); });
        return m_build.clone(normalized, children);
    }();
    if (const auto caller = m_normalized_sources[normalized.get_index().get_value()]; caller && !source(result))
        slot(m_sources, result) = caller;
    m_planned[normalized.get_index().get_value()] = result;
    return result;
}
}
