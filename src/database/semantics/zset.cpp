/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "yggdrasil/database/semantics/zset.hpp"

namespace ygg::database
{
ZSet::ZSet(std::span<const ColumnLayout> columns) : m_rows(columns) {}

ZSet::ZSet(std::span<const Index<Column>> columns) : m_rows(columns) {}

Weight ZSet::weight(std::span<const std::byte> row) const
{
    const auto position = m_rows.find(row);
    return position ? m_weights[*position] : 0;
}

Weight ZSet::add(std::span<const std::byte> row, Weight weight)
{
    if (weight == 0)
        return this->weight(row);
    // insert() returns the position of a present row, so a row is hashed once.
    const auto position = m_rows.insert(row);
    if (position == m_weights.size())
    {
        m_weights.push_back(weight);
        return weight;
    }
    const auto result = m_weights[position] += weight;
    if (result == 0)
    {
        m_rows.erase(position);
        m_weights[position] = m_weights.back();
        m_weights.pop_back();
    }
    return result;
}

void ZSet::add(const ZSet& other, Weight factor)
{
    for (size_t i = 0; i < other.size(); ++i)
        add(other.row(i), other.weight(i) * factor);
}

void ZSet::clear() noexcept
{
    m_rows.clear();
    m_weights.clear();
}

void ZSet::initialize(std::span<const ColumnLayout> columns)
{
    m_rows.initialize(columns);
    m_weights.clear();
}
}  // namespace ygg::database
