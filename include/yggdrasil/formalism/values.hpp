/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_FORMALISM_VALUES_HPP_
#define YGG_FORMALISM_VALUES_HPP_

#include <array>
#include <bit>
#include <boost/integer.hpp>
#include <boost/mp11.hpp>
#include <climits>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <type_traits>
#include <urd/elements/index.hpp>
#include <utility>
#include <variant>

namespace ygg::formalism
{
struct Object;
}  // namespace ygg::formalism

namespace ygg
{

/// The types of a relation's columns, the only list of them: a column's value is one of its alternatives. An object
/// column's value is an object index.
using ColumnValue = std::variant<std::uint32_t, std::uint64_t, std::int32_t, std::int64_t, float, double, bool, urd::index<formalism::Object>>;

/// A column's type: the index of its alternative in ColumnValue.
enum class TypeId : std::uint8_t
{
};

/// Formats as its number.
constexpr std::uint8_t format_as(TypeId id) noexcept { return static_cast<std::uint8_t>(id); }

/// An alternative of ColumnValue.
template<typename T>
concept ColumnValueType = boost::mp11::mp_contains<ColumnValue, T>::value;

/// The column type of values of T: the index of its alternative in ColumnValue.
template<typename T>
    requires ColumnValueType<T>
inline constexpr TypeId type_id = static_cast<TypeId>(boost::mp11::mp_find<ColumnValue, T>::value);

/// A value's whole width: a bool's one bit, other types all their bits.
template<ColumnValueType T>
inline constexpr std::uint8_t whole_bits = std::same_as<T, bool> ? 1 : sizeof(T) * CHAR_BIT;

/// A column's value as the unsigned bits its row stores (urd rows know no types): an unsigned integer, a bool or an
/// object index as is, a signed integer as its two's complement in its own width, a float as its bits (NaN and -0.0
/// normalized). What rows are read, hashed, compared and built as, one per column, whatever the column's type.
using Value = std::uint64_t;

/// The values of a row as its views read them, whatever the layout it is stored in (bit-packed, contiguous, a trie): a
/// sized input range of Values. Random access, where the layout has it, makes a column's read O(1).
template<typename R>
concept RowValues = std::ranges::input_range<R> && std::ranges::sized_range<R> && std::same_as<std::ranges::range_value_t<R>, Value>;

/// encode_value<T>/decode_value<T>: a value's bits and back, one rule per category of T.

/// A floating-point value through the unsigned integer of its size (NaN and -0.0 normalized).
template<std::floating_point T>
[[nodiscard]] Value encode_value(T value) noexcept
{
    using Bits = typename boost::uint_t<sizeof(T) * CHAR_BIT>::exact;
    const auto normal = std::isnan(value) ? std::numeric_limits<T>::quiet_NaN() : (value == T { 0 } ? T { 0 } : value);
    return std::bit_cast<Bits>(normal);
}

template<std::floating_point T>
[[nodiscard]] T decode_value(Value bits) noexcept
{ return std::bit_cast<T>(static_cast<typename boost::uint_t<sizeof(T) * CHAR_BIT>::exact>(bits)); }

/// An integer through its unsigned type (a signed one as its two's complement in its own width).
template<std::integral T>
    requires(!std::same_as<T, bool>)
[[nodiscard]] Value encode_value(T value) noexcept
{ return static_cast<std::make_unsigned_t<T>>(value); }

template<std::integral T>
    requires(!std::same_as<T, bool>)
[[nodiscard]] T decode_value(Value bits) noexcept
{ return static_cast<T>(static_cast<std::make_unsigned_t<T>>(bits)); }

/// A bool as 0 or 1.
template<std::same_as<bool> T>
[[nodiscard]] Value encode_value(T value) noexcept
{ return value ? 1 : 0; }

template<std::same_as<bool> T>
[[nodiscard]] T decode_value(Value bits) noexcept
{ return bits != 0; }

/// An urd index (an object index of any tag) as its value (urd::index_of).
template<typename T>
    requires urd::is_index_type<T>
[[nodiscard]] Value encode_value(T value) noexcept
{ return urd::index_of<T>::to_index(value); }

template<typename T>
    requires urd::is_index_type<T>
[[nodiscard]] T decode_value(Value bits) noexcept
{ return urd::index_of<T>::from_index(static_cast<std::size_t>(bits)); }

// NOLINTNEXTLINE(bugprone-exception-escape): std::visit throws only for a valueless variant, which ColumnValue's alternatives (scalars, an index) never leave
[[nodiscard]] inline Value encode(const ColumnValue& value) noexcept
{
    return std::visit([](auto alternative) { return encode_value(alternative); }, value);
}

/// The bits a column of `type` stores for `value`, the value converted to the column's type first: an integer to an
/// integer type it fits, an integer or a floating-point number to a floating-point type, a bool to bool, an object's
/// index to the object type; std::invalid_argument where `type` names no type or the value does not convert to it.
[[nodiscard]] inline Value encode(const ColumnValue& value, TypeId type)
{
    constexpr auto encoders = []<typename... Ts>(std::type_identity<std::variant<Ts...>>)
    {
        return std::array { +[](const ColumnValue& given) -> Value
                            {
                                return std::visit(
                                    [](auto held) -> Value
                                    {
                                        using From = decltype(held);
                                        using To = Ts;
                                        if constexpr (std::same_as<From, To>)
                                            return encode_value(held);
                                        else if constexpr (std::integral<From> && std::integral<To> && !std::same_as<From, bool> && !std::same_as<To, bool>)
                                        {
                                            if (!std::in_range<To>(held))
                                                throw std::invalid_argument("encode: the value does not fit the column's type.");
                                            return encode_value(static_cast<To>(held));
                                        }
                                        else if constexpr (std::floating_point<To>
                                                           && (std::floating_point<From> || (std::integral<From> && !std::same_as<From, bool>) ))
                                            return encode_value(static_cast<To>(held));
                                        else
                                            throw std::invalid_argument("encode: the value does not convert to the column's type.");
                                    },
                                    given);
                            }... };
    }(std::type_identity<ColumnValue> {});
    if (static_cast<std::size_t>(type) >= encoders.size())
        throw std::invalid_argument("encode: invalid type id.");
    return encoders[static_cast<std::size_t>(type)](value);
}

/// encode of a value of one alternative: what Python passes (a bool, an integer, a floating-point number, an object's
/// index) goes to the alternative of its own type, which encode then converts to the column's.
[[nodiscard]] inline Value encode(bool value, TypeId type) { return encode(ColumnValue(value), type); }

[[nodiscard]] inline Value encode(std::int64_t value, TypeId type) { return encode(ColumnValue(value), type); }

[[nodiscard]] inline Value encode(std::uint64_t value, TypeId type) { return encode(ColumnValue(value), type); }

[[nodiscard]] inline Value encode(double value, TypeId type) { return encode(ColumnValue(value), type); }

[[nodiscard]] inline Value encode(urd::index<formalism::Object> value, TypeId type) { return encode(ColumnValue(value), type); }

/// The value of a column of `type` whose bits these are; std::invalid_argument where `type` names no type.
[[nodiscard]] inline ColumnValue decode(Value bits, TypeId type)
{
    constexpr auto decoders = []<typename... Ts>(std::type_identity<std::variant<Ts...>>)
    { return std::array { +[](Value value) { return ColumnValue(std::in_place_type<Ts>, decode_value<Ts>(value)); }... }; }(std::type_identity<ColumnValue> {});
    if (static_cast<std::size_t>(type) >= decoders.size())
        throw std::invalid_argument("decode: invalid type id.");
    return decoders[static_cast<std::size_t>(type)](bits);
}

/// The bits a row gives a column of the type: its whole width, an object `object_bits`; std::out_of_range where
/// `type` names no type.
constexpr std::uint8_t column_bits(TypeId type, std::uint8_t object_bits)
{
    constexpr auto widths = []<typename... Ts>(std::type_identity<std::variant<Ts...>>)
    { return std::array { whole_bits<Ts>... }; }(std::type_identity<ColumnValue> {});
    return type == type_id<urd::index<formalism::Object>> ? object_bits : widths.at(static_cast<std::size_t>(type));
}

}  // namespace ygg

#endif
