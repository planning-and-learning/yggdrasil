/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_PLANS_HPP_
#define YGG_DATABASE_PLANS_HPP_

#include "yggdrasil/database/columns.hpp"

#include <cstddef>
#include <initializer_list>
#include <span>
#include <vector>

namespace ygg::database
{

/// Owns schemas and resolves projection positions once. Reuse with any relation
/// having the same ordered input schema, regardless of its rows or identity.
class ProjectionPlan
{
private:
    Columns m_input;
    Columns m_output;
    std::vector<size_t> m_positions;

public:
    ProjectionPlan(ColumnsView input, ColumnsView columns);
    ProjectionPlan(std::span<const Column> input, std::span<const Column> columns);
    ProjectionPlan(ColumnsView input, std::initializer_list<Column> columns);
    ProjectionPlan(std::span<const Column> input, std::initializer_list<Column> columns);
    ProjectionPlan(std::initializer_list<Column> input, std::initializer_list<Column> columns);

    ColumnsView input_columns() const& noexcept { return m_input.view(); }
    ColumnsView input_columns() const&& = delete;
    ColumnsView output_columns() const& noexcept { return m_output.view(); }
    ColumnsView output_columns() const&& = delete;
    std::span<const size_t> positions() const noexcept { return m_positions; }
};

/// Owns schemas and resolves natural-join keys/payload positions once. The
/// smaller hash-build side is still selected from current input sizes at runtime.
class JoinPlan
{
private:
    Columns m_lhs;
    Columns m_rhs;
    Columns m_output;
    std::vector<size_t> m_lhs_keys;
    std::vector<size_t> m_rhs_keys;
    std::vector<size_t> m_rhs_payload;

public:
    JoinPlan(ColumnsView lhs, ColumnsView rhs);
    JoinPlan(std::span<const Column> lhs, std::span<const Column> rhs);
    JoinPlan(std::initializer_list<Column> lhs, std::initializer_list<Column> rhs);

    ColumnsView lhs_columns() const& noexcept { return m_lhs.view(); }
    ColumnsView lhs_columns() const&& = delete;
    ColumnsView rhs_columns() const& noexcept { return m_rhs.view(); }
    ColumnsView rhs_columns() const&& = delete;
    ColumnsView output_columns() const& noexcept { return m_output.view(); }
    ColumnsView output_columns() const&& = delete;
    std::span<const size_t> lhs_keys() const noexcept { return m_lhs_keys; }
    std::span<const size_t> rhs_keys() const noexcept { return m_rhs_keys; }
    std::span<const size_t> rhs_payload() const noexcept { return m_rhs_payload; }
};

}  // namespace ygg::database

#include "yggdrasil/database/details/plans.hpp"

#endif
