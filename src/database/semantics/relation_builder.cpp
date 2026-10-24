/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/semantics/relation_builder.hpp"

namespace ygg
{
Builder<database::Relation>::Builder(Builder<database::Columns> columns) : m_columns(std::move(columns)), m_rows(m_columns.row_size()) {}
}

namespace ygg
{
Builder<database::Relation>::Builder(std::span<const database::ColumnLayout> columns) : Builder(Builder<database::Columns>(columns)) {}
}

namespace ygg
{
Builder<database::Relation>::Builder(std::span<const Index<database::Column>> columns) : Builder(Builder<database::Columns>(columns)) {}
}

namespace ygg
{
Builder<database::Relation>::Builder(std::initializer_list<Index<database::Column>> columns) : Builder(std::span<const Index<database::Column>>(columns)) {}
}

namespace ygg
{
uint_t Builder<database::Relation>::insert(std::span<const std::byte> row)
{
    if (row.size() != m_columns.row_size())
        throw std::invalid_argument("Relation: row byte width does not match its schema.");
    const auto index = m_rows.insert(row);
    ygg::clear(m_index);
    return index;
}
}

namespace ygg
{
uint_t Builder<database::Relation>::insert(database::Row row)
{
    if (!std::ranges::equal(row.columns(), m_columns.span()))
        throw std::invalid_argument("Relation: row schema does not match.");
    const auto index = m_rows.insert(row.bytes());
    ygg::clear(m_index);
    return index;
}
}

namespace ygg
{
std::optional<uint_t> Builder<database::Relation>::find(std::span<const std::byte> row) const
{
    if (row.size() != m_columns.row_size())
        throw std::invalid_argument("Relation: row byte width does not match its schema.");
    return m_rows.find(row);
}
}

namespace ygg
{
std::optional<uint_t> Builder<database::Relation>::find(database::Row row) const
{
    if (!std::ranges::equal(row.columns(), m_columns.span()))
        throw std::invalid_argument("Relation: row schema does not match.");
    return m_rows.find(row.bytes());
}
}

namespace ygg
{
void Builder<database::Relation>::erase(size_t index)
{
    if (index >= size())
        throw std::out_of_range("Relation: row index out of range.");
    m_rows.erase(static_cast<uint_t>(index));
    ygg::clear(m_index);
}
}

namespace ygg
{
std::span<const std::byte> Builder<database::Relation>::row(size_t index) const noexcept
{
    assert(index < size());
    return m_rows[static_cast<uint_t>(index)];
}
}

namespace ygg
{
database::Row Builder<database::Relation>::at(size_t index) const
{
    if (index >= size())
        throw std::out_of_range("Relation: row index out of range.");
    return (*this)[index];
}
}

namespace ygg
{
void Builder<database::Relation>::clear() noexcept
{
    ygg::clear(m_index);
    m_rows.clear();
}
}

namespace ygg
{
void Builder<database::Relation>::rename(std::span<const Index<database::Column>> columns)
{
    m_columns.rename(columns);
    ygg::clear(m_index);
}
}

namespace ygg
{
void Builder<database::Relation>::rename(std::span<const database::ColumnLayout> columns)
{
    database::detail::validate_relabel(m_columns.span(), columns);
    m_columns.assign(columns);
    ygg::clear(m_index);
}
}

namespace ygg
{
void Builder<database::Relation>::initialize(std::span<const database::ColumnLayout> columns)
{
    database::validate_columns(columns);
    reinitialize(database::row_size(columns), columns);
}
}

namespace ygg
{
void Builder<database::Relation>::initialize(std::span<const Index<database::Column>> columns)
{ reinitialize(database::detail::label_row_size<>(columns), columns); }
}

namespace ygg
{
void Builder<database::Relation>::initialize(std::initializer_list<Index<database::Column>> columns)
{ initialize(std::span<const Index<database::Column>>(columns)); }
}

namespace ygg
{
Builder<database::Relation>::Builder(Builder&&) noexcept = default;
Builder<database::Relation>& Builder<database::Relation>::operator=(Builder&&) noexcept = default;
Builder<database::Relation>::~Builder() = default;
}
