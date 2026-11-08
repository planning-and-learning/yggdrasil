/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/semantics/incremental/join.hpp"

namespace ygg::database::incremental
{
JoinEvaluator::JoinEvaluator(const Repository& layer, JoinPlan plan) :
    m_layer(&layer),
    m_plan(std::move(plan)),
    m_lhs(m_plan.lhs_columns()),
    m_rhs(m_plan.rhs_columns()),
    m_result(m_plan.output_columns()),
    m_delta(layer, m_plan.output_columns())
{
}
}

namespace ygg::database::incremental
{
BuilderRelationView JoinEvaluator::get_result() const&
{
    detail::require_initialized(m_initialized);
    return m_layer->view(m_result);
}
}

namespace ygg::database::incremental
{
const Delta& JoinEvaluator::get_delta() const&
{
    detail::require_initialized(m_initialized);
    return m_delta;
}
}

namespace ygg::database::incremental
{
size_t JoinEvaluator::memory_usage() const noexcept
{
    return m_lhs.memory_usage() + m_rhs.memory_usage() + m_result.memory_usage() + m_delta.memory_usage() + urd::memory_usage(m_lhs_index)
           + urd::memory_usage(m_rhs_index);
}
}
