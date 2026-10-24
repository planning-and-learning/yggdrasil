/*
 * Copyright (C) 2025-2026 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef YGG_CORE_CONCEPTS_HPP_
#define YGG_CORE_CONCEPTS_HPP_

#include "yggdrasil/core/config.hpp"
#include "yggdrasil/core/type_list.hpp"

#include <concepts>
#include <cstddef>
#include <ranges>
#include <type_traits>
#include <utility>

namespace ygg
{

template<typename T>
struct EqualTo;

template<typename T>
struct Hash;

template<typename T>
struct Less;

/// The members that identify a record, as member pointers:
/// `static constexpr auto identifying_members() noexcept { return ygg::Members<&Data::a, &Data::b> {}; }`.
template<auto... Ms>
struct Members
{
};

namespace detail
{
template<typename L>
inline constexpr bool is_members_v = false;
template<auto... Ms>
inline constexpr bool is_members_v<Members<Ms...>> = true;
}

/// @brief T lists its identifying members as a Members type.
template<typename T>
concept MemberIdentified = requires { T::identifying_members(); } && detail::is_members_v<decltype(T::identifying_members())>;

/// @brief T lists all its fields as member pointers, which cista and yggdrasil iterate without a tuple:
/// `static constexpr auto cista_members() noexcept { return ygg::Members<&Data::index, &Data::a> {}; }`.
template<typename T>
concept MemberListed = requires { T::cista_members(); } && detail::is_members_v<decltype(T::cista_members())>;

namespace detail
{
template<typename T, typename F, auto... Ms>
constexpr void for_each_member(T& t, F& f, Members<Ms...>)
{
    (f(t.*Ms), ...);
}
}

/// Calls f with each field of t, in the order of cista_members().
template<typename T, typename F>
    requires MemberListed<std::remove_const_t<T>>
constexpr void for_each_member(T& t, F f)
{
    detail::for_each_member(t, f, std::remove_const_t<T>::cista_members());
}

namespace detail
{
template<typename MemberPointer>
struct MemberPointee;
template<typename Class, typename Value>
struct MemberPointee<Value Class::*>
{
    using type = Value;
};

template<std::size_t I, typename List>
struct NthMember;
template<std::size_t I, auto M, auto... Ms>
struct NthMember<I, Members<M, Ms...>> : NthMember<I - 1, Members<Ms...>>
{
};
template<auto M, auto... Ms>
struct NthMember<0, Members<M, Ms...>>
{
    using type = typename MemberPointee<decltype(M)>::type;
};
}

/// @brief The type of the I-th identifying member of a member-identified T.
template<MemberIdentified T, std::size_t I>
using IdentifyingMember = typename detail::NthMember<I, decltype(T::identifying_members())>::type;

/// @brief T has a function that returns the members that identify it.
template<typename T>
concept FunctionIdentified = !MemberIdentified<T> && requires(const T a) {
    { a.identifying_members() };
};

template<typename T>
concept Identifiable = FunctionIdentified<T> || MemberIdentified<T>;

/// @brief The type returned by identifying_members().
template<FunctionIdentified T>
using IdentifyingMembers = decltype(std::declval<const T&>().identifying_members());

template<typename Range, typename Value>
concept InputRangeOf = std::ranges::input_range<Range> && std::same_as<std::ranges::range_value_t<Range>, Value>;

/// A read-only, multipass range whose elements bind to the requested value type.
template<typename Range, typename Value>
concept SizedForwardRangeOf =
    std::ranges::forward_range<const std::remove_reference_t<Range>> && std::ranges::sized_range<const std::remove_reference_t<Range>>
    && std::same_as<std::ranges::range_value_t<const std::remove_reference_t<Range>>, Value>
    && std::convertible_to<std::ranges::range_reference_t<const std::remove_reference_t<Range>>, const Value&>;

template<typename T>
concept Clearable = requires(T& value) { value.clear(); };

template<typename T>
concept TriviallyCopyable = std::is_trivially_copyable_v<T>;

template<typename T>
concept Enumeration = std::is_enum_v<T>;

template<typename T, typename U>
concept SameAsIgnoringCvref = std::same_as<std::remove_cvref_t<T>, std::remove_cvref_t<U>>;

template<typename T, typename U>
concept SameAsIgnoringConst = std::same_as<std::remove_const_t<T>, std::remove_const_t<U>>;

template<typename H, typename T>
concept HashFor = requires(const H& hash, const T& value) {
    { hash(value) } -> std::same_as<hash_t>;
};

template<typename E, typename Lhs, typename Rhs = Lhs>
concept EqualToFor = requires(const E& equal_to, const Lhs& lhs, const Rhs& rhs) {
    { equal_to(lhs, rhs) } -> std::same_as<bool>;
};

template<typename C, typename Lhs, typename Rhs = Lhs>
concept LessFor = requires(const C& less, const Lhs& lhs, const Rhs& rhs) {
    { less(lhs, rhs) } -> std::same_as<bool>;
};

template<typename T>
concept Hashable = HashFor<Hash<std::remove_cvref_t<T>>, T>;

template<typename T>
concept EqualityComparableByEqualTo = EqualToFor<EqualTo<std::remove_cvref_t<T>>, T>;

template<typename T>
concept OrderedByLess = LessFor<Less<std::remove_cvref_t<T>>, T>;

namespace detail
{
template<typename F, typename List>
struct InvocableWithEachTrait : std::false_type
{
};

template<typename F, typename... Ts>
struct InvocableWithEachTrait<F, TypeList<Ts...>> : std::bool_constant<(std::invocable<F, Ts> && ...)>
{
};
}  // namespace detail

/// A visitor that can be called with a value of each type of `List` (a TypeList), e.g. each alternative of a variant.
template<typename F, typename List>
concept InvocableWithEach = detail::InvocableWithEachTrait<F, List>::value;

}

#endif
