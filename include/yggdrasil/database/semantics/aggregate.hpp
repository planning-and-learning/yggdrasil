/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_SEMANTICS_AGGREGATE_HPP_
#define YGG_DATABASE_SEMANTICS_AGGREGATE_HPP_

#include "yggdrasil/database/semantics/operations.hpp"
#include "yggdrasil/database/semantics/plans.hpp"
#include "yggdrasil/database/semantics/relation_builder.hpp"
#include "yggdrasil/database/semantics/relation_view.hpp"
#include "yggdrasil/database/semantics/zset.hpp"

#include <cista/containers/vector.h>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <vector>

namespace ygg::database
{
/// Resolves an aggregate's columns once: the group columns of the argument in the order of the groups' schema, the value
/// column (`uint_t`, unused by COUNT), and the output schema, the groups' columns followed by the result column
/// (`uint_t`).
class AggregatePlan
{
    Builder<Columns> m_arg;
    Builder<Columns> m_groups;
    Builder<Columns> m_output;
    cista::offset::vector<ColumnSlice> m_keys;
    ColumnSlice m_value {};
    AggregateFunction m_function = AggregateFunction::COUNT;

public:
    AggregatePlan() = default;
    AggregatePlan(std::span<const ColumnLayout> arg,
                  std::span<const ColumnLayout> groups,
                  AggregateFunction function,
                  Index<Column> value,
                  Index<Column> result);

    [[nodiscard]] auto arg_columns() const& noexcept { return make_view(m_arg, *this); }
    [[nodiscard]] auto arg_columns() const&& = delete;
    [[nodiscard]] auto groups_columns() const& noexcept { return make_view(m_groups, *this); }
    [[nodiscard]] auto groups_columns() const&& = delete;
    [[nodiscard]] auto output_columns() const& noexcept { return make_view(m_output, *this); }
    [[nodiscard]] auto output_columns() const&& = delete;
    /// The group columns in the argument, in the order of the groups' schema.
    [[nodiscard]] std::span<const ColumnSlice> keys() const noexcept { return { m_keys.data(), m_keys.size() }; }
    [[nodiscard]] ColumnSlice value() const noexcept { return m_value; }
    [[nodiscard]] AggregateFunction function() const noexcept { return m_function; }

    static constexpr auto cista_members() noexcept
    {
        return ygg::Members<&AggregatePlan::m_arg,
                            &AggregatePlan::m_groups,
                            &AggregatePlan::m_output,
                            &AggregatePlan::m_keys,
                            &AggregatePlan::m_value,
                            &AggregatePlan::m_function> {};
    }
};

/// The aggregate of each group, from weighted values: a count and a sum per group, and for MIN and MAX the multiset of
/// its values. A group whose count reaches zero is dropped.
class GroupedAggregate
{
    struct Group
    {
        Weight count = 0;
        uint64_t sum = 0;
        std::map<uint_t, Weight> values;
    };
    AggregateFunction m_function;
    Builder<Relation> m_keys;
    std::vector<Group> m_groups;

public:
    GroupedAggregate(AggregateFunction function, std::span<const ColumnLayout> key_columns);

    /// Adds a value of the group `key` with weight `weight` (a row added: 1, removed: -1).
    void add(std::span<const std::byte> key, uint_t value, Weight weight);
    /// The group's aggregate: 0 for COUNT and SUM of a group without rows, none for MIN and MAX.
    [[nodiscard]] std::optional<uint_t> get(std::span<const std::byte> key) const;
    void clear() noexcept;
    [[nodiscard]] size_t memory_usage() const noexcept;
};

namespace detail
{
/// The group key of an argument row, in the groups' column order.
std::span<const std::byte> aggregate_key(std::span<const std::byte> row, const AggregatePlan& plan, std::vector<std::byte>& buffer);
/// The value column of an argument row (0 for COUNT).
uint_t aggregate_value(std::span<const std::byte> row, const AggregatePlan& plan);
/// The output row of a group: its key followed by the aggregate.
std::span<const std::byte> aggregate_row(std::span<const std::byte> key, uint_t value, std::vector<std::byte>& buffer);
}  // namespace detail

/// For each row of `groups`, that row followed by the aggregate of the rows of `arg` that agree with it on the group
/// columns; COUNT and SUM are 0 for a group without rows, MIN and MAX give no row then.
template<RelationViewConcept A, RelationViewConcept G>
void aggregate(const A& arg, const G& groups, const AggregatePlan& plan, Builder<Relation>& out)
{
    detail::require_plan_columns(arg.columns().span(), plan.arg_columns().span());
    detail::require_plan_columns(groups.columns().span(), plan.groups_columns().span());
    auto state = GroupedAggregate(plan.function(), plan.groups_columns().span());
    auto buffer = std::vector<std::byte>();
    for (size_t i = 0; i < arg.size(); ++i)
        state.add(detail::aggregate_key(arg.row(i), plan, buffer), detail::aggregate_value(arg.row(i), plan), 1);
    out.initialize(plan.output_columns().span());
    for (size_t i = 0; i < groups.size(); ++i)
        if (const auto value = state.get(groups.row(i)))
            out.insert(detail::aggregate_row(groups.row(i), *value, buffer));
}
}  // namespace ygg::database

#endif
