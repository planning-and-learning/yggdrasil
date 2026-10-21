/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/semantics/incremental/join.hpp"

namespace ygg::database::incremental
{
JoinEvaluator::JoinEvaluator(JoinPlan plan) :
    m_plan(std::move(plan)),
    m_lhs(m_plan.lhs_columns().span()),
    m_rhs(m_plan.rhs_columns().span()),
    m_result(m_plan.output_columns().span()),
    m_delta(m_plan.output_columns().span())
{
}
}

namespace ygg::database::incremental
{
const Builder<Relation>& JoinEvaluator::get_result() const&
{
    detail::require_initialized(m_initialized);
    return m_result;
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
    return m_lhs.memory_usage() + m_rhs.memory_usage() + m_result.memory_usage() + m_delta.memory_usage() + m_lhs_index.memory_usage()
           + m_rhs_index.memory_usage();
}
}
