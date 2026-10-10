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

namespace ygg::database
{
template<typename V, typename Values>
concept ColumnsViewConcept = ColumnTypes<Values> && requires(const V& columns, size_t index, Index<Column> column) {
    { columns.get_index() } -> std::same_as<Index<Columns<Values>>>;
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
template<database::ColumnTypes Values, typename C>
class View<Builder<database::Columns<Values>>, C>
{
    const Builder<database::Columns<Values>>* m_handle;
    const C* m_context;

public:
    View(const Builder<database::Columns<Values>>& handle, const C& context) noexcept : m_handle(&handle), m_context(&context) {}
    const auto& get_handle() const noexcept { return *m_handle; }
    const auto& get_data() const noexcept { return m_handle->get_data(); }
    const auto& get_context() const noexcept { return *m_context; }
    auto get_index() const noexcept { return get_data().index; }
    std::span<const database::ColumnLayout> span() const noexcept { return { get_data().values.data(), get_data().values.size() }; }
    auto begin() const noexcept { return span().begin(); }
    auto end() const noexcept { return span().end(); }
    const database::ColumnLayout* data() const noexcept { return span().data(); }
    size_t size() const noexcept { return span().size(); }
    bool empty() const noexcept { return span().empty(); }
    const database::ColumnLayout& operator[](size_t index) const noexcept { return span()[index]; }
    size_t column_index(Index<database::Column> column) const { return database::column_index(span(), column); }
    size_t row_size() const noexcept { return database::row_size(span()); }

    template<database::ColumnValueFor<Values> T>
    T get(std::span<const std::byte> bytes, Index<database::Column> column) const
    {
        return database::detail::read_column<Values, T>(bytes, span(), column_index(column));
    }
    template<database::ColumnValueFor<Values> T>
    void set(std::span<std::byte> bytes, Index<database::Column> column, T value) const
    {
        database::detail::write_column<Values>(bytes, span(), column_index(column), value);
    }
    void validate_row(std::span<const std::byte> bytes) const { database::validate_row<Values>(bytes, span()); }
};

template<database::ColumnTypes Values, typename C>
class View<Data<database::Columns<Values>>, C>
{
    const Data<database::Columns<Values>>* m_handle;
    const C* m_context;

public:
    View(const Data<database::Columns<Values>>& handle, const C& context) noexcept : m_handle(&handle), m_context(&context) {}
    const auto& get_handle() const noexcept { return *m_handle; }
    const auto& get_data() const noexcept { return *m_handle; }
    const auto& get_context() const noexcept { return *m_context; }
    auto get_index() const noexcept { return get_data().index; }
    std::span<const database::ColumnLayout> span() const noexcept { return { get_data().values.data(), get_data().values.size() }; }
    auto begin() const noexcept { return span().begin(); }
    auto end() const noexcept { return span().end(); }
    const database::ColumnLayout* data() const noexcept { return span().data(); }
    size_t size() const noexcept { return span().size(); }
    bool empty() const noexcept { return span().empty(); }
    const database::ColumnLayout& operator[](size_t index) const noexcept { return span()[index]; }
    size_t column_index(Index<database::Column> column) const { return database::column_index(span(), column); }
    size_t row_size() const noexcept { return database::row_size(span()); }

    template<database::ColumnValueFor<Values> T>
    T get(std::span<const std::byte> bytes, Index<database::Column> column) const
    {
        return database::detail::read_column<Values, T>(bytes, span(), column_index(column));
    }
    template<database::ColumnValueFor<Values> T>
    void set(std::span<std::byte> bytes, Index<database::Column> column, T value) const
    {
        database::detail::write_column<Values>(bytes, span(), column_index(column), value);
    }
    void validate_row(std::span<const std::byte> bytes) const { database::validate_row<Values>(bytes, span()); }
};

template<database::ColumnTypes Values, typename C>
class View<Index<database::Columns<Values>>, C>
{
    Index<database::Columns<Values>> m_handle;
    const C* m_context;

public:
    View(Index<database::Columns<Values>> handle, const C& context) noexcept : m_handle(handle), m_context(&context) {}
    const auto& get_handle() const noexcept { return m_handle; }
    const auto& get_data() const noexcept { return get_columns_repository(*m_context)[m_handle]; }
    const auto& get_context() const noexcept { return *m_context; }
    auto get_index() const noexcept { return get_data().index; }
    std::span<const database::ColumnLayout> span() const noexcept { return { get_data().values.data(), get_data().values.size() }; }
    auto begin() const noexcept { return span().begin(); }
    auto end() const noexcept { return span().end(); }
    const database::ColumnLayout* data() const noexcept { return span().data(); }
    size_t size() const noexcept { return span().size(); }
    bool empty() const noexcept { return span().empty(); }
    const database::ColumnLayout& operator[](size_t index) const noexcept { return span()[index]; }
    size_t column_index(Index<database::Column> column) const { return database::column_index(span(), column); }
    size_t row_size() const noexcept { return database::row_size(span()); }

    template<database::ColumnValueFor<Values> T>
    T get(std::span<const std::byte> bytes, Index<database::Column> column) const
    {
        return database::detail::read_column<Values, T>(bytes, span(), column_index(column));
    }
    template<database::ColumnValueFor<Values> T>
    void set(std::span<std::byte> bytes, Index<database::Column> column, T value) const
    {
        database::detail::write_column<Values>(bytes, span(), column_index(column), value);
    }
    void validate_row(std::span<const std::byte> bytes) const { database::validate_row<Values>(bytes, span()); }
    auto identifying_members() const noexcept { return std::make_tuple(m_handle, get_columns_repository(*m_context).get_index()); }
};

}  // namespace ygg

#include "yggdrasil/database/syntax/details/columns.hpp"

#endif
