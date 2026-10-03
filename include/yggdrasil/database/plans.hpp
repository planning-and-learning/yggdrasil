/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_PLANS_HPP_
#define YGG_DATABASE_PLANS_HPP_

#include "yggdrasil/database/columns.hpp"

#include <cista/containers/vector.h>
#include <cstddef>
#include <initializer_list>
#include <span>
#include <tuple>

namespace ygg::database
{

/// Owns schemas and resolves projection positions once. Reuse with any relation
/// having the same ordered input schema, regardless of its rows or identity.
class ProjectionPlan
{
private:
    Builder<Columns> m_input;
    Builder<Columns> m_output;
    cista::offset::vector<size_t> m_positions;

public:
    ProjectionPlan() = default;
    ProjectionPlan(std::span<const Index<Column>> input, std::span<const Index<Column>> columns);
    ProjectionPlan(std::span<const Index<Column>> input, std::initializer_list<Index<Column>> columns);
    ProjectionPlan(std::initializer_list<Index<Column>> input, std::initializer_list<Index<Column>> columns);

    auto input_columns() const& noexcept { return make_view(m_input, *this); }
    auto input_columns() const&& = delete;
    auto output_columns() const& noexcept { return make_view(m_output, *this); }
    auto output_columns() const&& = delete;
    std::span<const size_t> positions() const noexcept { return { m_positions.data(), m_positions.size() }; }

    auto cista_members() noexcept { return std::tie(m_input, m_output, m_positions); }
    auto cista_members() const noexcept { return std::tie(m_input, m_output, m_positions); }
};

/// Owns schemas and resolves natural-join keys/payload positions once. The
/// smaller hash-build side is still selected from current input sizes at runtime.
class JoinPlan
{
private:
    Builder<Columns> m_lhs;
    Builder<Columns> m_rhs;
    Builder<Columns> m_output;
    cista::offset::vector<size_t> m_lhs_keys;
    cista::offset::vector<size_t> m_rhs_keys;
    cista::offset::vector<size_t> m_rhs_payload;

public:
    JoinPlan() = default;
    JoinPlan(std::span<const Index<Column>> lhs, std::span<const Index<Column>> rhs);
    JoinPlan(std::initializer_list<Index<Column>> lhs, std::initializer_list<Index<Column>> rhs);

    auto lhs_columns() const& noexcept { return make_view(m_lhs, *this); }
    auto lhs_columns() const&& = delete;
    auto rhs_columns() const& noexcept { return make_view(m_rhs, *this); }
    auto rhs_columns() const&& = delete;
    auto output_columns() const& noexcept { return make_view(m_output, *this); }
    auto output_columns() const&& = delete;
    std::span<const size_t> lhs_keys() const noexcept { return { m_lhs_keys.data(), m_lhs_keys.size() }; }
    std::span<const size_t> rhs_keys() const noexcept { return { m_rhs_keys.data(), m_rhs_keys.size() }; }
    std::span<const size_t> rhs_payload() const noexcept { return { m_rhs_payload.data(), m_rhs_payload.size() }; }

    auto cista_members() noexcept { return std::tie(m_lhs, m_rhs, m_output, m_lhs_keys, m_rhs_keys, m_rhs_payload); }
    auto cista_members() const noexcept { return std::tie(m_lhs, m_rhs, m_output, m_lhs_keys, m_rhs_keys, m_rhs_payload); }
};

}  // namespace ygg::database

#include "yggdrasil/database/details/plans.hpp"

#endif
