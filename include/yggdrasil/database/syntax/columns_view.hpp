/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SYNTAX_COLUMNS_VIEW_HPP_
#define YGG_DATABASE_SYNTAX_COLUMNS_VIEW_HPP_

#include "yggdrasil/database/syntax/columns_builder.hpp"

#include <concepts>
#include <cstddef>
#include <span>
#include <tuple>
#include <type_traits>

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
    const Data<database::Columns>& record() const noexcept { return static_cast<const V&>(*this).get_record(); }

public:
    std::span<const database::ColumnLayout> span() const noexcept { return { record().values.data(), record().values.size() }; }
    auto begin() const noexcept { return span().begin(); }
    auto end() const noexcept { return span().end(); }
    const database::ColumnLayout* data() const noexcept { return span().data(); }
    size_t size() const noexcept { return span().size(); }
    bool empty() const noexcept { return span().empty(); }
    const database::ColumnLayout& operator[](size_t index) const noexcept { return span()[index]; }
    size_t column_index(Index<database::Column> column) const { return database::column_index(span(), column); }
    size_t row_size() const noexcept { return database::row_size(span()); }

    template<ygg::ByteCodable T>
    T get(std::span<const std::byte> bytes, Index<database::Column> column) const
    {
        return database::detail::read_column<T>(bytes, span(), column_index(column));
    }
    template<ygg::ByteCodable T>
    void set(std::span<std::byte> bytes, Index<database::Column> column, T value) const
    {
        database::detail::write_column<T>(bytes, span(), column_index(column), value);
    }
    void validate_row(std::span<const std::byte> bytes) const { database::validate_row(bytes, span()); }
};
}  // namespace ygg

#include "yggdrasil/database/syntax/details/columns.hpp"

#endif
