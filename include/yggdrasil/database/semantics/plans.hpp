/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SEMANTICS_PLANS_HPP_
#define YGG_DATABASE_SEMANTICS_PLANS_HPP_

#include "yggdrasil/database/syntax/columns.hpp"

#include <cista/containers/vector.h>
#include <cstddef>
#include <initializer_list>
#include <span>
#include <tuple>

namespace ygg::database
{

/// A field's physical layout, independent of its column label.
/// Renaming columns preserves compiled keys and reusable join indexes.
struct ColumnSlice
{
    size_t type;
    size_t offset;
    size_t size;

    bool operator==(const ColumnSlice&) const = default;
    auto identifying_members() const noexcept { return std::tie(type, offset, size); }
    auto cista_members() noexcept { return std::tie(type, offset, size); }
    auto cista_members() const noexcept { return std::tie(type, offset, size); }
};

/// Owns typed schemas and resolves field copies once. Reuse with any relation
/// having the same ordered input schema, regardless of its rows or identity.
template<ColumnTypes Values = DefaultColumnTypes>
class ProjectionPlan
{
    Builder<Columns<Values>> m_input;
    Builder<Columns<Values>> m_output;
    cista::offset::vector<ColumnSlice> m_positions;

public:
    ProjectionPlan() = default;
    ProjectionPlan(std::span<const ColumnLayout> input, std::span<const Index<Column>> columns);
    ProjectionPlan(std::span<const ColumnLayout> input, std::initializer_list<Index<Column>> columns);

    ProjectionPlan(std::span<const Index<Column>> input, std::span<const Index<Column>> columns);
    ProjectionPlan(std::initializer_list<Index<Column>> input, std::initializer_list<Index<Column>> columns);

    auto input_columns() const& noexcept { return make_view(m_input, *this); }
    auto input_columns() const&& = delete;
    auto output_columns() const& noexcept { return make_view(m_output, *this); }
    auto output_columns() const&& = delete;
    std::span<const ColumnSlice> positions() const noexcept { return { m_positions.data(), m_positions.size() }; }

    auto cista_members() noexcept { return std::tie(m_input, m_output, m_positions); }
    auto cista_members() const noexcept { return std::tie(m_input, m_output, m_positions); }
};

/// Owns typed schemas and resolves natural-join keys and payload copies once.
/// The smaller hash-build side is selected from current input sizes at runtime.
template<ColumnTypes Values = DefaultColumnTypes>
class JoinPlan
{
    Builder<Columns<Values>> m_lhs;
    Builder<Columns<Values>> m_rhs;
    Builder<Columns<Values>> m_output;
    cista::offset::vector<ColumnSlice> m_lhs_keys;
    cista::offset::vector<ColumnSlice> m_rhs_keys;
    cista::offset::vector<ColumnSlice> m_rhs_payload;

public:
    JoinPlan() = default;
    JoinPlan(std::span<const ColumnLayout> lhs, std::span<const ColumnLayout> rhs);

    JoinPlan(std::span<const Index<Column>> lhs, std::span<const Index<Column>> rhs);
    JoinPlan(std::initializer_list<Index<Column>> lhs, std::initializer_list<Index<Column>> rhs);

    auto lhs_columns() const& noexcept { return make_view(m_lhs, *this); }
    auto lhs_columns() const&& = delete;
    auto rhs_columns() const& noexcept { return make_view(m_rhs, *this); }
    auto rhs_columns() const&& = delete;
    auto output_columns() const& noexcept { return make_view(m_output, *this); }
    auto output_columns() const&& = delete;
    std::span<const ColumnSlice> lhs_keys() const noexcept { return { m_lhs_keys.data(), m_lhs_keys.size() }; }
    std::span<const ColumnSlice> rhs_keys() const noexcept { return { m_rhs_keys.data(), m_rhs_keys.size() }; }
    std::span<const ColumnSlice> rhs_payload() const noexcept { return { m_rhs_payload.data(), m_rhs_payload.size() }; }

    auto cista_members() noexcept { return std::tie(m_lhs, m_rhs, m_output, m_lhs_keys, m_rhs_keys, m_rhs_payload); }
    auto cista_members() const noexcept { return std::tie(m_lhs, m_rhs, m_output, m_lhs_keys, m_rhs_keys, m_rhs_payload); }
};

}  // namespace ygg::database

#include "yggdrasil/database/semantics/details/plans.hpp"

#endif
