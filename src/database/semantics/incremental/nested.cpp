/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "yggdrasil/database/semantics/incremental/nested.hpp"

#include "yggdrasil/database/semantics/operations.hpp"

#include <algorithm>

namespace ygg::database::incremental
{
NestedJoin::NestedJoin(JoinPlan plan) :
    m_plan(std::move(plan)),
    m_lhs(m_plan.lhs_columns().span(), m_plan.lhs_keys()),
    m_rhs(m_plan.rhs_columns().span(), m_plan.rhs_keys())
{
}

void NestedJoin::emit(std::span<const std::byte> lhs, std::span<const std::byte> rhs, Weight weight, Iteration at, Iteration now, ZSet& output)
{
    if (weight == 0)
        return;
    m_row.assign(lhs.begin(), lhs.end());
    database::detail::append_fields(m_row, rhs, m_plan.rhs_payload());
    if (at == now)
        output.add(m_row, weight);
    else
        m_future.try_emplace(at, columns()).first->second.add(m_row, weight);
}

void NestedJoin::step(const ZSet& lhs, const ZSet& rhs, Iteration iteration, ZSet& output)
{
    output.clear();
    if (const auto it = m_future.find(iteration); it != m_future.end())
    {
        output.add(it->second);
        m_future.erase(it);
    }
    // The rhs change of this iteration is joined with the lhs change below; the lhs change joins the rhs history.
    m_rhs.add(rhs, iteration);
    for (size_t i = 0; i < lhs.size(); ++i)
    {
        const auto row = lhs.row(i);
        const auto weight = lhs.weight(i);
        m_rhs.past().for_each_match(row,
                                    m_plan.lhs_keys(),
                                    [&](std::span<const std::byte> match, std::span<const TimedRows::Entry> entries)
                                    {
                                        for (const auto& entry : entries)
                                            emit(row, match, weight * entry.weight, std::max(iteration, entry.iteration), iteration, output);
                                    });
        m_rhs.current().for_each_match(row,
                                       m_plan.lhs_keys(),
                                       [&](std::span<const std::byte> match, std::span<const TimedRows::Entry> entries)
                                       {
                                           // The current update's rhs changes are at this iteration or earlier.
                                           emit(row, match, weight * TimedRows::up_to(entries, iteration), iteration, iteration, output);
                                       });
    }
    for (size_t i = 0; i < rhs.size(); ++i)
    {
        const auto row = rhs.row(i);
        const auto weight = rhs.weight(i);
        m_lhs.past().for_each_match(row,
                                    m_plan.rhs_keys(),
                                    [&](std::span<const std::byte> match, std::span<const TimedRows::Entry> entries)
                                    {
                                        for (const auto& entry : entries)
                                            emit(match, row, entry.weight * weight, std::max(iteration, entry.iteration), iteration, output);
                                    });
        // The lhs change of this iteration is not yet in the trace: it was joined with this rhs change above.
        m_lhs.current().for_each_match(row,
                                       m_plan.rhs_keys(),
                                       [&](std::span<const std::byte> match, std::span<const TimedRows::Entry> entries)
                                       { emit(match, row, TimedRows::up_to(entries, iteration) * weight, iteration, iteration, output); });
    }
    m_lhs.add(lhs, iteration);
}

bool NestedJoin::pending_after(Iteration iteration) const noexcept { return !m_future.empty() && m_future.rbegin()->first > iteration; }

void NestedJoin::commit()
{
    m_lhs.commit();
    m_rhs.commit();
    m_future.clear();
}

void NestedJoin::clear() noexcept
{
    m_lhs.clear();
    m_rhs.clear();
    m_future.clear();
}

size_t NestedJoin::memory_usage() const noexcept
{
    size_t result = m_lhs.memory_usage() + m_rhs.memory_usage() + m_row.capacity();
    for (const auto& [iteration, zset] : m_future)
        result += zset.memory_usage();
    return result;
}

namespace
{
Weight distinct(Weight weight) noexcept { return weight > 0 ? 1 : 0; }
}  // namespace

NestedDistinct::NestedDistinct(std::span<const ColumnLayout> columns) : m_input(columns, {}), m_candidates(columns) {}

void NestedDistinct::step(const ZSet& input, Iteration iteration, ZSet& output)
{
    output.clear();
    m_candidates.clear();
    if (const auto it = m_scheduled.find(iteration); it != m_scheduled.end())
    {
        for (size_t i = 0; i < it->second.size(); ++i)
            m_candidates.insert(it->second.row(i));
        m_scheduled.erase(it);
    }
    for (size_t i = 0; i < input.size(); ++i)
    {
        const auto row = input.row(i);
        m_candidates.insert(row);
        // The row's value now differs from the previous update's from this iteration on: evaluate it again where its
        // history has later iterations.
        for (const auto& entry : m_input.past().entries(row))
            if (entry.iteration > iteration)
            {
                auto& scheduled = m_scheduled.try_emplace(entry.iteration, input.columns()).first->second;
                if (!scheduled.contains(row))
                    scheduled.insert(row);
            }
    }
    m_input.add(input, iteration);
    for (size_t i = 0; i < m_candidates.size(); ++i)
    {
        const auto row = m_candidates.row(i);
        const auto past = m_input.past().entries(row);
        const auto current = m_input.current().entries(row);
        const auto before_now = TimedRows::up_to(past, iteration);
        const auto now = before_now + TimedRows::up_to(current, iteration);
        auto change = distinct(now) - distinct(before_now);
        if (iteration > 0)
        {
            const auto before_previous = TimedRows::up_to(past, iteration - 1);
            const auto previous = before_previous + TimedRows::up_to(current, iteration - 1);
            change -= distinct(previous) - distinct(before_previous);
        }
        if (change != 0)
            output.add(row, change);
    }
}

bool NestedDistinct::pending_after(Iteration iteration) const noexcept { return !m_scheduled.empty() && m_scheduled.rbegin()->first > iteration; }

void NestedDistinct::commit()
{
    m_input.commit();
    m_scheduled.clear();
}

void NestedDistinct::clear() noexcept
{
    m_input.clear();
    m_scheduled.clear();
}

size_t NestedDistinct::memory_usage() const noexcept
{
    size_t result = m_input.memory_usage() + m_candidates.memory_usage();
    for (const auto& [iteration, rows] : m_scheduled)
        result += rows.memory_usage();
    return result;
}
}  // namespace ygg::database::incremental
