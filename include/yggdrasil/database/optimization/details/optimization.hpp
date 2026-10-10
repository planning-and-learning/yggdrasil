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
#include <map>
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
    const std::map<QueryIndex, QueryIndex>* m_normalized_sources;
    QueryRepository<Values>* m_plans;
    QueryBuilder<Values>* m_builder;
    BlockBuilder<Values> m_build;
    std::vector<std::optional<QueryView<Values>>> m_planned;
    /// Equivalent caller query of a plan query, for observed statistics.
    std::map<QueryIndex, QueryIndex> m_sources;
    std::map<QueryIndex, Estimate> m_estimates;
    std::map<QueryIndex, bool> m_known;

    template<class Tag>
    static bool is(QueryView<Values> query)
    {
        return query.get_variant().template is<Index<Query<Values, Tag>>>();
    }
    static bool joins(QueryView<Values> query) { return is<QueryJoinTag>(query) || is<QueryGenericJoinTag>(query); }

    std::optional<QueryIndex> source(QueryView<Values> plan) const
    {
        const auto it = m_sources.find(plan.get_index());
        return it == m_sources.end() ? std::nullopt : std::optional(it->second);
    }
    /// Whether the plan query's rows are measured or derivable from measured inputs.
    bool known(QueryView<Values> plan)
    {
        if (const auto it = m_known.find(plan.get_index()); it != m_known.end())
            return it->second;
        bool result = false;
        if (const auto caller = source(plan); caller && m_statistics->expressions.contains(*caller))
            result = true;
        else if (is<QueryDistanceTag>(plan))
            result = false;
        else if (is<QueryInputTag>(plan))
            result = m_statistics->inputs.contains(plan.get_variant().template get<Index<Query<Values, QueryInputTag>>>().get_input_slot());
        else
        {
            result = true;
            for_each_child(plan, [&](QueryView<Values> child) { result = result && known(child); });
        }
        return m_known.emplace(plan.get_index(), result).first->second;
    }
    const Estimate& estimate(QueryView<Values> plan)
    {
        if (const auto it = m_estimates.find(plan.get_index()); it != m_estimates.end())
            return it->second;
        auto result = optimization_detail::estimate(plan, source(plan), [&](QueryView<Values> child) { return estimate(child); }, *m_statistics);
        return m_estimates.emplace(plan.get_index(), std::move(result)).first->second;
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
            const std::map<QueryIndex, QueryIndex>& normalized_sources,
            size_t normalized_size,
            QueryRepository<Values>& plans,
            QueryBuilder<Values>& builder) :
        m_statistics(&statistics),
        m_normalized_sources(&normalized_sources),
        m_plans(&plans),
        m_builder(&builder),
        m_build(plans, builder),
        m_planned(normalized_size)
    {
    }

    /// The plan of a normalized query, with the same ordered schema.
    QueryView<Values> plan(QueryView<Values> normalized)
    {
        auto& planned = m_planned.at(normalized.get_index().get_value());
        if (planned)
            return *planned;
        QueryView<Values> result = [&]
        {
            if (joins(normalized))
                return plan_block(normalized, normalized.columns());
            if (is<QueryProjectTag>(normalized))
            {
                const auto projection = normalized.get_variant().template get<Index<Query<Values, QueryProjectTag>>>();
                if (joins(projection.get_arg()))
                    return plan_block(projection.get_arg(), normalized.columns());
            }
            std::vector<QueryView<Values>> children;
            for_each_child(normalized, [&](QueryView<Values> child) { children.push_back(plan(child)); });
            return detail::clone_query(normalized, std::span<const QueryView<Values>>(children), *m_plans, *m_builder);
        }();
        if (const auto it = m_normalized_sources->find(normalized.get_index()); it != m_normalized_sources->end())
            m_sources.try_emplace(result.get_index(), it->second);
        planned = result;
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
    std::map<Index<Query<Values>>, Index<Query<Values>>> sources;
    for (const auto query : reachable(roots))
        sources.try_emplace(normalize(query).get_index(), query.get_index());
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
