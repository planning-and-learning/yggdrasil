/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_OPTIMIZATION_DETAILS_OPTIMIZATION_HPP_
#define YGG_DATABASE_OPTIMIZATION_DETAILS_OPTIMIZATION_HPP_

#include "yggdrasil/containers/associative_containers.hpp"
#include "yggdrasil/database/optimization/details/cost.hpp"
#include "yggdrasil/database/optimization/details/estimation.hpp"
#include "yggdrasil/database/optimization/details/join_block.hpp"
#include "yggdrasil/database/optimization/details/normalization.hpp"
#include "yggdrasil/database/optimization/details/structural.hpp"

#include <cassert>
#include <concepts>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace ygg::database
{
/// How a root is evaluated: fully, by materializing it, or bound, by the bound evaluator with some columns given per
/// call (bound::QueryEvaluator).
enum class Evaluation : std::uint8_t
{
    full,
    bound,
};
}  // namespace ygg::database

namespace ygg::database::detail
{
/// Plans normalized queries block by block into a plan repository.
class Planner
{
    using QueryIndex = Index<Query<>>;

    const Statistics* m_statistics;
    std::span<const std::optional<QueryIndex>> m_normalized_sources;
    OperatorBuilder m_build;
    // Memos indexed by query index; computed values are stored after recursing,
    // because recursion may grow the vectors.
    std::vector<std::optional<QueryView<>>> m_planned;
    /// Equivalent caller query of a plan query, for observed statistics.
    std::vector<std::optional<QueryIndex>> m_sources;
    std::vector<std::optional<Estimate>> m_estimates;
    std::vector<std::optional<bool>> m_known;

    template<class T>
    static std::optional<T>& slot(std::vector<std::optional<T>>& memo, QueryView<> query)
    {
        const auto position = query.get_index().get_value();
        if (memo.size() <= position)
            memo.resize(position + 1);
        return memo[position];
    }
    static bool joins(QueryView<> query) { return is<QueryJoinTag>(query) || is<QueryGenericJoinTag>(query); }

    [[nodiscard]] std::optional<QueryIndex> source(QueryView<> plan) const
    {
        const auto position = plan.get_index().get_value();
        return position < m_sources.size() ? m_sources[position] : std::nullopt;
    }
    /// Whether the plan query's rows are measured or derivable from measured inputs.
    bool known(QueryView<> plan);
    Estimate estimate(QueryView<> plan)
    {
        if (const auto& cached = slot(m_estimates, plan))
            return *cached;
        auto result = detail::estimate(plan, source(plan), [&](QueryView<> child) { return estimate(child); }, *m_statistics);
        slot(m_estimates, plan) = result;
        return result;
    }

    /// The planned operands of the joins below a normalized join, each once.
    void collect(QueryView<> normalized, std::vector<QueryView<>>& atoms, UnorderedSet<QueryIndex>& seen)
    {
        if (!joins(normalized))
        {
            if (const auto atom = plan(normalized); seen.insert(atom.get_index()).second)
                atoms.push_back(atom);
            return;
        }
        for_each_child(normalized, [&](QueryView<> child) { collect(child, atoms, seen); });
    }
    /// Plans π_output(⋈ atoms): cost-based when every atom is known, otherwise from structure.
    QueryView<> plan_block(QueryView<> join, std::span<const ColumnLayout> output);

public:
    Planner(const Statistics& statistics,
            std::span<const std::optional<QueryIndex>> normalized_sources,
            size_t normalized_size,
            QueryRepository& plans,
            QueryBuilder& builder) :
        m_statistics(&statistics),
        m_normalized_sources(normalized_sources),
        m_build(plans, builder),
        m_planned(normalized_size)
    {
    }

    /// The plan of a normalized query, with the same ordered schema.
    QueryView<> plan(QueryView<> normalized);
};

void validate(std::span<const QueryView<>> roots, const Statistics& statistics);

/// Normalizes the reachable queries, plans each join block of the fully evaluated roots, and compiles the roots.
QueryPlan optimize(std::span<const QueryView<>> roots, std::span<const Evaluation> evaluations, const Statistics& statistics);
}  // namespace ygg::database::detail

#endif
