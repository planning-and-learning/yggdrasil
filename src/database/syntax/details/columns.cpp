/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/syntax/details/columns.hpp"

namespace ygg::database
{
namespace detail
{
void validate_column_labels(std::span<const Index<Column>> columns)
{
    // Schemas are small; validating labels needs no temporary allocation.
    for (auto it = columns.begin(); it != columns.end(); ++it)
        if (std::find(columns.begin(), it, *it) != it)
            throw std::invalid_argument("Columns: duplicate column label.");
}
}
}

namespace ygg::database
{
namespace detail
{
std::span<const std::byte> column_bytes(std::span<const std::byte> bytes, const ColumnLayout& column)
{
    if (column.offset > bytes.size() || column.size > bytes.size() - column.offset)
        throw std::invalid_argument("Columns: row is too short for the requested field.");
    return bytes.subspan(column.offset, column.size);
}
}
}

namespace ygg::database
{
void validate_columns(std::span<const ColumnLayout> columns)
{
    size_t offset = 0;
    for (auto it = columns.begin(); it != columns.end(); ++it)
    {
        const auto size = it->size;
        if (it->offset != offset || size == 0)
            throw std::invalid_argument("Columns: columns must be nonempty and packed contiguously.");
        if (const auto builtin = builtin_column_size(it->type); builtin != 0 && builtin != size)
            throw std::invalid_argument("Columns: column width does not match its type.");
        if (size > std::numeric_limits<size_t>::max() - offset)
            throw std::length_error("Columns: row byte width exceeds addressable memory.");
        offset += size;
        if (std::find_if(columns.begin(), it, [&](const auto& previous) { return previous.label == it->label; }) != it)
            throw std::invalid_argument("Columns: duplicate column label.");
    }
}
}

namespace ygg::database
{
namespace detail
{
void validate_relabel(std::span<const ColumnLayout> previous, std::span<const ColumnLayout> next)
{
    validate_columns(next);
    if (!std::ranges::equal(previous, next, {}, &ColumnLayout::type, &ColumnLayout::type))
        throw std::invalid_argument("Columns: relabeling must keep the arity and column types.");
}
}
}

namespace ygg::database
{
namespace detail
{
void assign_columns(Data<Columns>& data, std::span<const ColumnLayout> columns)
{
    validate_columns(columns);
    auto& values = data.values;
    if (columns.size() <= values.size())
    {
        // Copy before shrinking: columns may refer to a slice of this schema.
        if (columns.data() != values.data())
            std::copy(columns.begin(), columns.end(), values.begin());
        values.resize(columns.size());
    }
    else
        values.set(columns.begin(), columns.end());
    ygg::clear(data.index);
}
}
}

namespace ygg::database
{
size_t column_index(std::span<const ColumnLayout> columns, Index<Column> column)
{
    const auto it = std::ranges::find(columns, column, &ColumnLayout::label);
    if (it == columns.end())
        throw std::out_of_range("Columns: unknown column label.");
    return static_cast<size_t>(it - columns.begin());
}
}

namespace ygg::database
{
bool contains_column(std::span<const ColumnLayout> columns, Index<Column> column) noexcept
{
    return std::ranges::find(columns, column, &ColumnLayout::label) != columns.end();
}
}

namespace ygg::database
{
std::vector<Index<Column>> column_labels(std::span<const ColumnLayout> columns)
{
    std::vector<Index<Column>> result;
    result.reserve(columns.size());
    for (const auto& column : columns)
        result.push_back(column.label);
    return result;
}
}

namespace ygg
{
Builder<database::Columns>::Builder(std::span<const database::ColumnLayout> columns)
{
    assign(columns);
}
}

namespace ygg
{
Builder<database::Columns>::Builder(std::initializer_list<database::ColumnLayout> columns) : Builder(std::span<const database::ColumnLayout>(columns))
{
}
}

namespace ygg
{
Builder<database::Columns>::Builder(std::span<const Index<database::Column>> columns)
{
    assign<>(columns);
}
}

namespace ygg
{
Builder<database::Columns>::Builder(std::initializer_list<Index<database::Column>> columns) : Builder(std::span<const Index<database::Column>>(columns))
{
}
}

namespace ygg
{
void Builder<database::Columns>::push_back(Index<database::Column> column, database::ColumnTypeId type, size_t size)
{
    for (const auto& existing : span())
        if (existing.label == column)
            throw std::invalid_argument("Columns: duplicate column label.");
    const auto offset = row_size();
    if (size == 0)
        throw std::invalid_argument("Columns: columns must be nonempty.");
    if (size > std::numeric_limits<size_t>::max() - offset)
        throw std::length_error("Columns: row byte width exceeds addressable memory.");
    m_data.values.push_back(database::ColumnLayout { column, type, offset, size });
    ygg::clear(m_data.index);
}
}

namespace ygg
{
void Builder<database::Columns>::assign(std::span<const database::ColumnLayout> columns)
{
    database::detail::assign_columns(m_data, columns);
}
}

namespace ygg
{
void Builder<database::Columns>::assign(std::initializer_list<database::ColumnLayout> columns)
{
    assign(std::span<const database::ColumnLayout>(columns));
}
}

namespace ygg
{
void Builder<database::Columns>::rename(std::span<const Index<database::Column>> columns)
{
    if (columns.size() != size())
        throw std::invalid_argument("Columns: renaming must preserve arity.");
    database::detail::validate_column_labels(columns);
    for (size_t i = 0; i < columns.size(); ++i)
        m_data.values[i].label = columns[i];
    ygg::clear(m_data.index);
}
}
