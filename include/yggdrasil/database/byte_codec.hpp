/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_BYTE_CODEC_HPP_
#define YGG_DATABASE_BYTE_CODEC_HPP_

#include <boost/mp11.hpp>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <span>
#include <stdexcept>
#include <type_traits>

namespace ygg
{

/// The value types of byte-encoded storage. Values are fixed and never reused.
enum class TypeId : std::uint8_t
{
    none = 0,  // no type (yet)
    uint32 = 1,
    int32 = 2,
    uint64 = 3,
    int64 = 4,
    float32 = 5,
    float64 = 6,
    boolean = 7,
};

/// Formats as its number.
constexpr std::uint8_t format_as(TypeId id) noexcept { return static_cast<std::uint8_t>(id); }

/// The value types of byte-encoded storage, in TypeId order: the i-th has TypeId i + 1.
using ByteCodableTypes = boost::mp11::mp_list<std::uint32_t, std::int32_t, std::uint64_t, std::int64_t, float, double, bool>;

template<typename T>
concept ByteCodable = boost::mp11::mp_contains<ByteCodableTypes, T>::value;

template<ByteCodable T>
inline constexpr TypeId type_id = static_cast<TypeId>(boost::mp11::mp_find<ByteCodableTypes, T>::value + 1);

/// Fixed-width, canonical in-memory encoding: equal values encode to identical bytes and decoding preserves the value.
/// Not a portable file format.
template<ByteCodable T>
struct ByteCodec
{
    static_assert(!std::floating_point<T> || std::numeric_limits<T>::is_iec559);

    static constexpr std::size_t size = sizeof(T);

    static void encode(T value, std::span<std::byte> bytes)
    {
        if (bytes.size() != size)
            throw std::invalid_argument("ByteCodec: incorrect byte width.");
        if constexpr (std::same_as<T, bool>)
            bytes[0] = value ? std::byte { 1 } : std::byte { 0 };
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
            std::memcpy(bytes.data(), &value, size);
        }
    }

    static T decode(std::span<const std::byte> bytes)
    {
        if (bytes.size() != size)
            throw std::invalid_argument("ByteCodec: incorrect byte width.");
        if constexpr (std::same_as<T, bool>)
        {
            if (bytes[0] != std::byte { 0 } && bytes[0] != std::byte { 1 })
                throw std::invalid_argument("ByteCodec<bool>: invalid encoding.");
            return bytes[0] == std::byte { 1 };
        }
        else
        {
            auto value = T {};
            std::memcpy(&value, bytes.data(), size);
            return value;
        }
    }
};

/// Invocable with std::type_identity<T> for every byte-codable T.
template<typename Visitor>
concept TypeIdVisitor =
    boost::mp11::mp_all_of_q<boost::mp11::mp_transform<std::type_identity, ByteCodableTypes>, boost::mp11::mp_bind_front<std::is_invocable, Visitor&>>::value;

/// Calls visitor with std::type_identity<T> for the type T of `id`; every call returns the same type.
template<TypeIdVisitor Visitor>
decltype(auto) visit_type_id(TypeId id, Visitor visitor)
{
    using Result = std::invoke_result_t<Visitor&, std::type_identity<std::uint32_t>>;
    const auto call = [&]<typename T>(std::type_identity<T> type) -> Result
    {
        static_assert(std::same_as<std::invoke_result_t<Visitor&, std::type_identity<T>>, Result>,
                      "visit_type_id: the visitor must return the same type for every type id.");
        return std::invoke(visitor, type);
    };
    const auto index = static_cast<std::size_t>(id);
    if (index == 0 || index > boost::mp11::mp_size<ByteCodableTypes>::value)
        throw std::invalid_argument("visit_type_id: invalid type id.");
    return boost::mp11::mp_with_index<boost::mp11::mp_size<ByteCodableTypes>::value>(
        index - 1,
        [&](auto position) -> Result { return call(std::type_identity<boost::mp11::mp_at<ByteCodableTypes, decltype(position)>> {}); });
}

/// The byte width of the type of `id`, or 0 if `id` names no type.
constexpr std::size_t byte_size(TypeId id) noexcept
{
    const auto index = static_cast<std::size_t>(id);
    if (index == 0 || index > boost::mp11::mp_size<ByteCodableTypes>::value)
        return 0;
    return boost::mp11::mp_with_index<boost::mp11::mp_size<ByteCodableTypes>::value>(
        index - 1,
        [](auto position) { return ByteCodec<boost::mp11::mp_at<ByteCodableTypes, decltype(position)>>::size; });
}

}  // namespace ygg

#endif
