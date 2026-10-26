/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_SEMANTICS_INCREMENTAL_AGGREGATE_HPP_
#define YGG_DATABASE_SEMANTICS_INCREMENTAL_AGGREGATE_HPP_

#include "yggdrasil/core/config.hpp"
#include "yggdrasil/database/semantics/aggregate.hpp"
#include "yggdrasil/database/semantics/incremental/delta.hpp"

#include <optional>
#include <vector>

namespace ygg::database::incremental
{
/// Maintains aggregate() under changes of the argument and the groups: a change re-evaluates only the groups it touches,
/// and the output changes where a group's aggregate does (DBSP, §7.4). Inputs are borrowed only for each call and must
/// not alias this evaluator's result or delta.
class AggregateEvaluator
{
    AggregatePlan m_plan;
    GroupedAggregate m_state;
    Builder<Relation> m_groups;
    Builder<Relation> m_result;
    Delta m_delta;
    /// The groups a change touches and their aggregates before it.
    Builder<Relation> m_touched;
    std::vector<std::optional<uint_t>> m_before;
    std::vector<std::byte> m_key;
    std::vector<std::byte> m_row;
    bool m_initialized = false;

    [[nodiscard]] std::optional<uint_t> output(std::span<const std::byte> key) const;
    void touch(std::span<const std::byte> key);
    template<RelationChange A, RelationChange G>
    void apply(const A& arg, const G& groups);

    template<typename>
    friend struct detail::NodeRules;

public:
    explicit AggregateEvaluator(AggregatePlan plan);

    template<RelationViewConcept A, RelationViewConcept G>
    void initialize(const A& arg, const G& groups);
    /// Each change describes actual set changes of that input; schema, overlap and aliasing errors are rejected before
    /// changing the result.
    template<RelationChange A, RelationChange G>
    void update(const A& arg, const G& groups);

    [[nodiscard]] const Builder<Relation>& get_result() const&;
    [[nodiscard]] const Builder<Relation>& get_result() const&& = delete;
    [[nodiscard]] const Delta& get_delta() const&;
    [[nodiscard]] const Delta& get_delta() const&& = delete;
    [[nodiscard]] size_t memory_usage() const noexcept;
};

template<RelationViewConcept A, RelationViewConcept G>
void AggregateEvaluator::initialize(const A& arg, const G& groups)
{
    database::detail::require_plan_columns(arg.columns().span(), m_plan.arg_columns().span());
    database::detail::require_plan_columns(groups.columns().span(), m_plan.groups_columns().span());
    detail::require_unaliased(arg, m_result, m_delta);
    detail::require_unaliased(groups, m_result, m_delta);
    m_initialized = false;
    database::aggregate(arg, groups, m_plan, m_result);
    m_state.clear();
    for (size_t i = 0; i < arg.size(); ++i)
        m_state.add(database::detail::aggregate_key(arg.row(i), m_plan, m_key), database::detail::aggregate_value(arg.row(i), m_plan), 1);
    database::assign(m_groups, groups);
    m_delta.clear();
    m_initialized = true;
}

template<RelationChange A, RelationChange G>
void AggregateEvaluator::update(const A& arg, const G& groups)
{
    detail::require_initialized(m_initialized);
    detail::require_change(arg, m_plan.arg_columns().span(), m_result, m_delta);
    detail::require_change(groups, m_plan.groups_columns().span(), m_result, m_delta);
    apply(arg, groups);
}

template<RelationChange A, RelationChange G>
void AggregateEvaluator::apply(const A& arg, const G& groups)
{
    const auto& [arg_added, arg_removed] = arg;
    const auto& [groups_added, groups_removed] = groups;
    m_initialized = false;
    m_delta.clear();
    m_touched.clear();
    m_before.clear();
    const auto touch_groups_of = [&](const auto& rows)
    {
        for (size_t i = 0; i < rows.size(); ++i)
            touch(database::detail::aggregate_key(rows.row(i), m_plan, m_key));
    };
    const auto touch_groups = [&](const auto& rows)
    {
        for (size_t i = 0; i < rows.size(); ++i)
            touch(rows.row(i));
    };
    const auto add_values = [&](const auto& rows, Weight weight)
    {
        for (size_t i = 0; i < rows.size(); ++i)
            m_state.add(database::detail::aggregate_key(rows.row(i), m_plan, m_key), database::detail::aggregate_value(rows.row(i), m_plan), weight);
    };
    touch_groups_of(arg_added);
    touch_groups_of(arg_removed);
    touch_groups(groups_added);
    touch_groups(groups_removed);
    add_values(arg_added, 1);
    add_values(arg_removed, -1);
    for (size_t i = 0; i < groups_removed.size(); ++i)
    {
        const auto position = m_groups.find(groups_removed.row(i));
        YGG_ASSUME(position.has_value());  // a removed group is a group
        m_groups.erase(*position);
    }
    for (size_t i = 0; i < groups_added.size(); ++i)
        m_groups.insert(groups_added.row(i));
    for (size_t i = 0; i < m_touched.size(); ++i)
    {
        const auto key = m_touched.row(i);
        const auto before = m_before[i];
        const auto after = output(key);
        if (before == after)
            continue;
        if (before)
        {
            const auto row = database::detail::aggregate_row(key, *before, m_row);
            m_delta.removed.insert(row);
            const auto position = m_result.find(row);
            YGG_ASSUME(position.has_value());  // a removed row is in the result
            m_result.erase(*position);
        }
        if (after)
        {
            const auto row = database::detail::aggregate_row(key, *after, m_row);
            m_delta.added.insert(row);
            m_result.insert(row);
        }
    }
    m_initialized = true;
}
}  // namespace ygg::database::incremental

#endif
