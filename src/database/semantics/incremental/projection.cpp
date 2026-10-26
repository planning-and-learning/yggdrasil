/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/semantics/incremental/projection.hpp"

namespace ygg::database::incremental
{
std::span<const std::byte> ProjectionEvaluator::project_row(std::span<const std::byte> input, Workspace& workspace) const
{
    auto& row = workspace.row;
    row.clear();
    row.reserve(m_plan.output_columns().row_size());
    database::detail::append_fields(row, input, m_plan.positions());
    return row;
}
}

namespace ygg::database::incremental
{
ProjectionEvaluator::ProjectionEvaluator(ProjectionPlan plan) :
    m_plan(std::move(plan)),
    m_supports(m_plan.output_columns().span()),
    m_delta(m_plan.output_columns().span()),
    m_removals(m_plan.output_columns().span())
{
}
}

namespace ygg::database::incremental
{
const Builder<Relation>& ProjectionEvaluator::get_result() const&
{
    detail::require_initialized(m_initialized);
    return m_supports.rows();
}
}

namespace ygg::database::incremental
{
const Delta& ProjectionEvaluator::get_delta() const&
{
    detail::require_initialized(m_initialized);
    return m_delta;
}
}

namespace ygg::database::incremental
{
size_t ProjectionEvaluator::memory_usage() const noexcept { return m_supports.memory_usage() + m_delta.memory_usage() + m_removals.memory_usage(); }
}
