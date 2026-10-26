/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "yggdrasil/database/semantics/incremental/trace.hpp"

#include <algorithm>
#include <cassert>

namespace ygg::database::incremental
{
TimedRows::TimedRows(std::span<const ColumnLayout> columns, std::span<const ColumnSlice> keys) : m_rows(columns), m_keys(keys.begin(), keys.end()) {}

void TimedRows::add(std::span<const std::byte> row, Iteration iteration, Weight weight)
{
    if (weight == 0)
        return;
    // insert() returns the position of a present row, so a row is hashed once.
    const auto position = m_rows.insert(row);
    if (position == m_entries.size())
    {
        m_entries.emplace_back();
        if (!m_keys.empty())
            m_index.insert(database::detail::join_key_hash(row, m_keys), position);
    }
    auto& entries = m_entries[position];
    const auto it = std::ranges::lower_bound(entries, iteration, {}, &Entry::iteration);
    if (it != entries.end() && it->iteration == iteration)
    {
        it->weight += weight;
        if (it->weight == 0)
            entries.erase(it);
    }
    else
        entries.insert(it, Entry { iteration, weight });
    if (entries.empty())
        erase(position);
}

std::span<const TimedRows::Entry> TimedRows::entries(std::span<const std::byte> row) const
{
    const auto position = m_rows.find(row);
    return position ? std::span<const Entry>(m_entries[*position]) : std::span<const Entry>();
}

void TimedRows::move_into(TimedRows& other)
{
    for (size_t i = 0; i < m_rows.size(); ++i)
        for (const auto& entry : m_entries[i])
            other.add(m_rows.row(i), entry.iteration, entry.weight);
    clear();
}

void TimedRows::clear() noexcept
{
    m_rows.clear();
    m_entries.clear();
    m_index.clear();
}

size_t TimedRows::memory_usage() const noexcept
{
    size_t result = m_rows.memory_usage() + m_entries.capacity() * sizeof(std::vector<Entry>) + m_index.memory_usage();
    for (const auto& entries : m_entries)
        result += entries.capacity() * sizeof(Entry);
    return result;
}

Weight TimedRows::up_to(std::span<const Entry> entries, Iteration iteration) noexcept
{
    Weight result = 0;
    for (const auto& entry : entries)
    {
        if (entry.iteration > iteration)
            break;
        result += entry.weight;
    }
    return result;
}

void TimedRows::erase(uint_t position)
{
    const auto last = to_uint_t(m_rows.size() - 1);
    if (!m_keys.empty())
    {
        [[maybe_unused]] const auto erased = m_index.erase(database::detail::join_key_hash(m_rows.row(position), m_keys), position);
        assert(erased);
        if (position != last)
        {
            [[maybe_unused]] const auto replaced = m_index.replace(database::detail::join_key_hash(m_rows.row(last), m_keys), last, position);
            assert(replaced);
        }
    }
    m_rows.erase(position);
    m_entries[position] = std::move(m_entries.back());
    m_entries.pop_back();
}

Trace::Trace(std::span<const ColumnLayout> columns, std::span<const ColumnSlice> keys) : m_past(columns, keys), m_current(columns, keys) {}

void Trace::add(const ZSet& change, Iteration iteration)
{
    for (size_t i = 0; i < change.size(); ++i)
        m_current.add(change.row(i), iteration, change.weight(i));
}

void Trace::clear() noexcept
{
    m_past.clear();
    m_current.clear();
}
}  // namespace ygg::database::incremental
