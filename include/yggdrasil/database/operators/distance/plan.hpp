/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_OPERATORS_DISTANCE_PLAN_HPP_
#define YGG_DATABASE_OPERATORS_DISTANCE_PLAN_HPP_

#include "yggdrasil/database/relation/columns.hpp"

#include <cstddef>
#include <span>

namespace ygg::database
{
/// Sources and targets contain k-field vertices; each edge contains two such
/// vertices. Positional field types must agree, independently of column labels.
/// Output columns are the edge columns followed by a fresh uint_t distance column.
class DistancePlan
{
    Builder<Columns> m_sources;
    Builder<Columns> m_edges;
    Builder<Columns> m_targets;
    Builder<Columns> m_output;

public:
    DistancePlan() = default;
    DistancePlan(std::span<const ColumnLayout> sources,
                 std::span<const ColumnLayout> edges,
                 std::span<const ColumnLayout> targets,
                 Index<Column> distance_column);

    [[nodiscard]] auto source_columns() const& noexcept { return make_view(m_sources, *this); }
    [[nodiscard]] auto source_columns() const&& = delete;
    [[nodiscard]] auto edge_columns() const& noexcept { return make_view(m_edges, *this); }
    [[nodiscard]] auto edge_columns() const&& = delete;
    [[nodiscard]] auto target_columns() const& noexcept { return make_view(m_targets, *this); }
    [[nodiscard]] auto target_columns() const&& = delete;
    [[nodiscard]] auto output_columns() const& noexcept { return make_view(m_output, *this); }
    [[nodiscard]] auto output_columns() const&& = delete;
    [[nodiscard]] size_t tuple_size() const noexcept { return m_sources.row_size(); }
    [[nodiscard]] size_t arity() const noexcept { return m_sources.size(); }
    static constexpr auto cista_members() noexcept
    { return ygg::Members<&DistancePlan::m_sources, &DistancePlan::m_edges, &DistancePlan::m_targets, &DistancePlan::m_output> {}; }
};
}  // namespace ygg::database

#endif
