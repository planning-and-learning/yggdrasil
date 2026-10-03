/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_COLUMNS_VIEW_HPP_
#define YGG_DATABASE_COLUMNS_VIEW_HPP_

#include "yggdrasil/database/columns_builder.hpp"

#include <concepts>
#include <cstddef>
#include <span>
#include <tuple>

namespace ygg::database
{
/// Reject duplicate labels before storing or publishing a schema.
void validate_columns(std::span<const Index<Column>> columns);
size_t column_index(std::span<const Index<Column>> columns, Index<Column> column);

template<typename V>
concept ColumnsViewConcept = requires(const V& columns, size_t index, Index<Column> column) {
    { columns.span() } -> std::same_as<std::span<const Index<Column>>>;
    { columns.data() } -> std::same_as<const Index<Column>*>;
    { columns.size() } -> std::same_as<size_t>;
    { columns.empty() } -> std::same_as<bool>;
    { columns[index] } -> std::same_as<Index<Column>>;
    { columns.column_index(column) } -> std::same_as<size_t>;
};
}  // namespace ygg::database

namespace ygg
{
template<typename C>
class View<Builder<database::Columns>, C>
{
    const Builder<database::Columns>* m_handle;
    const C* m_context;

public:
    View(const Builder<database::Columns>& handle, const C& context) noexcept : m_handle(&handle), m_context(&context) {}
    const auto& get_handle() const noexcept { return *m_handle; }
    const auto& get_data() const noexcept { return m_handle->get_data(); }
    const auto& get_context() const noexcept { return *m_context; }
    auto get_index() const noexcept { return get_data().index; }
    std::span<const Index<database::Column>> span() const noexcept { return { get_data().values.data(), get_data().values.size() }; }
    auto begin() const noexcept { return span().begin(); }
    auto end() const noexcept { return span().end(); }
    const Index<database::Column>* data() const noexcept { return span().data(); }
    size_t size() const noexcept { return span().size(); }
    bool empty() const noexcept { return span().empty(); }
    Index<database::Column> operator[](size_t index) const noexcept { return span()[index]; }
    size_t column_index(Index<database::Column> column) const { return database::column_index(span(), column); }
};

template<typename C>
class View<Data<database::Columns>, C>
{
    const Data<database::Columns>* m_handle;
    const C* m_context;

public:
    View(const Data<database::Columns>& handle, const C& context) noexcept : m_handle(&handle), m_context(&context) {}
    const auto& get_handle() const noexcept { return *m_handle; }
    const auto& get_data() const noexcept { return *m_handle; }
    const auto& get_context() const noexcept { return *m_context; }
    auto get_index() const noexcept { return get_data().index; }
    std::span<const Index<database::Column>> span() const noexcept { return { get_data().values.data(), get_data().values.size() }; }
    auto begin() const noexcept { return span().begin(); }
    auto end() const noexcept { return span().end(); }
    const Index<database::Column>* data() const noexcept { return span().data(); }
    size_t size() const noexcept { return span().size(); }
    bool empty() const noexcept { return span().empty(); }
    Index<database::Column> operator[](size_t index) const noexcept { return span()[index]; }
    size_t column_index(Index<database::Column> column) const { return database::column_index(span(), column); }
};

template<typename C>
class View<Index<database::Columns>, C>
{
    Index<database::Columns> m_handle;
    const C* m_context;

public:
    View(Index<database::Columns> handle, const C& context) noexcept : m_handle(handle), m_context(&context) {}
    const auto& get_handle() const noexcept { return m_handle; }
    const auto& get_data() const noexcept { return get_columns_repository(*m_context)[m_handle]; }
    const auto& get_context() const noexcept { return *m_context; }
    auto get_index() const noexcept { return m_handle; }
    std::span<const Index<database::Column>> span() const noexcept { return { get_data().values.data(), get_data().values.size() }; }
    auto begin() const noexcept { return span().begin(); }
    auto end() const noexcept { return span().end(); }
    const Index<database::Column>* data() const noexcept { return span().data(); }
    size_t size() const noexcept { return span().size(); }
    bool empty() const noexcept { return span().empty(); }
    Index<database::Column> operator[](size_t index) const noexcept { return span()[index]; }
    size_t column_index(Index<database::Column> column) const { return database::column_index(span(), column); }
    auto identifying_members() const noexcept { return std::make_tuple(m_handle, get_columns_repository(*m_context).get_index()); }
};

}  // namespace ygg

#include "yggdrasil/database/details/columns.hpp"

#endif
