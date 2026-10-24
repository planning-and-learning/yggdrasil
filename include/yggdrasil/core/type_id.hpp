/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_CORE_TYPE_ID_HPP_
#define YGG_CORE_TYPE_ID_HPP_

#include <cstdint>

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

}  // namespace ygg

#endif
