/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SYNTAX_COLUMN_CODEC_HPP_
#define YGG_DATABASE_SYNTAX_COLUMN_CODEC_HPP_

#include "yggdrasil/core/bytes.hpp"
#include "yggdrasil/core/type_list.hpp"
#include "yggdrasil/core/types.hpp"
#include "yggdrasil/ids/index_coder.hpp"
#include "yggdrasil/ids/index_mixins.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <span>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

namespace ygg::database
{
/// Fixed-width, canonical in-memory encoding. Equal values must encode to identical
/// bytes, and decoding must preserve their value. No portable file format is implied.
/// Specialize explicitly for additional column types; object padding is never encoded.
template<typename T>
struct ColumnCodec;

template<typename T>
    requires(std::same_as<T, std::uint32_t> || std::same_as<T, std::int32_t> || std::same_as<T, std::uint64_t> || std::same_as<T, std::int64_t>)
struct ColumnCodec<T>
{
    static_assert(std::has_unique_object_representations_v<T>);
    static constexpr size_t size = sizeof(T);
    static void encode(T value, std::span<std::byte> bytes) { store_unaligned(bytes, value); }
    static T decode(std::span<const std::byte> bytes) { return load_unaligned<T>(bytes); }
};

template<typename T>
    requires(std::same_as<T, float> || std::same_as<T, double>)
struct ColumnCodec<T>
{
    static_assert(std::numeric_limits<T>::is_iec559 && (sizeof(T) == 4 || sizeof(T) == 8));
    static constexpr size_t size = sizeof(T);
    static void encode(T value, std::span<std::byte> bytes)
    {
        // Match ygg::EqualTo: both signed zeros and all NaN payloads are equal.
        if (std::isnan(value))
            value = std::numeric_limits<T>::quiet_NaN();
        else if (value == T { 0 })
            value = T { 0 };
        store_unaligned(bytes, value);
    }
    static T decode(std::span<const std::byte> bytes) { return load_unaligned<T>(bytes); }
};

template<>
struct ColumnCodec<bool>
{
    static constexpr size_t size = 1;
    static void encode(bool value, std::span<std::byte> bytes)
    {
        if (bytes.size() != size)
            throw std::invalid_argument("Column codec: incorrect byte width.");
        bytes[0] = value ? std::byte { 1 } : std::byte { 0 };
    }
    static bool decode(std::span<const std::byte> bytes)
    {
        if (bytes.size() != size || (bytes[0] != std::byte { 0 } && bytes[0] != std::byte { 1 }))
            throw std::invalid_argument("Column codec: invalid Boolean encoding.");
        return bytes[0] == std::byte { 1 };
    }
};

template<typename Tag>
    requires std::derived_from<Index<Tag>, IndexMixin<Index<Tag>>>
struct ColumnCodec<Index<Tag>>
{
    static constexpr size_t size = ColumnCodec<uint_t>::size;
    static void encode(Index<Tag> value, std::span<std::byte> bytes) { ColumnCodec<uint_t>::encode(IndexCoder<Index<Tag>>::encode(value), bytes); }
    static Index<Tag> decode(std::span<const std::byte> bytes) { return IndexCoder<Index<Tag>>::decode(ColumnCodec<uint_t>::decode(bytes)); }
};

template<typename T>
concept ColumnValue = requires(T value, std::span<std::byte> destination, std::span<const std::byte> source) {
    requires(ColumnCodec<T>::size > 0);
    { ColumnCodec<T>::encode(value, destination) } -> std::same_as<void>;
    { ColumnCodec<T>::decode(source) } -> std::same_as<T>;
};

namespace detail
{
template<typename... Ts>
struct UniqueColumnTypes : std::true_type
{
};

template<typename T, typename... Rest>
struct UniqueColumnTypes<T, Rest...> : std::bool_constant<(!std::same_as<T, Rest> && ...) && UniqueColumnTypes<Rest...>::value>
{
};

template<typename Values>
struct ValidColumnTypes : std::false_type
{
};

template<typename... Ts>
struct ValidColumnTypes<TypeList<Ts...>> : std::bool_constant<(sizeof...(Ts) > 0) && (ColumnValue<Ts> && ...) && UniqueColumnTypes<Ts...>::value>
{
};

template<typename Values>
struct ColumnTypeInfo;

template<typename First, typename... Rest>
struct ColumnTypeInfo<TypeList<First, Rest...>>
{
    static constexpr std::array<size_t, 1 + sizeof...(Rest)> sizes { ColumnCodec<First>::size, ColumnCodec<Rest>::size... };

    template<typename T>
    static consteval size_t index()
    {
        static_assert(TypeList<First, Rest...>::template contains<T>);
        constexpr std::array matches { std::same_as<T, First>, std::same_as<T, Rest>... };
        return static_cast<size_t>(std::ranges::find(matches, true) - matches.begin());
    }

    template<size_t Position = 0, typename Visitor>
        requires std::invocable<Visitor, std::type_identity<First>> && (std::invocable<Visitor, std::type_identity<Rest>> && ...)
    static std::invoke_result_t<Visitor, std::type_identity<First>> visit(size_t type, Visitor&& visitor)
    {
        static_assert((std::same_as<std::invoke_result_t<Visitor, std::type_identity<First>>, std::invoke_result_t<Visitor, std::type_identity<Rest>>> && ...),
                      "Column visitors must return the same type for every registered value type.");
        using T = std::tuple_element_t<Position, std::tuple<First, Rest...>>;
        if (type == Position)
            return std::invoke(std::forward<Visitor>(visitor), std::type_identity<T> {});
        if constexpr (Position < sizeof...(Rest))
            return visit<Position + 1>(type, std::forward<Visitor>(visitor));
        else
            throw std::invalid_argument("Columns: unregistered column type.");
    }
};
}  // namespace detail

template<typename Values>
concept ColumnTypes = detail::ValidColumnTypes<Values>::value;

/// A codec-supported value type registered in this column family.
template<typename T, typename Values>
concept ColumnValueFor = ColumnTypes<Values> && Values::template contains<T>;

using DefaultColumnTypes = TypeList<std::uint32_t, std::int32_t, std::uint64_t, std::int64_t, float, double, bool>;

template<ColumnTypes Values, ColumnValueFor<Values> T>
inline constexpr size_t column_type = detail::ColumnTypeInfo<Values>::template index<T>();

template<ColumnTypes Values>
size_t column_size(size_t type)
{
    if (type >= detail::ColumnTypeInfo<Values>::sizes.size())
        throw std::invalid_argument("Columns: unregistered column type.");
    return detail::ColumnTypeInfo<Values>::sizes[type];
}

/// Dispatches a schema type, not a stored variant. Visitors return a common result type.
template<ColumnTypes Values, typename Visitor>
    requires requires(Visitor&& visitor) { detail::ColumnTypeInfo<Values>::visit(size_t {}, std::forward<Visitor>(visitor)); }
decltype(auto) visit_column_type(size_t type, Visitor&& visitor)
{
    return detail::ColumnTypeInfo<Values>::visit(type, std::forward<Visitor>(visitor));
}
}  // namespace ygg::database

#endif
