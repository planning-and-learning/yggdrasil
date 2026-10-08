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

#include <array>
#include <boost/container_hash/hash.hpp>
#include <boost/hash2/xxhash.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <gtl/btree.hpp>
#include <map>
#include <memory>
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

// Hashes are deterministic for a fixed Boost version and platform word size. Byte-hashed ranges
// additionally depend on their element representation. The public result is 64-bit; hash_combine
// uses Boost.ContainerHash's native-size mixing.

/// Deterministic hashing primitives with fixed algorithms and constants.
namespace hashing
{

/// xxHash64 with a fixed zero seed over a byte range.
inline hash_t hash_bytes(const void* data, size_t size) noexcept
{
    auto hash = boost::hash2::xxhash_64 {};
    hash.update(data, size);
    return hash.result();
}

/// Byte equality must agree with value equality; custom Hash specializations retain value hashing.
template<typename T>
concept ByteHashable = (std::integral<T> || Enumeration<T>) && !std::same_as<T, bool> && std::has_unique_object_representations_v<T>
                      && requires { requires Hash<T>::is_identity; };

template<typename Range>
concept ByteHashableRange = std::ranges::input_range<Range> && Hashable<std::ranges::range_value_t<Range>> && std::ranges::sized_range<Range>
                            && ByteHashable<std::ranges::range_value_t<Range>>;
}

/**
 * Forward declarations
 */

template<Hashable T>
inline void hash_combine(hash_t& seed, const T& value);

template<typename T, Hashable... Rest>
inline void hash_combine(hash_t& seed, const Rest&... rest);

template<Hashable... Ts>
inline hash_t hash_combine(const Ts&... rest);

template<std::ranges::input_range Range>
    requires Hashable<std::ranges::range_value_t<Range>>
inline hash_t hash_range(Range&& range);

template<hashing::ByteHashableRange Range>
inline hash_t hash_range(Range&& range);

template<hashing::ByteHashableRange Range>
    requires std::ranges::contiguous_range<Range> && (!std::is_volatile_v<std::remove_reference_t<std::ranges::range_reference_t<Range>>>)
inline hash_t hash_range(Range&& range) noexcept(noexcept(std::ranges::data(range)) && noexcept(std::ranges::size(range)));

/// @brief `Hash` is our custom hasher, using explicit algorithms instead of implementation-defined std::hash.
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
    // Custom specializations omit this marker and retain element-wise range hashing.
    static constexpr bool is_identity = true;

    hash_t operator()(const T& el) const noexcept { return static_cast<hash_t>(el); }
};

template<Enumeration T>
struct Hash<T>
{
    static constexpr bool is_identity = requires { requires Hash<std::underlying_type_t<T>>::is_identity; };

    hash_t operator()(const T& el) const
        requires Hashable<std::underlying_type_t<T>>
    {
        return Hash<std::underlying_type_t<T>> {}(static_cast<std::underlying_type_t<T>>(el));
    }
};

template<>
struct Hash<void>
{
    using is_transparent = void;

    template<Hashable T>
    hash_t operator()(const T& el) const
    {
        return Hash<std::remove_cvref_t<T>> {}(el);
    }
};

template<std::floating_point T>
    requires std::same_as<T, float> || std::same_as<T, double>
struct Hash<T>
{
    hash_t operator()(const T& el) const noexcept
    {
        // EqualTo treats all NaNs as equal; Boost already normalizes signed zero.
        return std::isnan(el) ? hash_t { 0x9e3779b97f4a7c15ULL } : boost::hash<T> {}(el);
    }
};

template<>
struct Hash<std::string_view>
{
    hash_t operator()(std::string_view el) const noexcept { return hashing::hash_bytes(el.data(), el.size()); }
};

template<>
struct Hash<std::string>
{
    hash_t operator()(const std::string& el) const noexcept { return hashing::hash_bytes(el.data(), el.size()); }
};

template<typename T, size_t N>
struct Hash<std::array<T, N>>
{
    hash_t operator()(const std::array<T, N>& arr) const
        requires Hashable<T>
    {
        return ygg::hash_range(arr);
    }
};

template<typename T>
struct Hash<std::reference_wrapper<T>>
{
    hash_t operator()(const std::reference_wrapper<T>& ref) const
        requires Hashable<T>
    {
        return Hash<std::remove_cvref_t<T>> {}(ref.get());
    }
};

template<typename Key, typename Compare, typename Allocator>
struct Hash<std::set<Key, Compare, Allocator>>
{
    hash_t operator()(const std::set<Key, Compare, Allocator>& set) const
        requires Hashable<Key>
    {
        return ygg::hash_range(set);
    }
};

template<typename Key, typename T, typename Compare, typename Allocator>
struct Hash<std::map<Key, T, Compare, Allocator>>
{
    hash_t operator()(const std::map<Key, T, Compare, Allocator>& map) const
        requires Hashable<Key> && Hashable<T>
    {
        return ygg::hash_range(map);
    }
};

template<typename Key, typename Compare, typename Allocator>
struct Hash<gtl::btree_set<Key, Compare, Allocator>>
{
    hash_t operator()(const gtl::btree_set<Key, Compare, Allocator>& set) const
        requires Hashable<Key>
    {
        return ygg::hash_range(set);
    }
};

template<typename Key, typename T, typename Compare, typename Allocator>
struct Hash<gtl::btree_map<Key, T, Compare, Allocator>>
{
    hash_t operator()(const gtl::btree_map<Key, T, Compare, Allocator>& map) const
        requires Hashable<Key> && Hashable<T>
    {
        return ygg::hash_range(map);
    }
};

template<typename T, typename Allocator>
struct Hash<std::vector<T, Allocator>>
{
    hash_t operator()(const std::vector<T, Allocator>& vec) const
        requires Hashable<T>
    {
        return ygg::hash_range(vec);
    }
};

template<typename T1, typename T2>
struct Hash<std::pair<T1, T2>>
{
    hash_t operator()(const std::pair<T1, T2>& pair) const
        requires Hashable<T1> && Hashable<T2>
    {
        return ygg::hash_combine(pair.first, pair.second);
    }
};

template<typename... Ts>
struct Hash<std::tuple<Ts...>>
{
    hash_t operator()(const std::tuple<Ts...>& tuple) const
        requires(Hashable<Ts> && ...)
    {
        hash_t aggregated_hash = sizeof...(Ts);
        std::apply([&aggregated_hash](const Ts&... args) { (ygg::hash_combine(aggregated_hash, args), ...); }, tuple);
        return aggregated_hash;
    }
};

template<typename... Ts>
struct Hash<std::variant<Ts...>>
{
    hash_t operator()(const std::variant<Ts...>& variant) const
        requires(Hashable<Ts> && ...)
    {
        hash_t seed = variant.index();
        std::visit([&seed](const auto& arg) { ygg::hash_combine(seed, arg); }, variant);
        return seed;
    }
};

template<typename T>
struct Hash<std::optional<T>>
{
    hash_t operator()(const std::optional<T>& optional) const
        requires Hashable<T>
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
    hash_t operator()(const std::span<T, Extent>& span) const
        requires Hashable<T>
    {
        return ygg::hash_range(span);
    }
};

template<Identifiable T>
struct Hash<T>
{
    using is_transparent = void;

    hash_t operator()(const T& element) const
        requires Hashable<decltype(element.identifying_members())>
    {
        return ygg::hash_combine(element.identifying_members());
    }

    template<Hashable... Args>
    hash_t operator()(const std::tuple<Args...>& view) const
    {
        return ygg::hash_combine(view);
    }
};

/**
 * Definitions
 */

template<std::ranges::input_range Range>
    requires Hashable<std::ranges::range_value_t<Range>>
inline hash_t hash_range(Range&& range)
{
    hash_t seed = 0;
    if constexpr (std::ranges::sized_range<Range>)
        seed = std::ranges::size(range);

    for (const std::ranges::range_value_t<Range>& value : range)
        ygg::hash_combine(seed, value);

    return seed;
}

// Decoded and segmented ranges must hash identically to contiguous ranges of the same element type.
template<hashing::ByteHashableRange Range>
inline hash_t hash_range(Range&& range)
{
    auto hash = boost::hash2::xxhash_64 {};
    for (std::ranges::range_value_t<Range> value : range)
        hash.update(&value, sizeof(value));
    return hash.result();
}

template<hashing::ByteHashableRange Range>
    requires std::ranges::contiguous_range<Range> && (!std::is_volatile_v<std::remove_reference_t<std::ranges::range_reference_t<Range>>>)
inline hash_t hash_range(Range&& range) noexcept(noexcept(std::ranges::data(range)) && noexcept(std::ranges::size(range)))
{
    return hashing::hash_bytes(std::ranges::data(range), std::ranges::size(range) * sizeof(std::ranges::range_value_t<Range>));
}

template<Hashable T>
inline void hash_combine(hash_t& seed, const T& value)
{
    auto combined = static_cast<size_t>(seed);
    boost::hash_combine(combined, Hash<std::remove_cvref_t<T>> {}(value));
    seed = combined;
}

template<typename T, Hashable... Rest>
inline void hash_combine(hash_t& seed, const Rest&... rest)
{
    (ygg::hash_combine(seed, rest), ...);
}

template<Hashable... Ts>
inline hash_t hash_combine(const Ts&... rest)
{
    hash_t seed = 0;
    (ygg::hash_combine(seed, rest), ...);
    return seed;
}

}  // namespace ygg

#endif
