/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SEMANTICS_DISTANCE_PLAN_HPP_
#define YGG_DATABASE_SEMANTICS_DISTANCE_PLAN_HPP_

#include "yggdrasil/database/syntax/columns.hpp"

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

    auto source_columns() const& noexcept { return make_view(m_sources, *this); }
    auto source_columns() const&& = delete;
    auto edge_columns() const& noexcept { return make_view(m_edges, *this); }
    auto edge_columns() const&& = delete;
    auto target_columns() const& noexcept { return make_view(m_targets, *this); }
    auto target_columns() const&& = delete;
    auto output_columns() const& noexcept { return make_view(m_output, *this); }
    auto output_columns() const&& = delete;
    size_t tuple_size() const noexcept { return m_sources.row_size(); }
    size_t arity() const noexcept { return m_sources.size(); }
    static constexpr auto cista_members() noexcept { return ygg::Members<&DistancePlan::m_sources, &DistancePlan::m_edges, &DistancePlan::m_targets, &DistancePlan::m_output> {}; }
};
}  // namespace ygg::database

#endif
