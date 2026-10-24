/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_CORE_BYTES_HPP_
#define YGG_CORE_BYTES_HPP_

#include "yggdrasil/core/concepts.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstring>
#include <memory>
#include <span>
#include <stdexcept>

namespace ygg
{
/// Load a valid native representation of T from exactly sizeof(T) bytes.
/// No alignment or default constructor is required; no byte-order conversion is performed.
/// The fixed-width overload has nothing to check; the other one checks the width at run time.
template<TriviallyCopyable T>
T load_unaligned(std::span<const std::byte, sizeof(T)> bytes) noexcept
{
    auto representation = std::array<std::byte, sizeof(T)> {};
    std::memcpy(representation.data(), bytes.data(), sizeof(T));
    return std::bit_cast<T>(representation);
}

template<TriviallyCopyable T>
T load_unaligned(std::span<const std::byte> bytes)
{
    if (bytes.size() != sizeof(T))
        throw std::invalid_argument("load_unaligned: incorrect byte width.");
    return load_unaligned<T>(bytes.first<sizeof(T)>());
}

/// Copy the native representation into exactly sizeof(T) bytes, without canonicalization.
/// The destination needs no alignment and must not overlap value.
template<TriviallyCopyable T>
void store_unaligned(std::span<std::byte> bytes, const T& value)
{
    if (bytes.size() != sizeof(T))
        throw std::invalid_argument("store_unaligned: incorrect byte width.");
    std::memcpy(bytes.data(), std::addressof(value), sizeof(T));
}
}  // namespace ygg

#endif
