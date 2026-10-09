/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_DETAILS_PLANS_HPP_
#define YGG_DATABASE_DETAILS_PLANS_HPP_

#include "yggdrasil/database/plans.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <vector>

namespace ygg::database
{

namespace detail
{
/// Prepared plans and workspaces retain their slices in Cista and standard vectors.
template<typename Buffer>
concept ColumnSliceBuffer = requires(Buffer& buffer, size_t capacity, ColumnSlice slice) {
    buffer.clear();
    buffer.reserve(capacity);
    buffer.push_back(slice);
};

inline ColumnSlice column_slice(const ColumnLayout& column) noexcept { return { column.type, column.offset, column.size }; }

inline void append_column(std::vector<ColumnLayout>& output, ColumnLayout column)
{
    column.offset = output.empty() ? 0 : output.back().offset + output.back().size;
    if (column.size > std::numeric_limits<size_t>::max() - column.offset)
        throw std::overflow_error("Relational operation: row size overflow.");
    output.push_back(column);
}

template<ColumnSliceBuffer Slices>
void projection_positions(std::span<const ColumnLayout> input, std::span<const Index<Column>> labels, std::vector<ColumnLayout>& columns, Slices& positions)
{
    columns.clear();
    columns.reserve(labels.size());
    positions.clear();
    positions.reserve(labels.size());
    for (const auto label : labels)
    {
        const auto& column = input[column_index(input, label)];
        append_column(columns, column);
        positions.push_back(column_slice(column));
    }
}

template<ColumnSliceBuffer Slices>
void join_positions(std::span<const ColumnLayout> lhs,
                    std::span<const ColumnLayout> rhs,
                    std::vector<ColumnLayout>& columns,
                    Slices& lhs_keys,
                    Slices& rhs_keys,
                    Slices& rhs_payload)
{
    columns.assign(lhs.begin(), lhs.end());
    lhs_keys.clear();
    rhs_keys.clear();
    rhs_payload.clear();
    for (const auto& column : rhs)
    {
        const auto it = std::ranges::find(lhs, column.label, &ColumnLayout::label);
        if (it == lhs.end())
        {
            append_column(columns, column);
            rhs_payload.push_back(column_slice(column));
        }
        else
        {
            if (it->type != column.type || it->size != column.size)
                throw std::invalid_argument("Relational operation: common columns must have the same type.");
            lhs_keys.push_back(column_slice(*it));
            rhs_keys.push_back(column_slice(column));
        }
    }
}
}  // namespace detail

template<ColumnTypes Values>
ProjectionPlan<Values>::ProjectionPlan(std::span<const ColumnLayout> input, std::span<const Index<Column>> columns) : m_input(input)
{
    auto output = std::vector<ColumnLayout>();
    detail::projection_positions(m_input.span(), columns, output, m_positions);
    m_output.assign(output);
}

template<ColumnTypes Values>
ProjectionPlan<Values>::ProjectionPlan(std::span<const ColumnLayout> input, std::initializer_list<Index<Column>> columns) :
    ProjectionPlan(input, std::span<const Index<Column>>(columns))
{
}

template<ColumnTypes Values>
JoinPlan<Values>::JoinPlan(std::span<const ColumnLayout> lhs, std::span<const ColumnLayout> rhs) : m_lhs(lhs), m_rhs(rhs)
{
    auto columns = std::vector<ColumnLayout>();
    detail::join_positions(m_lhs.span(), m_rhs.span(), columns, m_lhs_keys, m_rhs_keys, m_rhs_payload);
    m_output.assign(columns);
}

template<ColumnTypes Values>
ProjectionPlan<Values>::ProjectionPlan(std::span<const Index<Column>> input, std::span<const Index<Column>> columns) : m_input(input)
{
    auto output = std::vector<ColumnLayout>();
    detail::projection_positions(m_input.span(), columns, output, m_positions);
    m_output.assign(output);
}

template<ColumnTypes Values>
ProjectionPlan<Values>::ProjectionPlan(std::initializer_list<Index<Column>> input, std::initializer_list<Index<Column>> columns) :
    ProjectionPlan(std::span<const Index<Column>>(input), std::span<const Index<Column>>(columns))
{
}

template<ColumnTypes Values>
JoinPlan<Values>::JoinPlan(std::span<const Index<Column>> lhs, std::span<const Index<Column>> rhs) : m_lhs(lhs), m_rhs(rhs)
{
    auto columns = std::vector<ColumnLayout>();
    detail::join_positions(m_lhs.span(), m_rhs.span(), columns, m_lhs_keys, m_rhs_keys, m_rhs_payload);
    m_output.assign(columns);
}

template<ColumnTypes Values>
JoinPlan<Values>::JoinPlan(std::initializer_list<Index<Column>> lhs, std::initializer_list<Index<Column>> rhs) :
    JoinPlan(std::span<const Index<Column>>(lhs), std::span<const Index<Column>>(rhs))
{
}

}  // namespace ygg::database

#endif
