/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_CORE_BYTE_CODEC_HPP_
#define YGG_CORE_BYTE_CODEC_HPP_

#include "yggdrasil/core/bytes.hpp"
#include "yggdrasil/core/concepts.hpp"
#include "yggdrasil/core/type_id.hpp"
#include "yggdrasil/core/type_list.hpp"

#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <span>
#include <stdexcept>
#include <type_traits>

namespace ygg
{

/// Fixed-width, canonical in-memory encoding: equal values encode to identical bytes, decoding preserves the value,
/// and object padding is never encoded. Not a portable file format. Specializations provide `id` (distinct from every
/// other codec's), `size` (> 0), `encode` and `decode`.
template<typename T>
struct ByteCodec;

/// The types with built-in codecs. Their ids are their positions plus one, so reordering the list changes the ids.
using BuiltinByteTypes = TypeList<std::uint32_t, std::int32_t, std::uint64_t, std::int64_t, float, double, bool>;

template<typename T>
    requires(BuiltinByteTypes::contains<T> && !std::same_as<T, bool>)
struct ByteCodec<T>
{
    static_assert(std::floating_point<T> ? std::numeric_limits<T>::is_iec559 : std::has_unique_object_representations_v<T>);

    static constexpr TypeId id = BuiltinByteTypes::index_of<T> + 1;
    static constexpr std::size_t size = sizeof(T);

    static void encode(T value, std::span<std::byte> bytes)
    {
        // Match ygg::EqualTo: both signed zeros and all NaN payloads are equal.
        if constexpr (std::floating_point<T>)
        {
            if (std::isnan(value))
                value = std::numeric_limits<T>::quiet_NaN();
            else if (value == T { 0 })
                value = T { 0 };
        }
        store_unaligned(bytes, value);
    }
    static T decode(std::span<const std::byte> bytes) { return load_unaligned<T>(bytes); }
};

template<>
struct ByteCodec<bool>
{
    static constexpr TypeId id = BuiltinByteTypes::index_of<bool> + 1;
    static constexpr std::size_t size = 1;

    static void encode(bool value, std::span<std::byte> bytes)
    {
        if (bytes.size() != size)
            throw std::invalid_argument("ByteCodec<bool>: incorrect byte width.");
        bytes[0] = value ? std::byte { 1 } : std::byte { 0 };
    }
    static bool decode(std::span<const std::byte> bytes)
    {
        if (bytes.size() != size || (bytes[0] != std::byte { 0 } && bytes[0] != std::byte { 1 }))
            throw std::invalid_argument("ByteCodec<bool>: invalid encoding.");
        return bytes[0] == std::byte { 1 };
    }
};

template<typename T>
concept ByteCodable = requires(T value, std::span<std::byte> destination, std::span<const std::byte> source) {
    { ByteCodec<T>::id } -> std::convertible_to<TypeId>;
    requires(ByteCodec<T>::size > 0);
    { ByteCodec<T>::encode(value, destination) } -> std::same_as<void>;
    { ByteCodec<T>::decode(source) } -> std::same_as<T>;
};

template<ByteCodable T>
inline constexpr TypeId type_id = ByteCodec<T>::id;

/// The byte width of a built-in type id, or 0 for other ids, whose width only their codec knows.
constexpr std::size_t builtin_byte_size(TypeId id) noexcept
{
    return []<typename... Ts>(TypeId id, TypeList<Ts...>)
    {
        std::size_t size = 0;
        (void) (((id == ByteCodec<Ts>::id) && (size = ByteCodec<Ts>::size, true)) || ...);
        return size;
    }(id, BuiltinByteTypes {});
}

namespace detail
{
template<typename List>
inline constexpr bool distinct_byte_codable = false;

template<typename... Ts>
inline constexpr bool distinct_byte_codable<TypeList<Ts...>> = []
{
    if constexpr (sizeof...(Ts) == 0 || !(ByteCodable<Ts> && ...))
        return false;
    else
    {
        constexpr auto ids = std::array<TypeId, sizeof...(Ts)> { ByteCodec<Ts>::id... };
        for (std::size_t i = 0; i < ids.size(); ++i)
            for (std::size_t j = i + 1; j < ids.size(); ++j)
                if (ids[i] == ids[j])
                    return false;
        return true;
    }
}();

template<typename Visitor, typename T, typename... Ts>
decltype(auto) visit_type_id(TypeId id, Visitor& visitor)
{
    if (id == ByteCodec<T>::id)
        return std::invoke(visitor, std::type_identity<T> {});
    if constexpr (sizeof...(Ts) == 0)
        throw std::invalid_argument("visit_type_id: id not in the list of types.");
    else
        return visit_type_id<Visitor, Ts...>(id, visitor);
}
}  // namespace detail

/// A non-empty list of codable types with pairwise distinct ids, for turning a runtime id back into its type.
template<typename List>
concept ByteCodableTypes = detail::distinct_byte_codable<List>;

/// Calls visitor with std::type_identity<T> for the type T of List whose id is `id`; all calls return the same type.
template<ByteCodableTypes List, InvocableWithEach<MapTypeListT<std::type_identity, List>> Visitor>
decltype(auto) visit_type_id(TypeId id, Visitor&& visitor)
{
    return [&]<typename T, typename... Ts>(TypeList<T, Ts...>) -> decltype(auto)
    {
        using Result = std::invoke_result_t<Visitor&, std::type_identity<T>>;
        static_assert((std::same_as<Result, std::invoke_result_t<Visitor&, std::type_identity<Ts>>> && ...),
                      "visit_type_id: the visitor must return the same type for every type of the list.");
        return detail::visit_type_id<Visitor, T, Ts...>(id, visitor);
    }(List {});
}

}  // namespace ygg

#endif
