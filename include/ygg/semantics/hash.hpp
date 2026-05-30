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

#ifndef YGG_COMMON_HASH_HPP_
#define YGG_COMMON_HASH_HPP_

#include "ygg/core/concepts.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <functional>
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

#include <gtl/btree.hpp>

namespace ygg
{

/**
 * Forward declarations
 */

template<typename T>
inline void hash_combine(size_t& seed, const T& value) noexcept;

template<typename T, typename... Rest>
inline void hash_combine(size_t& seed, const Rest&... rest) noexcept;

template<typename... Ts>
inline size_t hash_combine(const Ts&... rest) noexcept;

template<std::ranges::input_range Range>
inline size_t hash_range(const Range& range) noexcept;

/// @brief `Hash` is our custom hasher, like std::hash.
///
/// Forwards to std::hash by default.
/// Specializations can be injected into the namespace.
template<typename T = void>
struct Hash
{
    size_t operator()(const T& el) const noexcept { return std::hash<T> {}(el); }
};

template<>
struct Hash<void>
{
    using is_transparent = void;

    template<typename T>
    size_t operator()(const T& el) const noexcept
    {
        return Hash<std::remove_cvref_t<T>> {}(el);
    }
};

template<std::floating_point T>
struct Hash<T>
{
    size_t operator()(const T& el) const noexcept
    {
        if (std::isnan(el))
            return 0x9e3779b97f4a7c15ULL;  // any fixed salt

        return std::hash<T> {}(el);
    }
};

template<typename T, size_t N>
struct Hash<std::array<T, N>>
{
    size_t operator()(const std::array<T, N>& arr) const noexcept { return hash_range(arr); }
};

template<typename T>
struct Hash<std::reference_wrapper<T>>
{
    size_t operator()(const std::reference_wrapper<T>& ref) const noexcept { return Hash<std::remove_cvref_t<T>> {}(ref.get()); }
};

template<typename Key, typename Compare, typename Allocator>
struct Hash<std::set<Key, Compare, Allocator>>
{
    size_t operator()(const std::set<Key, Compare, Allocator>& set) const noexcept { return hash_range(set); }
};

template<typename Key, typename T, typename Compare, typename Allocator>
struct Hash<std::map<Key, T, Compare, Allocator>>
{
    size_t operator()(const std::map<Key, T, Compare, Allocator>& map) const noexcept { return hash_range(map); }
};

template<typename Key, typename Compare, typename Allocator>
struct Hash<gtl::btree_set<Key, Compare, Allocator>>
{
    size_t operator()(const gtl::btree_set<Key, Compare, Allocator>& set) const noexcept { return hash_range(set); }
};

template<typename Key, typename T, typename Compare, typename Allocator>
struct Hash<gtl::btree_map<Key, T, Compare, Allocator>>
{
    size_t operator()(const gtl::btree_map<Key, T, Compare, Allocator>& map) const noexcept { return hash_range(map); }
};

template<typename T, typename Allocator>
struct Hash<std::vector<T, Allocator>>
{
    size_t operator()(const std::vector<T, Allocator>& vec) const noexcept { return hash_range(vec); }
};

template<typename T1, typename T2>
struct Hash<std::pair<T1, T2>>
{
    size_t operator()(const std::pair<T1, T2>& pair) const noexcept { return hash_combine(pair.first, pair.second); }
};

template<typename... Ts>
struct Hash<std::tuple<Ts...>>
{
    size_t operator()(const std::tuple<Ts...>& tuple) const noexcept
    {
        size_t aggregated_hash = sizeof...(Ts);
        std::apply([&aggregated_hash](const Ts&... args) { (hash_combine(aggregated_hash, args), ...); }, tuple);
        return aggregated_hash;
    }
};

template<typename... Ts>
struct Hash<std::variant<Ts...>>
{
    size_t operator()(const std::variant<Ts...>& variant) const noexcept
    {
        return std::visit([](const auto& arg) { return Hash<std::remove_cvref_t<decltype(arg)>> {}(arg); }, variant);
    }
};

template<typename T>
struct Hash<std::optional<T>>
{
    size_t operator()(const std::optional<T>& optional) const noexcept { return optional.has_value() ? Hash<std::remove_cvref_t<T>> {}(optional.value()) : 0; }
};

template<typename T, std::size_t Extent>
struct Hash<std::span<T, Extent>>
{
    size_t operator()(const std::span<T, Extent>& span) const noexcept { return hash_range(span); }
};

template<Identifiable T>
struct Hash<T>
{
    using is_transparent = void;  // <-- enables hetero lookup

    size_t operator()(const T& element) const noexcept { return hash_combine(element.identifying_members()); }

    template<typename... Args>
    size_t operator()(const std::tuple<Args...>& view) const noexcept
    {
        return hash_combine(view);
    }
};

/**
 * Definitions
 */

template<std::ranges::input_range Range>
inline size_t hash_range(const Range& range) noexcept
{
    size_t seed = 0;
    if constexpr (std::ranges::sized_range<Range>)
        seed = std::ranges::size(range);

    for (const auto& value : range)
        hash_combine(seed, value);

    return seed;
}

template<typename T>
inline void hash_combine(size_t& seed, const T& value) noexcept
{
    seed ^= Hash<std::remove_cvref_t<T>> {}(value) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

template<typename T, typename... Rest>
inline void hash_combine(size_t& seed, const Rest&... rest) noexcept
{
    (hash_combine(seed, rest), ...);
}

template<typename... Ts>
inline size_t hash_combine(const Ts&... rest) noexcept
{
    size_t seed = 0;
    (hash_combine(seed, rest), ...);
    return seed;
}

}

#endif
