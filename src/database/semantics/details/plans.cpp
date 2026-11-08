/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/semantics/plans.hpp"

#include <algorithm>
#include <stdexcept>

namespace
{
/// The position of the column labeled `label`; std::out_of_range where there is none.
size_t column_position(std::span<const ygg::ColumnLayout> columns, urd::index<ygg::Column> label)
{
    const auto found = std::ranges::find(columns, label, &ygg::ColumnLayout::label);
    if (found == columns.end())
        throw std::out_of_range("Columns: unknown column label.");
    return static_cast<size_t>(found - columns.begin());
}
}  // namespace

namespace ygg::database
{
ProjectionPlan::ProjectionPlan(std::span<const ColumnLayout> input, std::span<const urd::index<Column>> columns)
{
    validate_columns(input);
    m_input.set(input.begin(), input.end());
    for (const auto label : columns)
    {
        const auto position = column_position(input, label);
        m_output.push_back(input[position]);
        m_positions.push_back(ColumnSlice { position });
    }
    validate_columns(output_columns());
}

JoinPlan::JoinPlan(std::span<const ColumnLayout> lhs, std::span<const ColumnLayout> rhs)
{
    validate_columns(lhs);
    validate_columns(rhs);
    m_lhs.set(lhs.begin(), lhs.end());
    m_rhs.set(rhs.begin(), rhs.end());
    m_output.set(lhs.begin(), lhs.end());
    for (size_t position = 0; position < rhs.size(); ++position)
    {
        const auto& column = rhs[position];
        const auto it = std::ranges::find(lhs, column.label, &ColumnLayout::label);
        if (it == lhs.end())
        {
            m_output.push_back(column);
            m_rhs_payload.push_back(ColumnSlice { position });
        }
        else
        {
            if (it->type != column.type)
                throw std::invalid_argument("Relational operation: common columns must have the same type.");
            m_lhs_keys.push_back(ColumnSlice { static_cast<size_t>(it - lhs.begin()) });
            m_rhs_keys.push_back(ColumnSlice { position });
        }
    }
}

SetPlan::SetPlan(std::span<const ColumnLayout> lhs, std::span<const ColumnLayout> rhs)
{
    validate_columns(lhs);
    if (!std::ranges::equal(lhs, rhs))
        throw std::invalid_argument("Relational operation: input schemas must have the same column order.");
    m_columns.set(lhs.begin(), lhs.end());
}

SelectEqualPlan::SelectEqualPlan(std::span<const ColumnLayout> input, urd::index<Column> lhs, urd::index<Column> rhs) :
    m_lhs(column_position(input, lhs)),
    m_rhs(column_position(input, rhs))
{
    validate_columns(input);
    if (input[m_lhs].type != input[m_rhs].type)
        throw std::invalid_argument("Relational operation: selected columns must have the same type.");
    m_columns.set(input.begin(), input.end());
}

SelectValuePlan::SelectValuePlan(std::span<const ColumnLayout> input, urd::index<Column> column, TypeId type, Value value) :
    m_position(column_position(input, column)),
    m_value(value)
{
    validate_columns(input);
    if (input[m_position].type != type)
        throw std::invalid_argument("Relational operation: selected value has an incompatible column type.");
    m_columns.set(input.begin(), input.end());
}
}  // namespace ygg::database
