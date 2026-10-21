/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_CORE_BYTE_CODEC_HPP_
#define YGG_CORE_BYTE_CODEC_HPP_

#include "yggdrasil/core/bytes.hpp"
#include "yggdrasil/core/type_id.hpp"

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

/// The value types of byte-encoded storage, one per TypeId.
template<typename T>
concept ByteCodable = std::same_as<T, std::uint32_t> || std::same_as<T, std::int32_t> || std::same_as<T, std::uint64_t> || std::same_as<T, std::int64_t>
                      || std::same_as<T, float> || std::same_as<T, double> || std::same_as<T, bool>;

template<ByteCodable T>
inline constexpr TypeId type_id = std::same_as<T, std::uint32_t> ? TypeId::uint32 :
                                  std::same_as<T, std::int32_t>  ? TypeId::int32 :
                                  std::same_as<T, std::uint64_t> ? TypeId::uint64 :
                                  std::same_as<T, std::int64_t>  ? TypeId::int64 :
                                  std::same_as<T, float>         ? TypeId::float32 :
                                  std::same_as<T, double>        ? TypeId::float64 :
                                                                   TypeId::boolean;

/// Fixed-width, canonical in-memory encoding: equal values encode to identical bytes and decoding preserves the value.
/// Not a portable file format.
template<ByteCodable T>
struct ByteCodec
{
    static_assert(!std::floating_point<T> || std::numeric_limits<T>::is_iec559);

    static constexpr std::size_t size = sizeof(T);

    static void encode(T value, std::span<std::byte> bytes)
    {
        if constexpr (std::same_as<T, bool>)
        {
            if (bytes.size() != size)
                throw std::invalid_argument("ByteCodec<bool>: incorrect byte width.");
            bytes[0] = value ? std::byte { 1 } : std::byte { 0 };
            return;
        }
        else
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
    }

    static T decode(std::span<const std::byte> bytes)
    {
        if constexpr (std::same_as<T, bool>)
        {
            if (bytes.size() != size || (bytes[0] != std::byte { 0 } && bytes[0] != std::byte { 1 }))
                throw std::invalid_argument("ByteCodec<bool>: invalid encoding.");
            return bytes[0] == std::byte { 1 };
        }
        else
            return load_unaligned<T>(bytes);
    }
};

/// Invocable with std::type_identity<T> for every byte-codable T.
template<typename Visitor>
concept TypeIdVisitor = std::invocable<Visitor&, std::type_identity<std::uint32_t>> && std::invocable<Visitor&, std::type_identity<std::int32_t>>
                        && std::invocable<Visitor&, std::type_identity<std::uint64_t>> && std::invocable<Visitor&, std::type_identity<std::int64_t>>
                        && std::invocable<Visitor&, std::type_identity<float>> && std::invocable<Visitor&, std::type_identity<double>>
                        && std::invocable<Visitor&, std::type_identity<bool>>;

/// Calls visitor with std::type_identity<T> for the type T of `id`; every call returns the same type.
template<TypeIdVisitor Visitor>
decltype(auto) visit_type_id(TypeId id, Visitor&& visitor)
{
    using Result = std::invoke_result_t<Visitor&, std::type_identity<std::uint32_t>>;
    const auto call = [&]<typename T>(std::type_identity<T> type) -> Result
    {
        static_assert(std::same_as<std::invoke_result_t<Visitor&, std::type_identity<T>>, Result>,
                      "visit_type_id: the visitor must return the same type for every type id.");
        return std::invoke(visitor, type);
    };
    switch (id)
    {
        case TypeId::uint32:
            return call(std::type_identity<std::uint32_t> {});
        case TypeId::int32:
            return call(std::type_identity<std::int32_t> {});
        case TypeId::uint64:
            return call(std::type_identity<std::uint64_t> {});
        case TypeId::int64:
            return call(std::type_identity<std::int64_t> {});
        case TypeId::float32:
            return call(std::type_identity<float> {});
        case TypeId::float64:
            return call(std::type_identity<double> {});
        case TypeId::boolean:
            return call(std::type_identity<bool> {});
    }
    throw std::invalid_argument("visit_type_id: invalid type id.");
}

/// The byte width of the type of `id`, or 0 if `id` names no type.
constexpr std::size_t byte_size(TypeId id) noexcept
{
    switch (id)
    {
        case TypeId::uint32:
            return ByteCodec<std::uint32_t>::size;
        case TypeId::int32:
            return ByteCodec<std::int32_t>::size;
        case TypeId::uint64:
            return ByteCodec<std::uint64_t>::size;
        case TypeId::int64:
            return ByteCodec<std::int64_t>::size;
        case TypeId::float32:
            return ByteCodec<float>::size;
        case TypeId::float64:
            return ByteCodec<double>::size;
        case TypeId::boolean:
            return ByteCodec<bool>::size;
    }
    return 0;
}

}  // namespace ygg

#endif
