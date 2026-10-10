/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_OPTIMIZATION_DETAILS_OPTIMIZATION_HPP_
#define YGG_DATABASE_OPTIMIZATION_DETAILS_OPTIMIZATION_HPP_

#include "yggdrasil/database/optimization/details/cost.hpp"
#include "yggdrasil/database/optimization/details/estimation.hpp"
#include "yggdrasil/database/optimization/details/join_block.hpp"
#include "yggdrasil/database/optimization/details/normalization.hpp"
#include "yggdrasil/database/optimization/details/structural.hpp"

#include <cassert>
#include <concepts>
#include <utility>
#include <optional>
#include <span>
#include <vector>

namespace ygg::database::optimization_detail
{
/// Plans normalized queries block by block into a plan repository.
template<ColumnTypes Values>
class Planner
{
    using QueryIndex = Index<Query<Values>>;

    const Statistics<Values>* m_statistics;
    std::span<const std::optional<QueryIndex>> m_normalized_sources;
    QueryRepository<Values>* m_plans;
    QueryBuilder<Values>* m_builder;
    OperatorBuilder<Values> m_build;
    // Memos indexed by query index; computed values are stored after recursing,
    // because recursion may grow the vectors.
    std::vector<std::optional<QueryView<Values>>> m_planned;
    /// Equivalent caller query of a plan query, for observed statistics.
    std::vector<std::optional<QueryIndex>> m_sources;
    std::vector<std::optional<Estimate>> m_estimates;
    std::vector<std::optional<bool>> m_known;

    template<class T>
    static std::optional<T>& slot(std::vector<std::optional<T>>& memo, QueryView<Values> query)
    {
        const auto position = query.get_index().get_value();
        if (memo.size() <= position)
            memo.resize(position + 1);
        return memo[position];
    }
    static bool joins(QueryView<Values> query) { return is<QueryJoinTag>(query) || is<QueryGenericJoinTag>(query); }

    std::optional<QueryIndex> source(QueryView<Values> plan) const
    {
        const auto position = plan.get_index().get_value();
        return position < m_sources.size() ? m_sources[position] : std::nullopt;
    }
    /// Whether the plan query's rows are measured or derivable from measured inputs.
    bool known(QueryView<Values> plan)
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
            for_each_child(plan, [&](QueryView<Values> child) { result = result && known(child); });
        }
        slot(m_known, plan) = result;
        return result;
    }
    Estimate estimate(QueryView<Values> plan)
    {
        if (const auto& cached = slot(m_estimates, plan))
            return *cached;
        auto result = optimization_detail::estimate(plan, source(plan), [&](QueryView<Values> child) { return estimate(child); }, *m_statistics);
        slot(m_estimates, plan) = result;
        return result;
    }

    /// The planned operands of the joins below a normalized join, each once.
    void collect(QueryView<Values> normalized, std::vector<QueryView<Values>>& atoms)
    {
        if (!joins(normalized))
        {
            const auto atom = plan(normalized);
            if (std::ranges::none_of(atoms, [&](QueryView<Values> other) { return other.get_index() == atom.get_index(); }))
                atoms.push_back(atom);
            return;
        }
        for_each_child(normalized, [&](QueryView<Values> child) { collect(child, atoms); });
    }
    /// Plans π_output(⋈ atoms): cost-based when every atom is known, otherwise from structure.
    QueryView<Values> plan_block(QueryView<Values> join, std::span<const ColumnLayout> output)
    {
        JoinBlock<Values> block;
        collect(join, block.atoms);
        block.output.assign(output.begin(), output.end());
        if (block.atoms.empty())  // The empty join is the nullary relation with one row.
            return m_build.generic_join(block.atoms, {});
        const bool measured = block.atoms.size() <= max_cost_based_atoms && std::ranges::all_of(block.atoms, [&](auto atom) { return known(atom); });
        const auto result = block.atoms.size() == 1 ? block.atoms.front() :
                            measured ? CostBasedPlanner<Values>(block, m_build, [&](QueryView<Values> atom) { return estimate(atom); }).plan() :
                                       structural_plan(block, m_build);
        return m_build.project(result, detail::query_labels(output));
    }

public:
    Planner(const Statistics<Values>& statistics,
            std::span<const std::optional<QueryIndex>> normalized_sources,
            size_t normalized_size,
            QueryRepository<Values>& plans,
            QueryBuilder<Values>& builder) :
        m_statistics(&statistics),
        m_normalized_sources(normalized_sources),
        m_plans(&plans),
        m_builder(&builder),
        m_build(plans, builder),
        m_planned(normalized_size)
    {
    }

    /// The plan of a normalized query, with the same ordered schema.
    QueryView<Values> plan(QueryView<Values> normalized)
    {
        if (const auto& planned = m_planned.at(normalized.get_index().get_value()))
            return *planned;
        QueryView<Values> result = [&]
        {
            if (joins(normalized))
                return plan_block(normalized, normalized.columns());
            if (is<QueryProjectTag>(normalized))
            {
                if (const auto projection = as<QueryProjectTag>(normalized); joins(projection.get_arg()))
                    return plan_block(projection.get_arg(), normalized.columns());
            }
            std::vector<QueryView<Values>> children;
            for_each_child(normalized, [&](QueryView<Values> child) { children.push_back(plan(child)); });
            return detail::clone_query(normalized, std::span<const QueryView<Values>>(children), *m_plans, *m_builder);
        }();
        if (const auto caller = m_normalized_sources[normalized.get_index().get_value()]; caller && !source(result))
            slot(m_sources, result) = caller;
        m_planned[normalized.get_index().get_value()] = result;
        return result;
    }
};

template<ColumnTypes Values>
void validate(std::span<const QueryView<Values>> roots, const Statistics<Values>& statistics)
{
    for (const auto query : reachable(roots))
    {
        ygg::visit(
            [&]<typename Child>(Child child)
            {
                if constexpr (std::same_as<Child, QueryView<Values, QueryInputTag>>)
                    if (const auto it = statistics.inputs.find(child.get_input_slot()); it != statistics.inputs.end())
                        validate_statistics(it->second, child.columns());
            },
            query.get_variant());
        if (const auto it = statistics.expressions.find(query.get_index()); it != statistics.expressions.end())
            validate_statistics(it->second, query.columns());
    }
}

/// Normalizes the reachable queries, plans each join block, and compiles the roots.
template<ColumnTypes Values>
QueryPlan<Values> optimize(std::span<const QueryView<Values>> roots, const Statistics<Values>& statistics)
{
    validate(roots, statistics);
    if (roots.empty())
        return {};
    const auto& source = roots.front().get_repository();
    QueryBuilder<Values> builder;
    auto normalized = source.get_factory().create();
    Normalizer<Values> normalize(normalized, builder, source.size());
    std::vector<std::pair<Index<Query<Values>>, Index<Query<Values>>>> normalized_queries;
    for (const auto query : reachable(roots))
        normalized_queries.emplace_back(normalize(query).get_index(), query.get_index());
    // The equivalent caller query of each normalized query; the first one wins.
    std::vector<std::optional<Index<Query<Values>>>> sources(normalized.size());
    for (const auto& [normalized_query, caller] : normalized_queries)
        if (auto& source_of = sources[normalized_query.get_value()]; !source_of)
            source_of = caller;
    auto plans = source.get_factory().create();
    Planner<Values> planner(statistics, sources, normalized.size(), plans, builder);
    std::vector<QueryView<Values>> outputs;
    for (const auto root : roots)
    {
        outputs.push_back(planner.plan(normalize(root)));
        assert(std::ranges::equal(outputs.back().columns(), root.columns()) && "Plans preserve ordered schemas.");
    }
    return compile(std::span<const QueryView<Values>>(outputs));
}
}  // namespace ygg::database::optimization_detail

#endif
