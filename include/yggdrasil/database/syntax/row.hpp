/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SYNTAX_ROW_HPP_
#define YGG_DATABASE_SYNTAX_ROW_HPP_

#include "yggdrasil/database/syntax/columns.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <functional>
#include <span>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace ygg::database
{
/// Borrows canonical bytes and a validated schema. Both must outlive this row.
/// Construction is constant-time; public byte insertion validates encodings.
template<ColumnTypes Values>
class Row
{
    std::span<const std::byte> m_bytes;
    std::span<const ColumnLayout> m_columns;

public:
    Row(std::span<const std::byte> bytes, std::span<const ColumnLayout> columns) noexcept : m_bytes(bytes), m_columns(columns)
    {
        assert(bytes.size() == row_size(columns));
    }

    std::span<const std::byte> bytes() const noexcept { return m_bytes; }
    std::span<const ColumnLayout> columns() const noexcept { return m_columns; }
    size_t size() const noexcept { return m_columns.size(); }
    bool empty() const noexcept { return m_columns.empty(); }

    template<ColumnValueFor<Values> T>
    T get(size_t position) const
    {
        return detail::read_column<Values, T>(m_bytes, m_columns, position);
    }
    template<ColumnValueFor<Values> T>
    T get(Index<Column> column) const
    {
        return get<T>(column_index(m_columns, column));
    }

    template<typename Visitor>
    decltype(auto) visit(size_t position, Visitor&& visitor) const
    {
        if (position >= size())
            throw std::out_of_range("Row: column position is out of range.");
        return visit_column_type<Values>(m_columns[position].type,
                                         [&]<typename T>(std::type_identity<T>) -> decltype(auto)
                                         { return std::invoke(std::forward<Visitor>(visitor), get<T>(position)); });
    }
};

/// Encode a complete typed row using an already validated schema. The fixed-size
/// result lives on the stack; its arity and each value type must match the schema.
template<ColumnTypes Values, ColumnValueFor<Values>... Ts>
std::array<std::byte, (ColumnCodec<Ts>::size + ... + size_t { 0 })> encode_row(const std::tuple<Ts...>& values, std::span<const ColumnLayout> columns)
{
    if (columns.size() != sizeof...(Ts))
        throw std::invalid_argument("Relation: row arity does not match its schema.");
    std::array<std::byte, (ColumnCodec<Ts>::size + ... + size_t { 0 })> bytes {};
    size_t position = 0;
    std::apply([&](const Ts&... value) { (detail::write_column<Values>(bytes, columns, position++, value), ...); }, values);
    return bytes;
}

namespace detail
{
template<ColumnValue T, typename C>
T resolve_column(T value, const C&)
{
    return value;
}

template<typename Tag, typename C>
    requires ColumnValue<Index<Tag>>
auto resolve_column(Index<Tag> value, const C& context)
{
    if constexpr (ViewConcept<Index<Tag>, C>)
        return make_view(value, context);
    else
        return value;
}
}  // namespace detail
}  // namespace ygg::database

namespace ygg
{
/// Contextual row access resolves index values through the borrowed context;
/// scalar values remain values. The row descriptor itself is retained by value.
template<database::ColumnTypes Values, typename C>
class View<database::Row<Values>, C>
{
    database::Row<Values> m_handle;
    const C* m_context;

public:
    View(database::Row<Values> handle, const C& context) noexcept : m_handle(handle), m_context(&context) {}
    const auto& get_handle() const noexcept { return m_handle; }
    const auto& get_data() const noexcept { return m_handle; }
    const C& get_context() const noexcept { return *m_context; }
    std::span<const std::byte> bytes() const noexcept { return m_handle.bytes(); }
    std::span<const database::ColumnLayout> columns() const noexcept { return m_handle.columns(); }
    size_t size() const noexcept { return m_handle.size(); }
    bool empty() const noexcept { return m_handle.empty(); }

    template<database::ColumnValueFor<Values> T>
    auto get(size_t position) const
    {
        return database::detail::resolve_column(m_handle.template get<T>(position), *m_context);
    }
    template<database::ColumnValueFor<Values> T>
    auto get(Index<database::Column> column) const
    {
        return get<T>(database::column_index(columns(), column));
    }

    template<typename Visitor>
    decltype(auto) visit(size_t position, Visitor&& visitor) const
    {
        return m_handle.visit(position,
                              [&](auto value) -> decltype(auto)
                              { return std::invoke(std::forward<Visitor>(visitor), database::detail::resolve_column(value, *m_context)); });
    }
};
}  // namespace ygg

#endif
