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

#include <algorithm>
#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <limits>
#include <span>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace ygg::database
{

/// Identifies a column value type within a process. Layouts store it, so typed access needs no list of value types.
/// Like the encoding itself, it is no portable file format.
using ColumnTypeId = std::uint64_t;

namespace detail
{
/// FNV-1a over the compiler's spelling of T, for codecs whose type has no fixed id.
template<typename T>
consteval ColumnTypeId hash_type_name()
{
    constexpr std::string_view name = __PRETTY_FUNCTION__;
    auto hash = ColumnTypeId { 14695981039346656037ull };
    for (const auto c : name)
        hash = (hash ^ static_cast<unsigned char>(c)) * ColumnTypeId { 1099511628211ull };
    return hash;
}

template<typename T>
inline constexpr ColumnTypeId builtin_column_type_id = 0;
template<>
inline constexpr ColumnTypeId builtin_column_type_id<std::uint32_t> = 1;
template<>
inline constexpr ColumnTypeId builtin_column_type_id<std::int32_t> = 2;
template<>
inline constexpr ColumnTypeId builtin_column_type_id<std::uint64_t> = 3;
template<>
inline constexpr ColumnTypeId builtin_column_type_id<std::int64_t> = 4;
template<>
inline constexpr ColumnTypeId builtin_column_type_id<float> = 5;
template<>
inline constexpr ColumnTypeId builtin_column_type_id<double> = 6;
template<>
inline constexpr ColumnTypeId builtin_column_type_id<bool> = 7;
}  // namespace detail

/// Fixed-width, canonical in-memory encoding. Equal values must encode to identical
/// bytes, and decoding must preserve their value. No portable file format is implied.
/// Specialize explicitly for additional column types, with an id distinct from all other column types;
/// object padding is never encoded.
template<typename T>
struct ColumnCodec;

template<typename T>
    requires(std::same_as<T, std::uint32_t> || std::same_as<T, std::int32_t> || std::same_as<T, std::uint64_t> || std::same_as<T, std::int64_t>)
struct ColumnCodec<T>
{
    static_assert(std::has_unique_object_representations_v<T>);

    static constexpr ColumnTypeId id = detail::builtin_column_type_id<T>;
    static constexpr size_t size = sizeof(T);

    static void encode(T value, std::span<std::byte> bytes) { store_unaligned(bytes, value); }
    static T decode(std::span<const std::byte> bytes) { return load_unaligned<T>(bytes); }
};

template<typename T>
    requires(std::same_as<T, float> || std::same_as<T, double>)
struct ColumnCodec<T>
{
    static_assert(std::numeric_limits<T>::is_iec559 && (sizeof(T) == 4 || sizeof(T) == 8));

    static constexpr ColumnTypeId id = detail::builtin_column_type_id<T>;
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
    static constexpr ColumnTypeId id = detail::builtin_column_type_id<bool>;
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
    requires requires(Index<Tag> index) { { index.get_value() } -> std::same_as<uint_t>; }
struct ColumnCodec<Index<Tag>>
{
    static constexpr ColumnTypeId id = detail::hash_type_name<Index<Tag>>();
    static constexpr size_t size = ColumnCodec<uint_t>::size;

    static void encode(Index<Tag> value, std::span<std::byte> bytes) { ColumnCodec<uint_t>::encode(IndexCoder<Index<Tag>>::encode(value), bytes); }
    static Index<Tag> decode(std::span<const std::byte> bytes) { return IndexCoder<Index<Tag>>::decode(ColumnCodec<uint_t>::decode(bytes)); }
};

template<typename T>
concept ColumnValue = requires(T value, std::span<std::byte> destination, std::span<const std::byte> source) {
    { ColumnCodec<T>::id } -> std::convertible_to<ColumnTypeId>;
    requires(ColumnCodec<T>::size > 0);
    { ColumnCodec<T>::encode(value, destination) } -> std::same_as<void>;
    { ColumnCodec<T>::decode(source) } -> std::same_as<T>;
};

/// The id that layouts store for columns of type T.
template<ColumnValue T>
inline constexpr ColumnTypeId column_type = ColumnCodec<T>::id;

/// The byte width of a built-in column type, or 0 for other ids, whose width only their codec knows.
constexpr size_t builtin_column_size(ColumnTypeId type) noexcept
{
    switch (type)
    {
        case column_type<std::uint32_t>:
        case column_type<std::int32_t>:
        case column_type<float>:
            return 4;
        case column_type<std::uint64_t>:
        case column_type<std::int64_t>:
        case column_type<double>:
            return 8;
        case column_type<bool>:
            return 1;
        default:
            return 0;
    }
}

namespace detail
{
template<typename Values>
struct ValidColumnTypes : std::false_type
{
};

template<typename... Ts>
struct ValidColumnTypes<TypeList<Ts...>>
{
    static consteval bool distinct_ids()
    {
        constexpr std::array<ColumnTypeId, sizeof...(Ts)> ids { ColumnCodec<Ts>::id... };
        for (size_t i = 0; i < ids.size(); ++i)
            for (size_t j = i + 1; j < ids.size(); ++j)
                if (ids[i] == ids[j])
                    return false;
        return true;
    }

    static constexpr bool value = [] {
        if constexpr (sizeof...(Ts) > 0 && (ColumnValue<Ts> && ...))
            return distinct_ids();
        else
            return false;
    }();
};
}  // namespace detail

/// A list of value types with pairwise distinct ids, for turning a runtime column type back into its value type.
template<typename Values>
concept ColumnTypes = detail::ValidColumnTypes<Values>::value;

/// A codec-supported value type registered in this list.
template<typename T, typename Values>
concept ColumnValueFor = ColumnTypes<Values> && Values::template contains<T>;

using DefaultColumnTypes = TypeList<std::uint32_t, std::int32_t, std::uint64_t, std::int64_t, float, double, bool>;

namespace detail
{
template<typename Visitor, typename Values>
struct ColumnTypeVisitorTrait : std::false_type
{
};
template<typename Visitor, typename... Ts>
struct ColumnTypeVisitorTrait<Visitor, TypeList<Ts...>> : std::bool_constant<(std::invocable<Visitor, std::type_identity<Ts>> && ...)>
{
};
}  // namespace detail

/// Invocable with std::type_identity<T> for every T of Values.
template<typename Visitor, typename Values>
concept ColumnTypeVisitor = detail::ColumnTypeVisitorTrait<Visitor, Values>::value;

/// Calls visitor with std::type_identity<T> for the type T of Values whose id is type. Visitors return a common result type.
template<ColumnTypes Values, ColumnTypeVisitor<Values> Visitor>
decltype(auto) visit_column_type(ColumnTypeId type, Visitor&& visitor)
{
    return [&]<typename... Ts>(TypeList<Ts...>) -> decltype(auto) {
        using Result = std::invoke_result_t<Visitor, std::type_identity<std::tuple_element_t<0, std::tuple<Ts...>>>>;
        static_assert((std::same_as<Result, std::invoke_result_t<Visitor, std::type_identity<Ts>>> && ...),
                      "Column visitors must return the same type for every value type.");
        if constexpr (std::is_void_v<Result>)
        {
            const bool found = ((type == ColumnCodec<Ts>::id && (std::invoke(visitor, std::type_identity<Ts> {}), true)) || ...);
            if (!found)
                throw std::invalid_argument("Columns: column type not in the list of value types.");
        }
        else
        {
            auto result = std::optional<Result> {};
            const bool found = ((type == ColumnCodec<Ts>::id && (result.emplace(std::invoke(visitor, std::type_identity<Ts> {})), true)) || ...);
            if (!found)
                throw std::invalid_argument("Columns: column type not in the list of value types.");
            return Result(*std::move(result));
        }
    }(Values {});
}

}  // namespace ygg::database

#endif
