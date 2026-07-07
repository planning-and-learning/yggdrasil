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

#ifndef YGG_SEMANTICS_COMPARATORS_HPP_
#define YGG_SEMANTICS_COMPARATORS_HPP_

#include "yggdrasil/core/concepts.hpp"

#include <array>
#include <cstddef>
#include <functional>
#include <gtl/btree.hpp>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace ygg
{

template<std::ranges::input_range LhsRange, std::ranges::input_range RhsRange>
inline bool less_range(LhsRange&& lhs, RhsRange&& rhs) noexcept;

/// @brief `Less` is our custom less-than comparator, like std::less.
///
/// Forwards to std::less by default.
/// Specializations can be injected into the namespace.
template<typename T = void>
struct Less
{
    bool operator()(const T& lhs, const T& rhs) const noexcept { return std::less<T> {}(lhs, rhs); }
};

template<>
struct Less<void>
{
    using is_transparent = void;

    template<typename T, typename U>
    bool operator()(const T& lhs, const U& rhs) const noexcept
    {
        return Less<std::remove_cvref_t<T>> {}(lhs, rhs);
    }
};

template<typename... Ts>
struct Less<std::tuple<Ts...>>
{
    bool operator()(const std::tuple<Ts...>& lhs, const std::tuple<Ts...>& rhs) const noexcept { return less_impl<0>(lhs, rhs); }

private:
    template<size_t I>
    static bool less_impl(const std::tuple<Ts...>& lhs, const std::tuple<Ts...>& rhs) noexcept
    {
        if constexpr (I == sizeof...(Ts))
        {
            return false;
        }
        else
        {
            using Element = std::remove_cvref_t<std::tuple_element_t<I, std::tuple<Ts...>>>;

            if (Less<Element> {}(std::get<I>(lhs), std::get<I>(rhs)))
                return true;

            if (Less<Element> {}(std::get<I>(rhs), std::get<I>(lhs)))
                return false;

            return less_impl<I + 1>(lhs, rhs);
        }
    }
};

template<typename T, size_t N>
struct Less<std::array<T, N>>
{
    bool operator()(const std::array<T, N>& lhs, const std::array<T, N>& rhs) const noexcept { return less_range(lhs, rhs); }
};

template<typename T, typename Allocator>
struct Less<std::vector<T, Allocator>>
{
    bool operator()(const std::vector<T, Allocator>& lhs, const std::vector<T, Allocator>& rhs) const noexcept { return less_range(lhs, rhs); }
};

template<typename Key, typename Compare, typename Allocator>
struct Less<std::set<Key, Compare, Allocator>>
{
    bool operator()(const std::set<Key, Compare, Allocator>& lhs, const std::set<Key, Compare, Allocator>& rhs) const noexcept { return less_range(lhs, rhs); }
};

template<typename Key, typename T, typename Compare, typename Allocator>
struct Less<std::map<Key, T, Compare, Allocator>>
{
    bool operator()(const std::map<Key, T, Compare, Allocator>& lhs, const std::map<Key, T, Compare, Allocator>& rhs) const noexcept
    {
        return less_range(lhs, rhs);
    }
};

template<typename Key, typename Compare, typename Allocator>
struct Less<gtl::btree_set<Key, Compare, Allocator>>
{
    bool operator()(const gtl::btree_set<Key, Compare, Allocator>& lhs, const gtl::btree_set<Key, Compare, Allocator>& rhs) const noexcept
    {
        return less_range(lhs, rhs);
    }
};

template<typename Key, typename T, typename Compare, typename Allocator>
struct Less<gtl::btree_map<Key, T, Compare, Allocator>>
{
    bool operator()(const gtl::btree_map<Key, T, Compare, Allocator>& lhs, const gtl::btree_map<Key, T, Compare, Allocator>& rhs) const noexcept
    {
        return less_range(lhs, rhs);
    }
};

template<typename T1, typename T2>
struct Less<std::pair<T1, T2>>
{
    bool operator()(const std::pair<T1, T2>& lhs, const std::pair<T1, T2>& rhs) const noexcept
    {
        if (Less<std::remove_cvref_t<T1>> {}(lhs.first, rhs.first))
            return true;

        if (Less<std::remove_cvref_t<T1>> {}(rhs.first, lhs.first))
            return false;

        return Less<std::remove_cvref_t<T2>> {}(lhs.second, rhs.second);
    }
};

template<typename T>
struct Less<std::reference_wrapper<T>>
{
    bool operator()(const std::reference_wrapper<T>& lhs, const std::reference_wrapper<T>& rhs) const noexcept
    {
        return Less<std::remove_cvref_t<T>> {}(lhs.get(), rhs.get());
    }
};

template<typename T>
struct Less<std::optional<T>>
{
    bool operator()(const std::optional<T>& lhs, const std::optional<T>& rhs) const noexcept
    {
        if (lhs.has_value() != rhs.has_value())
            return !lhs.has_value();

        return lhs.has_value() ? Less<std::remove_cvref_t<T>> {}(lhs.value(), rhs.value()) : false;
    }
};

template<typename... Ts>
struct Less<std::variant<Ts...>>
{
    bool operator()(const std::variant<Ts...>& lhs, const std::variant<Ts...>& rhs) const noexcept
    {
        if (lhs.index() != rhs.index())
            return lhs.index() < rhs.index();

        return std::visit(
            [](const auto& l, const auto& r)
            {
                if constexpr (std::is_same_v<std::remove_cvref_t<decltype(l)>, std::remove_cvref_t<decltype(r)>>)
                    return Less<std::remove_cvref_t<decltype(l)>> {}(l, r);
                return false;
            },
            lhs,
            rhs);
    }
};

template<typename T, std::size_t Extent>
struct Less<std::span<T, Extent>>
{
    bool operator()(const std::span<T, Extent>& lhs, const std::span<T, Extent>& rhs) const noexcept { return less_range(lhs, rhs); }
};

template<Identifiable T>
struct Less<T>
{
    using is_transparent = void;

    using MembersTupleType = decltype(std::declval<T>().identifying_members());

    bool operator()(const T& lhs, const T& rhs) const noexcept
    {
        return Less<std::remove_cvref_t<MembersTupleType>> {}(lhs.identifying_members(), rhs.identifying_members());
    }

    template<SameAsIgnoringCvref<MembersTupleType> U>
    bool operator()(const T& a, const U& v) const noexcept
    {
        return Less<std::remove_cvref_t<MembersTupleType>> {}(a.identifying_members(), v);
    }

    template<SameAsIgnoringCvref<MembersTupleType> U>
    bool operator()(const U& v, const T& b) const noexcept
    {
        return Less<std::remove_cvref_t<MembersTupleType>> {}(v, b.identifying_members());
    }

    template<SameAsIgnoringCvref<MembersTupleType> U, SameAsIgnoringCvref<MembersTupleType> V>
    bool operator()(const U& u, const V& v) const noexcept
    {
        return Less<std::remove_cvref_t<MembersTupleType>> {}(u, v);
    }
};

/// @brief `LessEqual` is our custom less-equal comparator, like
/// std::less_equal.
///
/// Forwards to std::less_equal by default.
/// Specializations can be injected into the namespace.
template<typename T>
struct LessEqual
{
    bool operator()(const T& lhs, const T& rhs) const noexcept { return !Less<T> {}(rhs, lhs); }
};

template<Identifiable T>
struct LessEqual<T>
{
    using is_transparent = void;

    using MembersTupleType = decltype(std::declval<T>().identifying_members());

    bool operator()(const T& lhs, const T& rhs) const noexcept
    {
        return LessEqual<std::remove_cvref_t<MembersTupleType>> {}(lhs.identifying_members(), rhs.identifying_members());
    }

    // Mixed overloads support heterogeneous lookup against identifying member
    // tuples.
    template<SameAsIgnoringCvref<MembersTupleType> U>
    bool operator()(const T& a, const U& v) const noexcept
    {
        return LessEqual<std::remove_cvref_t<MembersTupleType>> {}(a.identifying_members(), v);
    }

    template<SameAsIgnoringCvref<MembersTupleType> U>
    bool operator()(const U& v, const T& b) const noexcept
    {
        return LessEqual<std::remove_cvref_t<MembersTupleType>> {}(v, b.identifying_members());
    }

    // Tuple-like overload for direct identifying-member comparisons.
    template<SameAsIgnoringCvref<MembersTupleType> U, SameAsIgnoringCvref<MembersTupleType> V>
    bool operator()(const U& u, const V& v) const noexcept
    {
        return LessEqual<std::remove_cvref_t<MembersTupleType>> {}(u, v);
    }
};

/// @brief `Greater` is our custom greater-than comparator, like std::greater.
///
/// Forwards to std::greater by default.
/// Specializations can be injected into the namespace.
template<typename T>
struct Greater
{
    bool operator()(const T& lhs, const T& rhs) const noexcept { return Less<T> {}(rhs, lhs); }
};

template<Identifiable T>
struct Greater<T>
{
    using is_transparent = void;

    using MembersTupleType = decltype(std::declval<T>().identifying_members());

    bool operator()(const T& lhs, const T& rhs) const noexcept
    {
        return Greater<std::remove_cvref_t<MembersTupleType>> {}(lhs.identifying_members(), rhs.identifying_members());
    }

    // Mixed overloads support heterogeneous lookup against identifying member
    // tuples.
    template<SameAsIgnoringCvref<MembersTupleType> U>
    bool operator()(const T& a, const U& v) const noexcept
    {
        return Greater<std::remove_cvref_t<MembersTupleType>> {}(a.identifying_members(), v);
    }

    template<SameAsIgnoringCvref<MembersTupleType> U>
    bool operator()(const U& v, const T& b) const noexcept
    {
        return Greater<std::remove_cvref_t<MembersTupleType>> {}(v, b.identifying_members());
    }

    // Tuple-like overload for direct identifying-member comparisons.
    template<SameAsIgnoringCvref<MembersTupleType> U, SameAsIgnoringCvref<MembersTupleType> V>
    bool operator()(const U& u, const V& v) const noexcept
    {
        return Greater<std::remove_cvref_t<MembersTupleType>> {}(u, v);
    }
};

/// @brief `GreaterEqual` is our custom greater-equal comparator, like
/// std::greater_equal.
///
/// Forwards to std::greater_equal by default.
/// Specializations can be injected into the namespace.
template<typename T>
struct GreaterEqual
{
    bool operator()(const T& lhs, const T& rhs) const noexcept { return !Less<T> {}(lhs, rhs); }
};

template<Identifiable T>
struct GreaterEqual<T>
{
    using is_transparent = void;

    using MembersTupleType = decltype(std::declval<T>().identifying_members());

    bool operator()(const T& lhs, const T& rhs) const noexcept
    {
        return GreaterEqual<std::remove_cvref_t<MembersTupleType>> {}(lhs.identifying_members(), rhs.identifying_members());
    }

    // Mixed overloads support heterogeneous lookup against identifying member
    // tuples.
    template<SameAsIgnoringCvref<MembersTupleType> U>
    bool operator()(const T& a, const U& v) const noexcept
    {
        return GreaterEqual<std::remove_cvref_t<MembersTupleType>> {}(a.identifying_members(), v);
    }

    template<SameAsIgnoringCvref<MembersTupleType> U>
    bool operator()(const U& v, const T& b) const noexcept
    {
        return GreaterEqual<std::remove_cvref_t<MembersTupleType>> {}(v, b.identifying_members());
    }

    // Tuple-like overload for direct identifying-member comparisons.
    template<SameAsIgnoringCvref<MembersTupleType> U, SameAsIgnoringCvref<MembersTupleType> V>
    bool operator()(const U& u, const V& v) const noexcept
    {
        return GreaterEqual<std::remove_cvref_t<MembersTupleType>> {}(u, v);
    }
};

template<std::ranges::input_range LhsRange, std::ranges::input_range RhsRange>
inline bool less_range(LhsRange&& lhs, RhsRange&& rhs) noexcept
{
    auto lhs_it = std::ranges::begin(lhs);
    auto rhs_it = std::ranges::begin(rhs);
    const auto lhs_end = std::ranges::end(lhs);
    const auto rhs_end = std::ranges::end(rhs);

    for (; lhs_it != lhs_end && rhs_it != rhs_end; ++lhs_it, ++rhs_it)
    {
        using LhsValue = std::remove_cvref_t<decltype(*lhs_it)>;
        using RhsValue = std::remove_cvref_t<decltype(*rhs_it)>;
        if constexpr (std::same_as<LhsValue, RhsValue>)
        {
            if (Less<LhsValue> {}(*lhs_it, *rhs_it))
                return true;
            if (Less<LhsValue> {}(*rhs_it, *lhs_it))
                return false;
        }
        else
        {
            return false;
        }
    }

    return lhs_it == lhs_end && rhs_it != rhs_end;
}

}  // namespace ygg

#endif
