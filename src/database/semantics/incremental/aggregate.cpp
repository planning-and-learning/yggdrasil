/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "yggdrasil/database/semantics/incremental/aggregate.hpp"

namespace ygg::database::incremental
{
AggregateEvaluator::AggregateEvaluator(AggregatePlan plan) :
    m_plan(std::move(plan)),
    m_state(m_plan.function(), m_plan.groups_columns().span()),
    m_groups(m_plan.groups_columns().span()),
    m_result(m_plan.output_columns().span()),
    m_delta(m_plan.output_columns().span()),
    m_touched(m_plan.groups_columns().span())
{
}

std::optional<uint_t> AggregateEvaluator::output(std::span<const std::byte> key) const { return m_groups.contains(key) ? m_state.get(key) : std::nullopt; }

void AggregateEvaluator::touch(std::span<const std::byte> key)
{
    if (m_touched.contains(key))
        return;
    m_touched.insert(key);
    m_before.push_back(output(key));
}

const Builder<Relation>& AggregateEvaluator::get_result() const&
{
    detail::require_initialized(m_initialized);
    return m_result;
}

const Delta& AggregateEvaluator::get_delta() const&
{
    detail::require_initialized(m_initialized);
    return m_delta;
}

size_t AggregateEvaluator::memory_usage() const noexcept
{
    return m_state.memory_usage() + m_groups.memory_usage() + m_result.memory_usage() + m_delta.memory_usage() + m_touched.memory_usage()
           + m_before.capacity() * sizeof(std::optional<uint_t>) + m_key.capacity() + m_row.capacity();
}
}  // namespace ygg::database::incremental
