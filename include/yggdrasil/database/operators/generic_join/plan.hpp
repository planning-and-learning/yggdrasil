/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_OPERATORS_GENERIC_JOIN_PLAN_HPP_
#define YGG_DATABASE_OPERATORS_GENERIC_JOIN_PLAN_HPP_

#include "yggdrasil/database/operators/relational/plan.hpp"

#include <cista/containers/vector.h>
#include <cstddef>
#include <span>
#include <vector>

namespace ygg::database
{
/// Prepared full natural join. Variable order controls execution; output order
/// independently preserves the caller's schema. Projection is a separate operator.
class GenericJoinPlan
{
    cista::offset::vector<cista::offset::vector<ColumnLayout>> m_inputs;
    Builder<Columns> m_output;
    cista::offset::vector<Index<Column>> m_order;
    cista::offset::vector<cista::offset::vector<ColumnSlice>> m_keys;
    cista::offset::vector<cista::offset::vector<size_t>> m_occurrences;
    cista::offset::vector<ColumnSlice> m_output_positions;

public:
    GenericJoinPlan() = default;
    GenericJoinPlan(std::span<const std::vector<ColumnLayout>> inputs,
                    std::span<const Index<Column>> variable_order,
                    std::span<const Index<Column>> output_order);

    [[nodiscard]] size_t input_count() const noexcept { return m_inputs.size(); }
    [[nodiscard]] std::span<const ColumnLayout> input_columns(size_t input) const
    {
        const auto& columns = m_inputs.at(input);
        return { columns.data(), columns.size() };
    }
    [[nodiscard]] const auto& output_columns() const& noexcept { return m_output; }
    [[nodiscard]] const auto& output_columns() const&& = delete;
    [[nodiscard]] std::span<const Index<Column>> variable_order() const noexcept { return { m_order.data(), m_order.size() }; }
    [[nodiscard]] std::span<const ColumnSlice> input_keys(size_t input) const
    {
        const auto& keys = m_keys.at(input);
        return { keys.data(), keys.size() };
    }
    [[nodiscard]] std::span<const size_t> variable_inputs(size_t depth) const
    {
        const auto& occurrences = m_occurrences.at(depth);
        return { occurrences.data(), occurrences.size() };
    }
    [[nodiscard]] ColumnSlice output_position(size_t depth) const { return m_output_positions.at(depth); }

    static constexpr auto cista_members() noexcept
    {
        return ygg::Members<&GenericJoinPlan::m_inputs,
                            &GenericJoinPlan::m_output,
                            &GenericJoinPlan::m_order,
                            &GenericJoinPlan::m_keys,
                            &GenericJoinPlan::m_occurrences,
                            &GenericJoinPlan::m_output_positions> {};
    }

    [[nodiscard]] bool matches(const GenericJoinPlan& other) const;
};
}  // namespace ygg::database

#endif
