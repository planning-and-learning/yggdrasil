/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SEMANTICS_PLANS_HPP_
#define YGG_DATABASE_SEMANTICS_PLANS_HPP_

#include "yggdrasil/database/syntax/columns.hpp"

#include <algorithm>
#include <cista/containers/vector.h>
#include <cstddef>
#include <initializer_list>
#include <limits>
#include <span>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace ygg::database
{

/// A field's physical layout, independent of its column label.
/// Renaming columns preserves compiled keys and reusable join indexes.
struct ColumnSlice
{
    TypeId type;
    size_t offset;
    size_t size;

    bool operator==(const ColumnSlice&) const = default;
    static constexpr auto identifying_members() noexcept { return ygg::Members<&ColumnSlice::type, &ColumnSlice::offset, &ColumnSlice::size> {}; }
    static constexpr auto cista_members() noexcept { return ygg::Members<&ColumnSlice::type, &ColumnSlice::offset, &ColumnSlice::size> {}; }
};

/// Owns typed schemas and resolves field copies once. Reuse with any relation
/// having the same ordered input schema, regardless of its rows or identity.
class ProjectionPlan
{
    Builder<Columns> m_input;
    Builder<Columns> m_output;
    cista::offset::vector<ColumnSlice> m_positions;

    void compile(std::span<const Index<Column>> columns);

public:
    ProjectionPlan() = default;
    ProjectionPlan(std::span<const ColumnLayout> input, std::span<const Index<Column>> columns);
    ProjectionPlan(std::span<const ColumnLayout> input, std::initializer_list<Index<Column>> columns);

    ProjectionPlan(std::span<const Index<Column>> input, std::span<const Index<Column>> columns);
    ProjectionPlan(std::initializer_list<Index<Column>> input, std::initializer_list<Index<Column>> columns);

    [[nodiscard]] auto input_columns() const& noexcept { return make_view(m_input, *this); }
    [[nodiscard]] auto input_columns() const&& = delete;
    [[nodiscard]] auto output_columns() const& noexcept { return make_view(m_output, *this); }
    [[nodiscard]] auto output_columns() const&& = delete;
    [[nodiscard]] std::span<const ColumnSlice> positions() const noexcept { return { m_positions.data(), m_positions.size() }; }

    static constexpr auto cista_members() noexcept
    { return ygg::Members<&ProjectionPlan::m_input, &ProjectionPlan::m_output, &ProjectionPlan::m_positions> {}; }
};

/// Owns typed schemas and resolves natural-join keys and payload copies once.
/// The smaller hash-build side is selected from current input sizes at runtime.
class JoinPlan
{
    Builder<Columns> m_lhs;
    Builder<Columns> m_rhs;
    Builder<Columns> m_output;
    cista::offset::vector<ColumnSlice> m_lhs_keys;
    cista::offset::vector<ColumnSlice> m_rhs_keys;
    cista::offset::vector<ColumnSlice> m_rhs_payload;

    void compile();

public:
    JoinPlan() = default;
    JoinPlan(std::span<const ColumnLayout> lhs, std::span<const ColumnLayout> rhs);

    JoinPlan(std::span<const Index<Column>> lhs, std::span<const Index<Column>> rhs);
    JoinPlan(std::initializer_list<Index<Column>> lhs, std::initializer_list<Index<Column>> rhs);

    [[nodiscard]] auto lhs_columns() const& noexcept { return make_view(m_lhs, *this); }
    [[nodiscard]] auto lhs_columns() const&& = delete;
    [[nodiscard]] auto rhs_columns() const& noexcept { return make_view(m_rhs, *this); }
    [[nodiscard]] auto rhs_columns() const&& = delete;
    [[nodiscard]] auto output_columns() const& noexcept { return make_view(m_output, *this); }
    [[nodiscard]] auto output_columns() const&& = delete;
    [[nodiscard]] std::span<const ColumnSlice> lhs_keys() const noexcept { return { m_lhs_keys.data(), m_lhs_keys.size() }; }
    [[nodiscard]] std::span<const ColumnSlice> rhs_keys() const noexcept { return { m_rhs_keys.data(), m_rhs_keys.size() }; }
    [[nodiscard]] std::span<const ColumnSlice> rhs_payload() const noexcept { return { m_rhs_payload.data(), m_rhs_payload.size() }; }

    static constexpr auto cista_members() noexcept
    { return ygg::Members<&JoinPlan::m_lhs, &JoinPlan::m_rhs, &JoinPlan::m_output, &JoinPlan::m_lhs_keys, &JoinPlan::m_rhs_keys, &JoinPlan::m_rhs_payload> {}; }
};

}  // namespace ygg::database

namespace ygg::database::detail
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
}  // namespace ygg::database::detail

#endif
