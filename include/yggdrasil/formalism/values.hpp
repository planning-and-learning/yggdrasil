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
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <urd/elements/index.hpp>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

namespace ygg::formalism
{
template<typename Tag>
struct Object;
}  // namespace ygg::formalism

namespace ygg
{

/// The types of a relation's columns, the only list of them: a column's value is one of its alternatives. An object
/// column's value is an object index (of any object tag; untagged here).
using ColumnValue = std::variant<std::uint32_t, std::uint64_t, std::int32_t, std::int64_t, float, double, bool, urd::index<formalism::Object<void>>>;

/// A column's type: the index of its alternative in ColumnValue.
enum class TypeId : std::uint8_t
{
};

/// Formats as its number.
constexpr std::uint8_t format_as(TypeId id) noexcept { return static_cast<std::uint8_t>(id); }

/// An alternative of ColumnValue.
template<typename T>
concept ColumnValueType = boost::mp11::mp_contains<ColumnValue, T>::value;

/// Whether T is an object index of some object tag (urd::index<formalism::Object<Tag>>): a value of an object column.
template<typename T>
inline constexpr bool is_object_index = false;
template<typename Tag>
inline constexpr bool is_object_index<urd::index<formalism::Object<Tag>>> = true;

/// The variant alternative a column value of T is stored as: the untagged object index for object indices of any tag.
template<typename T>
using column_alternative_t = std::conditional_t<is_object_index<T>, urd::index<formalism::Object<void>>, T>;

/// The column type of values of T: the index of its alternative in ColumnValue.
template<typename T>
    requires ColumnValueType<column_alternative_t<T>>
inline constexpr TypeId type_id = static_cast<TypeId>(boost::mp11::mp_find<ColumnValue, column_alternative_t<T>>::value);

/// A value's whole width: a bool's one bit, other types all their bits.
template<ColumnValueType T>
inline constexpr std::uint8_t whole_bits = std::same_as<T, bool> ? 1 : sizeof(T) * 8;

/// A column's value as the unsigned bits its row stores (urd rows know no types): an unsigned integer, a bool or an
/// object index as is, a signed integer as its two's complement in its own width, a float as its bits (NaN and -0.0
/// normalized). What rows are read, hashed, compared and built as, one per column, whatever the column's type.
using Value = std::uint64_t;

/// encode_value<T>/decode_value<T>: a value's bits and back, one rule per category of T.

/// A floating-point value through the unsigned integer of its size (NaN and -0.0 normalized).
template<std::floating_point T>
[[nodiscard]] Value encode_value(T value) noexcept
{
    using Bits = typename boost::uint_t<sizeof(T) * 8>::exact;
    const auto normal = std::isnan(value) ? std::numeric_limits<T>::quiet_NaN() : (value == T { 0 } ? T { 0 } : value);
    return std::bit_cast<Bits>(normal);
}
template<std::floating_point T>
[[nodiscard]] T decode_value(Value bits) noexcept
{ return std::bit_cast<T>(static_cast<typename boost::uint_t<sizeof(T) * 8>::exact>(bits)); }

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

[[nodiscard]] inline Value encode(const ColumnValue& value) noexcept
{ return std::visit([](auto alternative) { return encode_value(alternative); }, value); }

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
    constexpr auto widths = []<typename... Ts>(std::type_identity<std::variant<Ts...>>) { return std::array { whole_bits<Ts>... }; }(std::type_identity<ColumnValue> {});
    return type == type_id<urd::index<formalism::Object<void>>> ? object_bits : widths.at(static_cast<std::size_t>(type));
}

}  // namespace ygg

#endif
