/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SYNTAX_DETAILS_COLUMNS_HPP_
#define YGG_DATABASE_SYNTAX_DETAILS_COLUMNS_HPP_

#include "yggdrasil/database/syntax/columns_view.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace ygg::database
{
namespace detail
{
inline void validate_column_labels(std::span<const Index<Column>> columns)
{
    // Schemas are small; validating labels needs no temporary allocation.
    for (auto it = columns.begin(); it != columns.end(); ++it)
        if (std::find(columns.begin(), it, *it) != it)
            throw std::invalid_argument("Columns: duplicate column label.");
}

/// Validates an untyped schema, whose columns take the first registered type, and returns its row width.
template<ColumnTypes Values>
size_t label_row_size(std::span<const Index<Column>> labels)
{
    validate_column_labels(labels);
    const auto width = column_size<Values>(0);
    if (labels.size() > std::numeric_limits<size_t>::max() / width)
        throw std::length_error("Columns: row byte width exceeds addressable memory.");
    return labels.size() * width;
}

inline std::span<const std::byte> column_bytes(std::span<const std::byte> bytes, const ColumnLayout& column)
{
    if (column.offset > bytes.size() || column.size > bytes.size() - column.offset)
        throw std::invalid_argument("Columns: row is too short for the requested field.");
    return bytes.subspan(column.offset, column.size);
}

template<ColumnTypes Values, ColumnValueFor<Values> T>
T read_column(std::span<const std::byte> bytes, std::span<const ColumnLayout> columns, size_t position)
{
    if (position >= columns.size())
        throw std::out_of_range("Columns: column position is out of range.");
    const auto& column = columns[position];
    if (column.type != column_type<Values, T>)
        throw std::invalid_argument("Columns: requested value type does not match the column.");
    return ColumnCodec<T>::decode(column_bytes(bytes, column));
}

template<ColumnTypes Values, ColumnValueFor<Values> T>
void write_column(std::span<std::byte> bytes, std::span<const ColumnLayout> columns, size_t position, T value)
{
    if (position >= columns.size())
        throw std::out_of_range("Columns: column position is out of range.");
    const auto& column = columns[position];
    if (column.type != column_type<Values, T>)
        throw std::invalid_argument("Columns: supplied value type does not match the column.");
    if (column.offset > bytes.size() || column.size > bytes.size() - column.offset)
        throw std::invalid_argument("Columns: row is too short for the requested field.");
    ColumnCodec<T>::encode(value, bytes.subspan(column.offset, column.size));
}
}  // namespace detail

template<ColumnTypes Values>
void validate_columns(std::span<const ColumnLayout> columns)
{
    size_t offset = 0;
    for (auto it = columns.begin(); it != columns.end(); ++it)
    {
        const auto size = column_size<Values>(it->type);
        if (it->offset != offset || it->size != size)
            throw std::invalid_argument("Columns: layout does not match its registered types.");
        if (size > std::numeric_limits<size_t>::max() - offset)
            throw std::length_error("Columns: row byte width exceeds addressable memory.");
        offset += size;
        if (std::find_if(columns.begin(), it, [&](const auto& previous) { return previous.label == it->label; }) != it)
            throw std::invalid_argument("Columns: duplicate column label.");
    }
}

namespace detail
{
/// A relabeled schema keeps the arity and the column types.
template<ColumnTypes Values>
void validate_relabel(std::span<const ColumnLayout> previous, std::span<const ColumnLayout> next)
{
    validate_columns<Values>(next);
    if (!std::ranges::equal(previous, next, {}, &ColumnLayout::type, &ColumnLayout::type))
        throw std::invalid_argument("Columns: relabeling must keep the arity and column types.");
}

/// Replaces the schema, retaining capacity; the columns may be a slice of the replaced schema.
template<ColumnTypes Values>
void assign_columns(Data<Columns<Values>>& data, std::span<const ColumnLayout> columns)
{
    validate_columns<Values>(columns);
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
}  // namespace detail

inline size_t column_index(std::span<const ColumnLayout> columns, Index<Column> column)
{
    const auto it = std::ranges::find(columns, column, &ColumnLayout::label);
    if (it == columns.end())
        throw std::out_of_range("Columns: unknown column label.");
    return static_cast<size_t>(it - columns.begin());
}

inline bool contains_column(std::span<const ColumnLayout> columns, Index<Column> column) noexcept
{
    return std::ranges::find(columns, column, &ColumnLayout::label) != columns.end();
}

inline std::vector<Index<Column>> column_labels(std::span<const ColumnLayout> columns)
{
    std::vector<Index<Column>> result;
    result.reserve(columns.size());
    for (const auto& column : columns)
        result.push_back(column.label);
    return result;
}

inline size_t row_size(std::span<const ColumnLayout> columns) noexcept { return columns.empty() ? 0 : columns.back().offset + columns.back().size; }

template<ColumnTypes Values>
void validate_row(std::span<const std::byte> bytes, std::span<const ColumnLayout> columns)
{
    if (bytes.size() != row_size(columns))
        throw std::invalid_argument("Relation: row byte width does not match its schema.");
    for (const auto& column : columns)
        visit_column_type<Values>(column.type,
                                  [&]<typename T>(std::type_identity<T>)
                                  {
                                      const auto source = detail::column_bytes(bytes, column);
                                      std::array<std::byte, ColumnCodec<T>::size> canonical;
                                      ColumnCodec<T>::encode(ColumnCodec<T>::decode(source), canonical);
                                      if (!std::ranges::equal(source, canonical))
                                          throw std::invalid_argument("Relation: row contains a noncanonical value encoding.");
                                  });
}
}  // namespace ygg::database

namespace ygg
{
template<database::ColumnTypes Values>
Builder<database::Columns<Values>>::Builder(std::span<const database::ColumnLayout> columns)
{
    assign(columns);
}
template<database::ColumnTypes Values>
Builder<database::Columns<Values>>::Builder(std::initializer_list<database::ColumnLayout> columns) : Builder(std::span<const database::ColumnLayout>(columns))
{
}
template<database::ColumnTypes Values>
Builder<database::Columns<Values>>::Builder(std::span<const Index<database::Column>> columns)
{
    assign(columns);
}
template<database::ColumnTypes Values>
Builder<database::Columns<Values>>::Builder(std::initializer_list<Index<database::Column>> columns) : Builder(std::span<const Index<database::Column>>(columns))
{
}

template<database::ColumnTypes Values>
template<database::ColumnValueFor<Values> T>
void Builder<database::Columns<Values>>::push_back(Index<database::Column> column)
{
    for (const auto& existing : span())
        if (existing.label == column)
            throw std::invalid_argument("Columns: duplicate column label.");
    const auto offset = row_size();
    constexpr auto size = database::ColumnCodec<T>::size;
    if (size > std::numeric_limits<size_t>::max() - offset)
        throw std::length_error("Columns: row byte width exceeds addressable memory.");
    m_data.values.push_back(database::ColumnLayout { column, database::column_type<Values, T>, offset, size });
    ygg::clear(m_data.index);
}

template<database::ColumnTypes Values>
void Builder<database::Columns<Values>>::push_back(Index<database::Column> column, size_t type)
{
    database::visit_column_type<Values>(type, [&]<typename T>(std::type_identity<T>) { push_back<T>(column); });
}

template<database::ColumnTypes Values>
void Builder<database::Columns<Values>>::assign(std::span<const database::ColumnLayout> columns)
{
    database::detail::assign_columns<Values>(m_data, columns);
}

template<database::ColumnTypes Values>
void Builder<database::Columns<Values>>::assign(std::initializer_list<database::ColumnLayout> columns)
{
    assign(std::span<const database::ColumnLayout>(columns));
}

template<database::ColumnTypes Values>
void Builder<database::Columns<Values>>::assign(std::span<const Index<database::Column>> columns)
{
    database::detail::label_row_size<Values>(columns);
    const auto width = database::column_size<Values>(0);
    m_data.values.resize(columns.size());
    for (size_t i = 0; i < columns.size(); ++i)
        m_data.values[i] = database::ColumnLayout { columns[i], 0, i * width, width };
    ygg::clear(m_data.index);
}

template<database::ColumnTypes Values>
void Builder<database::Columns<Values>>::assign(std::initializer_list<Index<database::Column>> columns)
{
    assign(std::span<const Index<database::Column>>(columns));
}

template<database::ColumnTypes Values>
void Builder<database::Columns<Values>>::rename(std::span<const Index<database::Column>> columns)
{
    if (columns.size() != size())
        throw std::invalid_argument("Columns: renaming must preserve arity.");
    database::detail::validate_column_labels(columns);
    for (size_t i = 0; i < columns.size(); ++i)
        m_data.values[i].label = columns[i];
    ygg::clear(m_data.index);
}
}  // namespace ygg

namespace ygg::database
{
template<ColumnTypes Values, ColumnsViewConcept<Values> V>
Data<Columns<Values>>& assign(Data<Columns<Values>>& data, const V& source)
{
    detail::assign_columns<Values>(data, source.span());
    return data;
}

template<ColumnTypes Values, ColumnsViewConcept<Values> V>
Builder<Columns<Values>>& assign(Builder<Columns<Values>>& builder, const V& source)
{
    builder.assign(source.span());
    return builder;
}
}  // namespace ygg::database

#endif
