/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/operators/relational/incremental/projection.hpp"

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
bool ProjectionEvaluator::add(std::span<const std::byte> row)
{
    const auto index = m_result.insert(Row(row, m_plan.output_columns().span()));
    if (index == m_supports.size())
    {
        m_supports.push_back(1);
        return true;
    }
    auto& count = m_supports[index];
    if (count == std::numeric_limits<size_t>::max())
        throw std::overflow_error("Incremental projection: support count overflow.");
    ++count;
    return false;
}
}

namespace ygg::database::incremental
{
void ProjectionEvaluator::remove(std::span<const std::byte> row)
{
    const auto index = m_result.find(Row(row, m_plan.output_columns().span()));
    if (!index || m_supports[*index] == 0)
        throw std::invalid_argument("Incremental projection: removal has no supporting row.");
    if (--m_supports[*index] == 0)
        m_pending_removals.push_back(*index);
}
}

namespace ygg::database::incremental
{
ProjectionEvaluator::ProjectionEvaluator(ProjectionPlan plan) :
    m_plan(std::move(plan)),
    m_result(m_plan.output_columns().span()),
    m_delta(m_plan.output_columns().span())
{
}
}

namespace ygg::database::incremental
{
const Builder<Relation>& ProjectionEvaluator::get_result() const&
{
    detail::require_initialized(m_initialized);
    return m_result;
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
size_t ProjectionEvaluator::memory_usage() const noexcept
{ return m_result.memory_usage() + m_delta.memory_usage() + m_supports.capacity() * sizeof(size_t) + m_pending_removals.capacity() * sizeof(uint_t); }
}
