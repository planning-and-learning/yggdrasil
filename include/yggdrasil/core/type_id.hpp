/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_CORE_TYPE_ID_HPP_
#define YGG_CORE_TYPE_ID_HPP_

#include <cstdint>
#include <string_view>

namespace ygg
{

/// Identifies a type within a process. Not a portable file format: hashed ids depend on the compiler's spelling.
using TypeId = std::uint64_t;

/// FNV-1a over the compiler's spelling of T.
template<typename T>
consteval TypeId type_name_hash()
{
    constexpr std::string_view name = __PRETTY_FUNCTION__;
    auto hash = TypeId { 14695981039346656037ull };
    for (const auto c : name)
        hash = (hash ^ static_cast<unsigned char>(c)) * TypeId { 1099511628211ull };
    return hash;
}

}  // namespace ygg

#endif
