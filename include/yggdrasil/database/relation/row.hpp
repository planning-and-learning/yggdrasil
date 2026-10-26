/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_RELATION_ROW_HPP_
#define YGG_DATABASE_RELATION_ROW_HPP_

#include "yggdrasil/core/concepts.hpp"
#include "yggdrasil/core/type_list.hpp"
#include "yggdrasil/database/declarations.hpp"
#include "yggdrasil/database/relation/columns.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <functional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

namespace ygg::database
{
/// Borrows canonical bytes and a validated schema. Both must outlive this row.
/// Construction is constant-time; public byte insertion validates encodings.
class Row
{
    std::span<const std::byte> m_bytes;
    std::span<const ColumnLayout> m_columns;

public:
    Row(std::span<const std::byte> bytes, std::span<const ColumnLayout> columns) noexcept : m_bytes(bytes), m_columns(columns)
    { assert(bytes.size() == row_size(columns)); }

    [[nodiscard]] std::span<const std::byte> bytes() const noexcept { return m_bytes; }
    [[nodiscard]] std::span<const ColumnLayout> columns() const noexcept { return m_columns; }
    [[nodiscard]] size_t size() const noexcept { return m_columns.size(); }
    [[nodiscard]] bool empty() const noexcept { return m_columns.empty(); }

    template<ByteCodable T>
    [[nodiscard]] T get(size_t position) const
    { return detail::read_column<T>(m_bytes, m_columns, position); }
    /// Decodes every column as T into the reusable buffer, which the result borrows.
    template<ByteCodable T>
    std::span<const T> get_all(std::vector<T>& buffer) const
    {
        buffer.clear();
        for (size_t position = 0; position < size(); ++position)
            buffer.push_back(get<T>(position));
        return buffer;
    }
    template<ByteCodable T>
    [[nodiscard]] T get(Index<Column> column) const
    { return get<T>(column_index(m_columns, column)); }

    /// Calls visitor with the value at `position`, decoded as its column's type.
    template<typename Visitor>
    auto visit(size_t position, Visitor&& visitor) const
    {
        if (position >= size())
            throw std::out_of_range("Row: column position is out of range.");
        return visit_type_id(m_columns[position].type,
                             [&]<typename T>(std::type_identity<T>) { return std::invoke(std::forward<Visitor>(visitor), get<T>(position)); });
    }
};

/// Encode a row whose columns all hold T into the reusable buffer, which the result borrows.
template<ByteCodable T, std::ranges::sized_range R>
    requires std::convertible_to<std::ranges::range_reference_t<const R&>, T>
std::span<const std::byte> encode_row(const R& values, std::vector<std::byte>& buffer)
{
    buffer.resize(std::ranges::size(values) * ByteCodec<T>::size);
    auto field = std::span<std::byte>(buffer);
    for (const auto& value : values)
    {
        ByteCodec<T>::encode(T(value), field.first(ByteCodec<T>::size));
        field = field.subspan(ByteCodec<T>::size);
    }
    return buffer;
}

/// Encode a complete typed row using an already validated schema. The fixed-size
/// result lives on the stack; its arity and each value type must match the schema.
template<ByteCodable... Ts>
std::array<std::byte, (ByteCodec<Ts>::size + ... + size_t { 0 })> encode_row(const std::tuple<Ts...>& values, std::span<const ColumnLayout> columns)
{
    if (columns.size() != sizeof...(Ts))
        throw std::invalid_argument("Relation: row arity does not match its schema.");
    std::array<std::byte, (ByteCodec<Ts>::size + ... + size_t { 0 })> bytes {};
    size_t position = 0;
    std::apply([&](const Ts&... value) { (detail::write_column<Ts>(bytes, columns, position++, value), ...); }, values);
    return bytes;
}

}  // namespace ygg::database

namespace ygg
{
/// A row with its context. Index values stay raw: a relation's values may be owned by any repository, so callers
/// resolve them in their owner. The row descriptor itself is retained by value.
template<typename C>
class View<database::Row, C>
{
    database::Row m_handle;
    const C* m_context;

public:
    View(database::Row handle, const C& context) noexcept : m_handle(handle), m_context(&context) {}
    [[nodiscard]] const auto& get_handle() const noexcept { return m_handle; }
    [[nodiscard]] const auto& get_data() const noexcept { return m_handle; }
    [[nodiscard]] const C& get_context() const noexcept { return *m_context; }
    [[nodiscard]] std::span<const std::byte> bytes() const noexcept { return m_handle.bytes(); }
    [[nodiscard]] std::span<const database::ColumnLayout> columns() const noexcept { return m_handle.columns(); }
    [[nodiscard]] size_t size() const noexcept { return m_handle.size(); }
    [[nodiscard]] bool empty() const noexcept { return m_handle.empty(); }

    template<ygg::ByteCodable T>
    [[nodiscard]] T get(size_t position) const
    { return m_handle.template get<T>(position); }
    template<ygg::ByteCodable T>
    [[nodiscard]] T get(Index<database::Column> column) const
    { return get<T>(database::column_index(columns(), column)); }

    template<typename Visitor>
    auto visit(size_t position, Visitor&& visitor) const
    { return m_handle.visit(position, std::forward<Visitor>(visitor)); }
};
}  // namespace ygg

#endif
