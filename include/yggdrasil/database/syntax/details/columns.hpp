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
void validate_column_labels(std::span<const Index<Column>> columns);

/// Validates a schema given by labels alone, whose columns all have type T, and returns its row width.
template<ByteCodable T = uint_t>
size_t label_row_size(std::span<const Index<Column>> labels)
{
    validate_column_labels(labels);
    constexpr auto width = ByteCodec<T>::size;
    if (labels.size() > std::numeric_limits<size_t>::max() / width)
        throw std::length_error("Columns: row byte width exceeds addressable memory.");
    return labels.size() * width;
}

std::span<const std::byte> column_bytes(std::span<const std::byte> bytes, const ColumnLayout& column);

template<ByteCodable T>
T read_column(std::span<const std::byte> bytes, std::span<const ColumnLayout> columns, size_t position)
{
    if (position >= columns.size())
        throw std::out_of_range("Columns: column position is out of range.");
    const auto& column = columns[position];
    if (column.type != type_id<T> || column.size != ByteCodec<T>::size)
        throw std::invalid_argument("Columns: requested value type does not match the column.");
    return ByteCodec<T>::decode(column_bytes(bytes, column));
}

template<ByteCodable T>
void write_column(std::span<std::byte> bytes, std::span<const ColumnLayout> columns, size_t position, T value)
{
    if (position >= columns.size())
        throw std::out_of_range("Columns: column position is out of range.");
    const auto& column = columns[position];
    if (column.type != type_id<T> || column.size != ByteCodec<T>::size)
        throw std::invalid_argument("Columns: supplied value type does not match the column.");
    if (column.offset > bytes.size() || column.size > bytes.size() - column.offset)
        throw std::invalid_argument("Columns: row is too short for the requested field.");
    ByteCodec<T>::encode(value, bytes.subspan(column.offset, column.size));
}
}  // namespace detail

void validate_columns(std::span<const ColumnLayout> columns);

namespace detail
{
/// A relabeled schema keeps the arity and the column types.
void validate_relabel(std::span<const ColumnLayout> previous, std::span<const ColumnLayout> next);

/// Replaces the schema, retaining capacity; the columns may be a slice of the replaced schema.
void assign_columns(Data<Columns>& data, std::span<const ColumnLayout> columns);
}  // namespace detail

size_t column_index(std::span<const ColumnLayout> columns, Index<Column> column);

bool contains_column(std::span<const ColumnLayout> columns, Index<Column> column) noexcept;

std::vector<Index<Column>> column_labels(std::span<const ColumnLayout> columns);

inline size_t row_size(std::span<const ColumnLayout> columns) noexcept { return columns.empty() ? 0 : columns.back().offset + columns.back().size; }
}  // namespace ygg::database

namespace ygg
{

template<ygg::ByteCodable T>
void Builder<database::Columns>::push_back(Index<database::Column> column)
{ push_back(column, ygg::type_id<T>); }

template<ygg::ByteCodable T>
void Builder<database::Columns>::assign(std::span<const Index<database::Column>> columns)
{
    database::detail::label_row_size<T>(columns);
    constexpr auto width = ygg::ByteCodec<T>::size;
    m_data.values.resize(columns.size());
    for (size_t i = 0; i < columns.size(); ++i)
        m_data.values[i] = database::ColumnLayout { columns[i], ygg::type_id<T>, i * width, width };
    ygg::clear(m_data.index);
}

template<ygg::ByteCodable T>
void Builder<database::Columns>::assign(std::initializer_list<Index<database::Column>> columns)
{ assign<T>(std::span<const Index<database::Column>>(columns)); }

}  // namespace ygg

namespace ygg::database
{
template<ColumnsViewConcept V>
Data<Columns>& assign(Data<Columns>& data, const V& source)
{
    detail::assign_columns(data, source.span());
    return data;
}

template<ColumnsViewConcept V>
Builder<Columns>& assign(Builder<Columns>& builder, const V& source)
{
    builder.assign(source.span());
    return builder;
}
}  // namespace ygg::database

#endif
