/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SYNTAX_COLUMNS_HPP_
#define YGG_DATABASE_SYNTAX_COLUMNS_HPP_

#include "yggdrasil/core/config.hpp"
#include "yggdrasil/database/declarations.hpp"
#include <urd/repr/sequences/sequence.hpp>
#include <urd/store.hpp>
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
size_t column_index(std::span<const ColumnLayout> columns, urd::index<Column> column);
bool contains_column(std::span<const ColumnLayout> columns, urd::index<Column> column) noexcept;
/// The labels in schema order.
urd::Store<urd::sequence<urd::index<Column>>> column_labels(std::span<const ColumnLayout> columns);
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

namespace urd
{
/// Owns a validated typed schema. Changing it clears its canonical index and
/// retains column storage. Row byte width is independent of logical arity.
template<>
struct builder<ygg::database::Columns>
{
private:
    urd::data<ygg::database::Columns> m_data;

public:
    builder() = default;
    explicit builder(std::span<const ygg::database::ColumnLayout> columns);
    builder(std::initializer_list<ygg::database::ColumnLayout> columns);
    /// Labels alone give every column type ygg::uint_t; use assign<T> for another type.
    explicit builder(std::span<const urd::index<ygg::database::Column>> columns);
    builder(std::initializer_list<urd::index<ygg::database::Column>> columns);

    auto& get_data() noexcept { return m_data; }
    [[nodiscard]] const auto& get_data() const noexcept { return m_data; }
    [[nodiscard]] auto index() const noexcept { return m_data.index; }
    void set_index(urd::index<ygg::database::Columns> index) noexcept { m_data.index = index; }

    [[nodiscard]] std::span<const ygg::database::ColumnLayout> span() const& noexcept { return { m_data.values.data(), m_data.values.size() }; }
    [[nodiscard]] std::span<const ygg::database::ColumnLayout> span() const&& = delete;
    [[nodiscard]] auto begin() const noexcept { return m_data.values.begin(); }
    [[nodiscard]] auto end() const noexcept { return m_data.values.end(); }
    [[nodiscard]] const ygg::database::ColumnLayout* data() const noexcept { return m_data.values.data(); }
    [[nodiscard]] size_t size() const noexcept { return m_data.values.size(); }
    [[nodiscard]] bool empty() const noexcept { return m_data.values.empty(); }
    const ygg::database::ColumnLayout& operator[](size_t index) const noexcept { return m_data.values[index]; }
    [[nodiscard]] size_t column_index(urd::index<ygg::database::Column> column) const { return ygg::database::column_index(span(), column); }
    [[nodiscard]] size_t row_size() const noexcept { return ygg::database::row_size(span()); }
    [[nodiscard]] size_t memory_usage() const noexcept { return m_data.values.allocated_size_ * sizeof(ygg::database::ColumnLayout); }

    template<ygg::ByteCodable T>
    void push_back(urd::index<ygg::database::Column> column);
    void push_back(urd::index<ygg::database::Column> column, ygg::TypeId type);
    void assign(std::span<const ygg::database::ColumnLayout> columns);
    void assign(std::initializer_list<ygg::database::ColumnLayout> columns);
    template<ygg::ByteCodable T = ygg::uint_t>
    void assign(std::span<const urd::index<ygg::database::Column>> columns);
    template<ygg::ByteCodable T = ygg::uint_t>
    void assign(std::initializer_list<urd::index<ygg::database::Column>> columns);
    void initialize(std::span<const ygg::database::ColumnLayout> columns) { assign(columns); }
    void initialize(std::initializer_list<ygg::database::ColumnLayout> columns) { assign(columns); }
    void initialize(std::span<const urd::index<ygg::database::Column>> columns) { assign(columns); }
    void initialize(std::initializer_list<urd::index<ygg::database::Column>> columns) { assign(columns); }
    /// Relabel without changing types or layout; validates before mutating.
    void rename(std::span<const urd::index<ygg::database::Column>> columns);
    void rename(std::initializer_list<urd::index<ygg::database::Column>> columns) { rename(std::span<const urd::index<ygg::database::Column>>(columns)); }
    void clear() noexcept { urd::clear_members(m_data); }

    template<ygg::ByteCodable T>
    [[nodiscard]] T read(std::span<const std::byte> bytes, urd::index<ygg::database::Column> column) const
    { return ygg::database::detail::read_column<T>(bytes, span(), column_index(column)); }
    template<ygg::ByteCodable T>
    void write(std::span<std::byte> bytes, urd::index<ygg::database::Column> column, T value) const
    { ygg::database::detail::write_column<T>(bytes, span(), column_index(column), value); }
    void validate_row(std::span<const std::byte> bytes) const { ygg::database::validate_row(bytes, span()); }

    static constexpr auto cista_members() noexcept { return urd::Members<&builder::m_data> {}; }
    static constexpr auto identifying_members() noexcept { return urd::Members<&builder::m_data> {}; }
};
}  // namespace urd

namespace ygg::database
{
template<typename V>
concept ColumnsViewConcept = requires(const V& columns, size_t index, urd::index<Column> column) {
    { columns.index() } -> std::same_as<urd::index<Columns>>;
    { columns.span() } -> std::same_as<std::span<const ColumnLayout>>;
    { columns.size() } -> std::same_as<size_t>;
    { columns.empty() } -> std::same_as<bool>;
    { columns[index] } -> std::same_as<const ColumnLayout&>;
    { columns.column_index(column) } -> std::same_as<size_t>;
    { columns.row_size() } -> std::same_as<size_t>;
};
}  // namespace ygg::database

/// Schema accessors shared by the index, data, and builder views of columns (a builder view reads the builder's record).
template<typename V>
struct urd::view_mixin<ygg::database::Columns, V>
{
private:
    [[nodiscard]] const urd::data<ygg::database::Columns>& record() const noexcept
    {
        const auto& self = static_cast<const V&>(*this);
        if constexpr (requires { self.data().get_data(); })
            return self.data().get_data();
        else
            return self.data();
    }

public:
    /// The record's own index (an index view's index() hides it, the same).
    [[nodiscard]] urd::index<ygg::database::Columns> index() const noexcept { return record().index; }
    [[nodiscard]] std::span<const ygg::database::ColumnLayout> span() const noexcept { return { record().values.data(), record().values.size() }; }
    [[nodiscard]] auto begin() const noexcept { return span().begin(); }
    [[nodiscard]] auto end() const noexcept { return span().end(); }
    [[nodiscard]] size_t size() const noexcept { return span().size(); }
    [[nodiscard]] bool empty() const noexcept { return span().empty(); }
    const ygg::database::ColumnLayout& operator[](size_t index) const noexcept { return span()[index]; }
    [[nodiscard]] size_t column_index(urd::index<ygg::database::Column> column) const { return ygg::database::column_index(span(), column); }
    [[nodiscard]] size_t row_size() const noexcept { return ygg::database::row_size(span()); }

    template<ygg::ByteCodable T>
    T read(std::span<const std::byte> bytes, urd::index<ygg::database::Column> column) const
    { return ygg::database::detail::read_column<T>(bytes, span(), column_index(column)); }
    template<ygg::ByteCodable T>
    void write(std::span<std::byte> bytes, urd::index<ygg::database::Column> column, T value) const
    { ygg::database::detail::write_column<T>(bytes, span(), column_index(column), value); }
    void validate_row(std::span<const std::byte> bytes) const { ygg::database::validate_row(bytes, span()); }
};
namespace ygg::database
{
namespace detail
{
void validate_column_labels(std::span<const urd::index<Column>> columns);

/// Validates a schema given by labels alone, whose columns all have type T, and returns its row width.
template<ByteCodable T = uint_t>
size_t label_row_size(std::span<const urd::index<Column>> labels)
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
void assign_columns(urd::data<Columns>& data, std::span<const ColumnLayout> columns);
}  // namespace detail

inline size_t row_size(std::span<const ColumnLayout> columns) noexcept { return columns.empty() ? 0 : columns.back().offset + columns.back().size; }
}  // namespace ygg::database

namespace urd
{

template<ygg::ByteCodable T>
void urd::builder<ygg::database::Columns>::push_back(urd::index<ygg::database::Column> column)
{ push_back(column, ygg::type_id<T>); }

template<ygg::ByteCodable T>
void urd::builder<ygg::database::Columns>::assign(std::span<const urd::index<ygg::database::Column>> columns)
{
    ygg::database::detail::label_row_size<T>(columns);
    constexpr auto width = ygg::ByteCodec<T>::size;
    m_data.values.resize(columns.size());
    for (size_t i = 0; i < columns.size(); ++i)
        m_data.values[i] = ygg::database::ColumnLayout { columns[i], ygg::type_id<T>, i * width, width };
    m_data.index = {};
}

template<ygg::ByteCodable T>
void urd::builder<ygg::database::Columns>::assign(std::initializer_list<urd::index<ygg::database::Column>> columns)
{ assign<T>(std::span<const urd::index<ygg::database::Column>>(columns)); }

}  // namespace urd

namespace ygg::database
{
template<ColumnsViewConcept V>
urd::data<Columns>& assign(urd::data<Columns>& data, const V& source)
{
    detail::assign_columns(data, source.span());
    return data;
}

template<ColumnsViewConcept V>
urd::builder<Columns>& assign(urd::builder<Columns>& builder, const V& source)
{
    builder.assign(source.span());
    return builder;
}
}  // namespace ygg::database

#endif
