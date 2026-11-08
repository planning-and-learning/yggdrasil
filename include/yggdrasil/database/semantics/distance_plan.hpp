/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SEMANTICS_DISTANCE_PLAN_HPP_
#define YGG_DATABASE_SEMANTICS_DISTANCE_PLAN_HPP_

#include "yggdrasil/formalism/relation_view.hpp"

#include <cstddef>
#include <span>

namespace ygg::database
{
/// Sources and targets contain k-field vertices; each edge contains two such
/// vertices. Positional field types must agree, independently of column labels.
/// Output columns are the edge columns followed by a fresh uint_t distance column.
class DistancePlan
{
    urd::flat_vector<ColumnLayout> m_sources;
    urd::flat_vector<ColumnLayout> m_edges;
    urd::flat_vector<ColumnLayout> m_targets;
    urd::flat_vector<ColumnLayout> m_output;

public:
    DistancePlan() = default;
    DistancePlan(std::span<const ColumnLayout> sources,
                 std::span<const ColumnLayout> edges,
                 std::span<const ColumnLayout> targets,
                 urd::index<Column> distance_column);

    [[nodiscard]] std::span<const ColumnLayout> source_columns() const noexcept { return { m_sources.data(), m_sources.size() }; }
    [[nodiscard]] std::span<const ColumnLayout> edge_columns() const noexcept { return { m_edges.data(), m_edges.size() }; }
    [[nodiscard]] std::span<const ColumnLayout> target_columns() const noexcept { return { m_targets.data(), m_targets.size() }; }
    [[nodiscard]] std::span<const ColumnLayout> output_columns() const noexcept { return { m_output.data(), m_output.size() }; }
    [[nodiscard]] size_t tuple_size() const noexcept { return m_sources.size(); }
    [[nodiscard]] size_t arity() const noexcept { return m_sources.size(); }
    static constexpr auto cista_members() noexcept
    { return urd::Members<&DistancePlan::m_sources, &DistancePlan::m_edges, &DistancePlan::m_targets, &DistancePlan::m_output> {}; }
};
}  // namespace ygg::database

#endif
