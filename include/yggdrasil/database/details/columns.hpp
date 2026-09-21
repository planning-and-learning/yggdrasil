/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_DETAILS_COLUMNS_HPP_
#define YGG_DATABASE_DETAILS_COLUMNS_HPP_

#include "yggdrasil/database/columns.hpp"

#include <algorithm>
#include <stdexcept>

namespace ygg::database
{

template<typename C, size_t Extent>
    requires std::same_as<std::remove_const_t<C>, Column>
ColumnsView::ColumnsView(std::span<C, Extent> columns) : m_columns(columns)
{
    // ponytail: quadratic in arity; use a temporary set if wide schemas make validation costly.
    for (auto it = m_columns.begin(); it != m_columns.end(); ++it)
        if (std::find(m_columns.begin(), it, *it) != it)
            throw std::invalid_argument("Columns: duplicate column label.");
}

inline ColumnsView::ColumnsView(const Columns& columns) noexcept : m_columns(columns.m_columns.data(), columns.m_columns.size()) {}

inline ColumnsView::operator std::span<const Column>() const noexcept { return span(); }

inline size_t ColumnsView::column_index(Column column) const
{
    const auto it = std::ranges::find(m_columns, column);
    if (it == m_columns.end())
        throw std::out_of_range("Columns: unknown column label.");
    return static_cast<size_t>(it - m_columns.begin());
}

inline Columns::Columns(const std::vector<Column>& columns) : Columns(ColumnsView(std::span<const Column>(columns))) {}

inline Columns::Columns(std::initializer_list<Column> columns) : Columns(ColumnsView(std::span<const Column>(columns))) {}

inline Columns::Columns(ColumnsView columns) : m_columns(columns.begin(), columns.end()) {}

inline ColumnsView Columns::view() const& noexcept { return ColumnsView(*this); }

inline size_t Columns::column_index(Column column) const { return view().column_index(column); }

inline void Columns::assign(ColumnsView columns)
{
    if (columns.size() <= m_columns.size())
    {
        // Copy before shrinking: columns may refer to a slice of this schema.
        if (columns.data() != m_columns.data())
            std::copy(columns.begin(), columns.end(), m_columns.begin());
        m_columns.resize(columns.size());
    }
    else
        m_columns.set(columns.begin(), columns.end());
}

}  // namespace ygg::database

#endif
