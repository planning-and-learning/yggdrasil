/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_FORMALISM_VALUES_HPP_
#define YGG_FORMALISM_VALUES_HPP_

#include <array>
#include <bit>
#include <boost/mp11.hpp>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

namespace ygg
{

/// An object column's value: an object index.
struct ObjectIndex
{
    std::uint32_t value;

    friend constexpr bool operator==(ObjectIndex, ObjectIndex) = default;
};

/// The types of a relation's columns, the only list of them: a column's value is one of its alternatives.
using ColumnValue = std::variant<std::uint32_t, std::uint64_t, std::int32_t, std::int64_t, float, double, bool, ObjectIndex>;

/// A column's type: the index of its alternative in ColumnValue.
enum class TypeId : std::uint8_t
{
};

/// Formats as its number.
constexpr std::uint8_t format_as(TypeId id) noexcept { return static_cast<std::uint8_t>(id); }

/// An alternative of ColumnValue.
template<typename T>
concept ColumnValueType = boost::mp11::mp_contains<ColumnValue, T>::value;

template<ColumnValueType T>
inline constexpr TypeId type_id = static_cast<TypeId>(boost::mp11::mp_find<ColumnValue, T>::value);

/// A value's whole width: a bool's one bit, other types all their bits.
template<ColumnValueType T>
inline constexpr std::uint8_t whole_bits = std::same_as<T, bool> ? 1 : sizeof(T) * 8;

/// A column's value as the unsigned bits its row stores (urd rows know no types): an unsigned integer, a bool or an
/// object index as is, a signed integer as its two's complement in its own width, a float as its bits (NaN and -0.0
/// normalized). What rows are read, hashed, compared and built as, one per column, whatever the column's type.
using Value = std::uint64_t;

/// The value's bits (NaN and -0.0 normalized).
template<ColumnValueType T>
[[nodiscard]] Value encode_value(T value) noexcept
{
    if constexpr (std::floating_point<T>)
    {
        using Bits = std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>;
        const auto normal = std::isnan(value) ? std::numeric_limits<T>::quiet_NaN() : (value == T { 0 } ? T { 0 } : value);
        return std::bit_cast<Bits>(normal);
    }
    else if constexpr (std::same_as<T, bool>)
        return value ? 1 : 0;
    else if constexpr (std::same_as<T, ObjectIndex>)
        return value.value;
    else
        return static_cast<std::make_unsigned_t<T>>(value);
}
/// The value whose bits these are.
template<ColumnValueType T>
[[nodiscard]] T decode_value(Value bits) noexcept
{
    if constexpr (std::same_as<T, bool>)
        return bits != 0;
    else if constexpr (std::same_as<T, float>)
        return std::bit_cast<float>(static_cast<std::uint32_t>(bits));
    else if constexpr (std::same_as<T, double>)
        return std::bit_cast<double>(bits);
    else if constexpr (std::same_as<T, ObjectIndex>)
        return ObjectIndex { static_cast<std::uint32_t>(bits) };
    else
        return static_cast<T>(static_cast<std::make_unsigned_t<T>>(bits));
}

[[nodiscard]] inline Value encode(const ColumnValue& value) noexcept
{ return std::visit([](auto alternative) { return encode_value(alternative); }, value); }

/// The value of a column of `type` whose bits these are; std::invalid_argument where `type` names no type.
[[nodiscard]] inline ColumnValue decode(Value bits, TypeId type)
{
    constexpr auto count = std::variant_size_v<ColumnValue>;
    if (static_cast<std::size_t>(type) >= count)
        throw std::invalid_argument("decode: invalid type id.");
    return boost::mp11::mp_with_index<count>(static_cast<std::size_t>(type),
                                             [bits](auto i) { return ColumnValue(std::in_place_index<i>, decode_value<std::variant_alternative_t<i, ColumnValue>>(bits)); });
}

/// The bits a row gives a column of the type: its whole width, an object `object_bits`; std::out_of_range where
/// `type` names no type.
constexpr std::uint8_t column_bits(TypeId type, std::uint8_t object_bits)
{
    constexpr auto widths = []<typename... Ts>(std::type_identity<std::variant<Ts...>>) { return std::array { whole_bits<Ts>... }; }(std::type_identity<ColumnValue> {});
    return type == type_id<ObjectIndex> ? object_bits : widths.at(static_cast<std::size_t>(type));
}

}  // namespace ygg

#endif
