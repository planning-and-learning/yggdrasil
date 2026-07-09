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

#ifndef YGG_SEMANTICS_HASH_HPP_
#define YGG_SEMANTICS_HASH_HPP_

#include "yggdrasil/core/concepts.hpp"
#include "yggdrasil/semantics/murmurhash3.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <gtl/btree.hpp>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace ygg
{

// The deterministic hashing below fixes all algorithms and constants for 64-bit hash values, so that
// hash values are identical across standard libraries, compilers, and platform word sizes. Native
// hash-table APIs may narrow these values to size_t internally.

/// Deterministic hashing primitives with fixed algorithms and constants.
namespace hashing
{

/// MurmurHash3 (x64_128, fixed zero seed) over a byte range, truncated to 64 bits.
inline hash_t bytes(const char* data, size_t size) noexcept
{
    uint64_t out[2];  ///< MurmurHash3_x64_128 writes two 64-bit words.
    MurmurHash3_x64_128(data, static_cast<int>(size), 0U, out);
    return out[0];
}

}

/**
 * Forward declarations
 */

template<typename T>
inline void hash_combine(hash_t& seed, const T& value) noexcept;

template<typename T, typename... Rest>
inline void hash_combine(hash_t& seed, const Rest&... rest) noexcept;

template<typename... Ts>
inline hash_t hash_combine(const Ts&... rest) noexcept;

template<std::ranges::input_range Range>
inline hash_t hash_range(Range&& range) noexcept;

/// @brief `Hash` is our custom hasher, like std::hash, but with fixed, platform-independent algorithms.
///
/// There is deliberately no fallback to std::hash: its algorithms for floating-point and string types
/// differ between standard library implementations, which makes hash container iteration orders (and
/// everything order-sensitive built on top) platform-dependent. Unknown key types must either provide
/// identifying_members() or a ygg::Hash specialization.
/// The primary template is deliberately left undefined: a type without a deterministic hash fails to
/// compile as an incomplete type. Provide identifying_members() or specialize ygg::Hash for new types.
template<typename T = void>
struct Hash;

template<std::integral T>
struct Hash<T>
{
    hash_t operator()(const T& el) const noexcept { return fmix64(static_cast<uint64_t>(el)); }
};

template<Enumeration T>
struct Hash<T>
{
    hash_t operator()(const T& el) const noexcept { return Hash<std::underlying_type_t<T>> {}(static_cast<std::underlying_type_t<T>>(el)); }
};

template<>
struct Hash<void>
{
    using is_transparent = void;

    template<typename T>
    hash_t operator()(const T& el) const noexcept
    {
        return Hash<std::remove_cvref_t<T>> {}(el);
    }
};

/// Pointers appear in identity tuples (e.g., a view's repository) to discriminate owners. Their
/// address differs across runs (ASLR) and platforms, so hashing it would make hash values (and hash
/// container iteration orders) irreproducible. All pointers therefore hash to a fixed salt: distinct
/// owners still compare unequal via EqualTo, they merely share a hash bucket, which only costs
/// performance in the rare case of mixing elements of many owners in one container.
template<typename T>
struct Hash<T*>
{
    hash_t operator()(T* const&) const noexcept { return 0x2545f4914f6cdd1dULL; }  // any fixed salt
};

template<std::floating_point T>
struct Hash<T>
{
    static_assert(std::is_same_v<T, float> || std::is_same_v<T, double>, "ygg::Hash: long double has no portable bit representation.");

    hash_t operator()(const T& el) const noexcept
    {
        if (std::isnan(el))
            return 0x9e3779b97f4a7c15ULL;  // any fixed salt

        if (el == T(0))
            return fmix64(0);  // +0.0 and -0.0 compare equal, so they must hash alike

        if constexpr (std::is_same_v<T, float>)
            return fmix64(std::bit_cast<uint32_t>(el));
        else
            return fmix64(std::bit_cast<uint64_t>(el));
    }
};

template<>
struct Hash<std::string_view>
{
    hash_t operator()(std::string_view el) const noexcept { return hashing::bytes(el.data(), el.size()); }
};

template<>
struct Hash<std::string>
{
    hash_t operator()(const std::string& el) const noexcept { return hashing::bytes(el.data(), el.size()); }
};

template<typename T, size_t N>
struct Hash<std::array<T, N>>
{
    hash_t operator()(const std::array<T, N>& arr) const noexcept { return ygg::hash_range(arr); }
};

template<typename T>
struct Hash<std::reference_wrapper<T>>
{
    hash_t operator()(const std::reference_wrapper<T>& ref) const noexcept { return Hash<std::remove_cvref_t<T>> {}(ref.get()); }
};

template<typename Key, typename Compare, typename Allocator>
struct Hash<std::set<Key, Compare, Allocator>>
{
    hash_t operator()(const std::set<Key, Compare, Allocator>& set) const noexcept { return ygg::hash_range(set); }
};

template<typename Key, typename T, typename Compare, typename Allocator>
struct Hash<std::map<Key, T, Compare, Allocator>>
{
    hash_t operator()(const std::map<Key, T, Compare, Allocator>& map) const noexcept { return ygg::hash_range(map); }
};

template<typename Key, typename Compare, typename Allocator>
struct Hash<gtl::btree_set<Key, Compare, Allocator>>
{
    hash_t operator()(const gtl::btree_set<Key, Compare, Allocator>& set) const noexcept { return ygg::hash_range(set); }
};

template<typename Key, typename T, typename Compare, typename Allocator>
struct Hash<gtl::btree_map<Key, T, Compare, Allocator>>
{
    hash_t operator()(const gtl::btree_map<Key, T, Compare, Allocator>& map) const noexcept { return ygg::hash_range(map); }
};

template<typename T, typename Allocator>
struct Hash<std::vector<T, Allocator>>
{
    hash_t operator()(const std::vector<T, Allocator>& vec) const noexcept { return ygg::hash_range(vec); }
};

template<typename T1, typename T2>
struct Hash<std::pair<T1, T2>>
{
    hash_t operator()(const std::pair<T1, T2>& pair) const noexcept { return ygg::hash_combine(pair.first, pair.second); }
};

template<typename... Ts>
struct Hash<std::tuple<Ts...>>
{
    hash_t operator()(const std::tuple<Ts...>& tuple) const noexcept
    {
        hash_t aggregated_hash = sizeof...(Ts);
        std::apply([&aggregated_hash](const Ts&... args) { (ygg::hash_combine(aggregated_hash, args), ...); }, tuple);
        return aggregated_hash;
    }
};

template<typename... Ts>
struct Hash<std::variant<Ts...>>
{
    hash_t operator()(const std::variant<Ts...>& variant) const noexcept
    {
        hash_t seed = variant.index();
        std::visit([&seed](const auto& arg) { ygg::hash_combine(seed, arg); }, variant);
        return seed;
    }
};

template<typename T>
struct Hash<std::optional<T>>
{
    hash_t operator()(const std::optional<T>& optional) const noexcept
    {
        hash_t seed = optional.has_value() ? 1 : 0;
        if (optional.has_value())
            ygg::hash_combine(seed, optional.value());
        return seed;
    }
};

template<typename T, std::size_t Extent>
struct Hash<std::span<T, Extent>>
{
    hash_t operator()(const std::span<T, Extent>& span) const noexcept { return ygg::hash_range(span); }
};

template<Identifiable T>
struct Hash<T>
{
    using is_transparent = void;

    hash_t operator()(const T& element) const noexcept { return ygg::hash_combine(element.identifying_members()); }

    template<typename... Args>
    hash_t operator()(const std::tuple<Args...>& view) const noexcept
    {
        return ygg::hash_combine(view);
    }
};

/**
 * Definitions
 */

template<std::ranges::input_range Range>
inline hash_t hash_range(Range&& range) noexcept
{
    hash_t seed = 0;
    if constexpr (std::ranges::sized_range<Range>)
        seed = std::ranges::size(range);

    for (const auto& value : range)
        ygg::hash_combine(seed, value);

    return seed;
}

template<typename T>
inline void hash_combine(hash_t& seed, const T& value) noexcept
{
    seed ^= Hash<std::remove_cvref_t<T>> {}(value) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

template<typename T, typename... Rest>
inline void hash_combine(hash_t& seed, const Rest&... rest) noexcept
{
    (ygg::hash_combine(seed, rest), ...);
}

template<typename... Ts>
inline hash_t hash_combine(const Ts&... rest) noexcept
{
    hash_t seed = 0;
    (ygg::hash_combine(seed, rest), ...);
    return seed;
}

}  // namespace ygg

#endif
