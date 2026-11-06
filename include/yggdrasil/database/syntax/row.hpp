/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SYNTAX_ROW_HPP_
#define YGG_DATABASE_SYNTAX_ROW_HPP_

#include <urd/repr/sequences/sequence.hpp>
#include <urd/store.hpp>
#include <boost/mp11.hpp>
#include "yggdrasil/database/declarations.hpp"
#include "yggdrasil/database/syntax/columns.hpp"

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
    /// Decodes every column as the buffer's elements into the reusable buffer (any contiguous sequence that clears and
    /// appends), which the result borrows.
    template<typename Buffer, typename T = std::ranges::range_value_t<Buffer>>
        requires ByteCodable<T> && std::ranges::contiguous_range<Buffer> && requires(Buffer& buffer, T value) {
            buffer.clear();
            buffer.push_back(value);
        }
    std::span<const T> get_all(Buffer& buffer) const
    {
        buffer.clear();
        for (size_t position = 0; position < size(); ++position)
            buffer.push_back(get<T>(position));
        return buffer;
    }
    template<ByteCodable T>
    [[nodiscard]] T get(urd::index<Column> column) const
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

/// Encode a row whose columns all hold T into the reusable buffer (any resizable, contiguous sequence of bytes), which
/// the result borrows.
template<ByteCodable T, std::ranges::sized_range R, typename Buffer>
    requires std::convertible_to<std::ranges::range_reference_t<const R&>, T> && std::ranges::contiguous_range<Buffer>
             && std::same_as<std::ranges::range_value_t<Buffer>, std::byte> && requires(Buffer& buffer, std::size_t size) { buffer.resize(size); }
std::span<const std::byte> encode_row(const R& values, Buffer& buffer)
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

#endif
