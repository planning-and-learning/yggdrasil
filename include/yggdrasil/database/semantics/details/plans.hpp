/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SEMANTICS_DETAILS_PLANS_HPP_
#define YGG_DATABASE_SEMANTICS_DETAILS_PLANS_HPP_

#include "yggdrasil/database/semantics/plans.hpp"

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

void append_column(std::vector<ColumnLayout>& output, ColumnLayout column);

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

}  // namespace ygg::database

#endif
