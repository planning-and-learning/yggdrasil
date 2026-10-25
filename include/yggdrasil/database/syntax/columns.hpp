/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SYNTAX_COLUMNS_HPP_
#define YGG_DATABASE_SYNTAX_COLUMNS_HPP_

#include "yggdrasil/database/declarations.hpp"
#include "yggdrasil/database/syntax/columns_data.hpp"

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <initializer_list>
#include <limits>
#include <span>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <vector>

namespace ygg::database
{
/// Validate unique labels and nonempty, contiguously packed columns.
void validate_columns(std::span<const ColumnLayout> columns);
size_t column_index(std::span<const ColumnLayout> columns, Index<Column> column);
bool contains_column(std::span<const ColumnLayout> columns, Index<Column> column) noexcept;
/// The labels in schema order.
std::vector<Index<Column>> column_labels(std::span<const ColumnLayout> columns);
size_t row_size(std::span<const ColumnLayout> columns) noexcept;
/// The schema is already validated; validate the width and canonical field encodings.
void validate_row(std::span<const std::byte> bytes, std::span<const ColumnLayout> columns);

namespace detail
{
template<ByteCodable T>
T read_column(std::span<const std::byte> bytes, std::span<const ColumnLayout> columns, size_t position);
template<ByteCodable T>
void write_column(std::span<std::byte> bytes, std::span<const ColumnLayout> columns, size_t position, T value);
}  // namespace detail
}  // namespace ygg::database

namespace ygg
{
/// Owns a validated typed schema. Changing it clears its canonical index and
/// retains column storage. Row byte width is independent of logical arity.
template<>
struct Builder<database::Columns>
{
private:
    Data<database::Columns> m_data;

public:
    Builder() = default;
    explicit Builder(std::span<const database::ColumnLayout> columns);
    Builder(std::initializer_list<database::ColumnLayout> columns);
    /// Labels alone give every column type uint_t; use assign<T> for another type.
    explicit Builder(std::span<const Index<database::Column>> columns);
    Builder(std::initializer_list<Index<database::Column>> columns);

    auto& get_data() noexcept { return m_data; }
    [[nodiscard]] const auto& get_data() const noexcept { return m_data; }
    [[nodiscard]] auto get_index() const noexcept { return m_data.index; }
    void set_index(Index<database::Columns> index) noexcept { m_data.index = index; }

    [[nodiscard]] std::span<const database::ColumnLayout> span() const& noexcept { return { m_data.values.data(), m_data.values.size() }; }
    [[nodiscard]] std::span<const database::ColumnLayout> span() const&& = delete;
    [[nodiscard]] auto begin() const noexcept { return m_data.values.begin(); }
    [[nodiscard]] auto end() const noexcept { return m_data.values.end(); }
    [[nodiscard]] const database::ColumnLayout* data() const noexcept { return m_data.values.data(); }
    [[nodiscard]] size_t size() const noexcept { return m_data.values.size(); }
    [[nodiscard]] bool empty() const noexcept { return m_data.values.empty(); }
    const database::ColumnLayout& operator[](size_t index) const noexcept { return m_data.values[index]; }
    [[nodiscard]] size_t column_index(Index<database::Column> column) const { return database::column_index(span(), column); }
    [[nodiscard]] size_t row_size() const noexcept { return database::row_size(span()); }
    [[nodiscard]] size_t memory_usage() const noexcept { return m_data.values.allocated_size_ * sizeof(database::ColumnLayout); }

    template<ygg::ByteCodable T>
    void push_back(Index<database::Column> column);
    void push_back(Index<database::Column> column, ygg::TypeId type);
    void assign(std::span<const database::ColumnLayout> columns);
    void assign(std::initializer_list<database::ColumnLayout> columns);
    template<ygg::ByteCodable T = uint_t>
    void assign(std::span<const Index<database::Column>> columns);
    template<ygg::ByteCodable T = uint_t>
    void assign(std::initializer_list<Index<database::Column>> columns);
    void initialize(std::span<const database::ColumnLayout> columns) { assign(columns); }
    void initialize(std::initializer_list<database::ColumnLayout> columns) { assign(columns); }
    void initialize(std::span<const Index<database::Column>> columns) { assign(columns); }
    void initialize(std::initializer_list<Index<database::Column>> columns) { assign(columns); }
    /// Relabel without changing types or layout; validates before mutating.
    void rename(std::span<const Index<database::Column>> columns);
    void rename(std::initializer_list<Index<database::Column>> columns) { rename(std::span<const Index<database::Column>>(columns)); }
    void clear() noexcept { m_data.clear(); }

    template<ygg::ByteCodable T>
    [[nodiscard]] T get(std::span<const std::byte> bytes, Index<database::Column> column) const
    { return database::detail::read_column<T>(bytes, span(), column_index(column)); }
    template<ygg::ByteCodable T>
    void set(std::span<std::byte> bytes, Index<database::Column> column, T value) const
    { database::detail::write_column<T>(bytes, span(), column_index(column), value); }
    void validate_row(std::span<const std::byte> bytes) const { database::validate_row(bytes, span()); }

    static constexpr auto cista_members() noexcept { return ygg::Members<&Builder::m_data> {}; }
    static constexpr auto identifying_members() noexcept { return ygg::Members<&Builder::m_data> {}; }
};
}  // namespace ygg

namespace ygg::database
{
template<typename V>
concept ColumnsViewConcept = requires(const V& columns, size_t index, Index<Column> column) {
    { columns.get_index() } -> std::same_as<Index<Columns>>;
    { columns.span() } -> std::same_as<std::span<const ColumnLayout>>;
    { columns.data() } -> std::same_as<const ColumnLayout*>;
    { columns.size() } -> std::same_as<size_t>;
    { columns.empty() } -> std::same_as<bool>;
    { columns[index] } -> std::same_as<const ColumnLayout&>;
    { columns.column_index(column) } -> std::same_as<size_t>;
    { columns.row_size() } -> std::same_as<size_t>;
};
}  // namespace ygg::database

namespace ygg
{
/// Schema accessors shared by the index, data, and builder views of columns.
template<typename V>
class ViewMixin<database::Columns, V>
{
    [[nodiscard]] const Data<database::Columns>& record() const noexcept { return static_cast<const V&>(*this).get_record(); }

public:
    [[nodiscard]] std::span<const database::ColumnLayout> span() const noexcept { return { record().values.data(), record().values.size() }; }
    [[nodiscard]] auto begin() const noexcept { return span().begin(); }
    [[nodiscard]] auto end() const noexcept { return span().end(); }
    [[nodiscard]] const database::ColumnLayout* data() const noexcept { return span().data(); }
    [[nodiscard]] size_t size() const noexcept { return span().size(); }
    [[nodiscard]] bool empty() const noexcept { return span().empty(); }
    const database::ColumnLayout& operator[](size_t index) const noexcept { return span()[index]; }
    [[nodiscard]] size_t column_index(Index<database::Column> column) const { return database::column_index(span(), column); }
    [[nodiscard]] size_t row_size() const noexcept { return database::row_size(span()); }

    template<ygg::ByteCodable T>
    T get(std::span<const std::byte> bytes, Index<database::Column> column) const
    { return database::detail::read_column<T>(bytes, span(), column_index(column)); }
    template<ygg::ByteCodable T>
    void set(std::span<std::byte> bytes, Index<database::Column> column, T value) const
    { database::detail::write_column<T>(bytes, span(), column_index(column), value); }
    void validate_row(std::span<const std::byte> bytes) const { database::validate_row(bytes, span()); }
};
}  // namespace ygg

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

namespace detail
{
/// A relabeled schema keeps the arity and the column types.
void validate_relabel(std::span<const ColumnLayout> previous, std::span<const ColumnLayout> next);

/// Replaces the schema, retaining capacity; the columns may be a slice of the replaced schema.
void assign_columns(Data<Columns>& data, std::span<const ColumnLayout> columns);
}  // namespace detail

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
