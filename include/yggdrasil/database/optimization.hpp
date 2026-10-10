/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_OPTIMIZATION_HPP_
#define YGG_DATABASE_OPTIMIZATION_HPP_

#include "yggdrasil/database/query.hpp"
#include "yggdrasil/database/relation_view.hpp"

#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <type_traits>

namespace ygg::database
{
/// Cost-based plan choice over equivalent rewrites (the port of verdog's optimizer).
struct CostBasedTag
{
};

struct RelationStatistics
{
    double rows = 0;
    std::map<Index<Column>, double> distinct;
    std::optional<double> work;
};
template<ColumnTypes Values = DefaultColumnTypes>
struct Statistics
{
    /// Keyed by input slot.
    std::map<size_t, RelationStatistics> inputs;
    /// Observations of queries in the optimized roots' repository.
    std::map<Index<Query<Values>>, RelationStatistics> expressions;
};
struct SearchLimits
{
    size_t memo_expressions = 10000;
    size_t saturation_rounds = 64;
    size_t candidate_evaluations = 20000;
    size_t frontier_size = 16;
};
struct CostModel
{
    /// Rows of an input without statistics, and the cap on distinct counts.
    double domain_size = 1000;
    /// Weight of retained cells relative to work in a plan's score.
    double memory_weight = 1;
};
template<ColumnTypes Values = DefaultColumnTypes>
struct OptimizationContext
{
    Statistics<Values> statistics;
    SearchLimits limits;
    CostModel cost;
};
struct OptimizationReport
{
    size_t memo_expressions = 0, candidate_evaluations = 0;
    bool budget_exhausted = false, pruned = false;
    double estimated_work = 0, retained = 0, estimated_score = 0, baseline_score = 0;
};
template<ColumnTypes Values = DefaultColumnTypes>
struct OptimizationResult
{
    QueryPlan<Values> plan;
    OptimizationReport report;
};
/// Extension point: specialize Optimizer<MyTag, Values>::run(roots, context).
/// The roots belong to one repository. The returned plan must preserve every
/// root's ordered, typed set semantics.
template<class AlgorithmTag, ColumnTypes Values>
struct Optimizer;

template<ColumnTypes Values, RelationViewRange<Values> R>
Statistics<Values> collect_statistics(const R& inputs)
{
    Statistics<Values> result;
    for (size_t slot = 0; slot < std::ranges::size(inputs); ++slot)
    {
        const auto& input = inputs[slot];
        auto& stats = result.inputs[slot];
        stats.rows = static_cast<double>(input.size());
        const auto columns = input.columns();
        for (const auto& column : columns.span())
        {
            std::set<std::vector<std::byte>> keys;
            for (size_t row = 0; row < input.size(); ++row)
            {
                const auto bytes = detail::column_bytes(input.row(row), column);
                keys.emplace(bytes.begin(), bytes.end());
            }
            stats.distinct[column.label] = static_cast<double>(keys.size());
        }
    }
    return result;
}

}  // namespace ygg::database

#include "yggdrasil/database/details/optimization.hpp"

namespace ygg::database
{
template<ColumnTypes Values>
struct Optimizer<CostBasedTag, Values>
{
    static OptimizationResult<Values> run(std::span<const QueryView<Values>> roots, const OptimizationContext<Values>& context)
    {
        return optimization_detail::optimize(roots, context);
    }
};

template<class AlgorithmTag = CostBasedTag, ColumnTypes Values>
OptimizationResult<Values> optimize(std::span<const QueryView<Values>> roots, const std::type_identity_t<OptimizationContext<Values>>& context = {})
{
    const auto original = reachable(roots);
    auto result = Optimizer<AlgorithmTag, Values>::run(roots, context);
    if (result.plan.root_count() != roots.size())
        throw std::invalid_argument("Optimizer: the result must preserve the number of roots.");
    std::map<size_t, std::vector<ColumnLayout>> inputs;
    for (const auto query : original)
        ygg::visit(
            [&]<typename Child>(Child child)
            {
                if constexpr (std::same_as<Child, QueryView<Values, QueryInputTag>>)
                    inputs.emplace(child.get_input_slot(), optimization_detail::own_columns(child.columns()));
            },
            query.get_variant());
    for (size_t value = 0; value < result.plan.node_count(); ++value)
        ygg::visit(
            [&]<typename Child>(Child child)
            {
                if constexpr (std::same_as<Child, QueryView<Values, QueryInputTag>>)
                {
                    const auto binding = inputs.find(child.get_input_slot());
                    if (binding == inputs.end() || !std::ranges::equal(binding->second, child.columns()))
                        throw std::invalid_argument("Optimizer: result introduces an unknown input binding.");
                }
            },
            result.plan[Index<Query<Values>>(to_uint_t(value))].get_variant());
    for (size_t i = 0; i < roots.size(); ++i)
        if (!std::ranges::equal(result.plan[result.plan.roots()[i]].columns(), roots[i].columns()))
            throw std::invalid_argument("Optimizer: result must preserve each root's ordered schema.");
    return result;
}
template<class AlgorithmTag = CostBasedTag, ColumnTypes Values>
OptimizationResult<Values> optimize(QueryView<Values> root, const std::type_identity_t<OptimizationContext<Values>>& context = {})
{
    return optimize<AlgorithmTag>(std::span<const QueryView<Values>>(&root, 1), context);
}
}  // namespace ygg::database
#endif
