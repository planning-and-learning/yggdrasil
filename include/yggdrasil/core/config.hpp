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

#ifndef YGG_CORE_CONFIG_HPP_
#define YGG_CORE_CONFIG_HPP_

#ifndef YGG_ENABLE_FMT_FORMATTERS
#define YGG_ENABLE_FMT_FORMATTERS 1
#endif

/// Keeps a rarely taken helper out of line, so its size does not count towards the inlined size of
/// its caller. Without this, a hot caller whose body is small can exceed the inliner's single-call
/// budget purely because of a cold path spliced into it.
#if defined(_MSC_VER)
#define YGG_NOINLINE __declspec(noinline)
#else
#define YGG_NOINLINE [[gnu::noinline]]
#endif

#include "yggdrasil/core/dependent_false.hpp"

#include <algorithm>
#include <cassert>
#include <cista/mode.h>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>

namespace ygg
{
using int_t = std::int32_t;
using uint_t = std::uint32_t;
using hash_t = std::uint64_t;
using float_t = double;

inline uint_t to_uint_t(size_t value)
{
    if (value > std::numeric_limits<uint_t>::max())
        throw std::overflow_error("Value does not fit into uint_t.");

    return static_cast<uint_t>(value);
}

template<typename T>
struct FloatTolerance
{
    static_assert(dependent_false<T>::value, "FloatTolerance<T> is not defined for this type.");
};

template<>
struct FloatTolerance<float_t>
{
    static constexpr float_t abs_epsilon = static_cast<float_t>(1e-12);
    static constexpr float_t rel_epsilon = static_cast<float_t>(1e-12);

    static_assert(abs_epsilon > std::numeric_limits<float_t>::epsilon(), "Absolute float tolerance is too small.");
    static_assert(rel_epsilon > std::numeric_limits<float_t>::epsilon(), "Relative float tolerance is too small.");
    static_assert(abs_epsilon < static_cast<float_t>(1), "Absolute float tolerance is too large.");
    static_assert(rel_epsilon < static_cast<float_t>(1), "Relative float tolerance is too large.");

    static constexpr float_t tolerance(float_t lhs, float_t rhs) noexcept
    {
        return std::max(abs_epsilon, rel_epsilon * std::max(std::abs(lhs), std::abs(rhs)));
    }

    static constexpr float_t canonicalize(float_t value) noexcept
    {
        if (std::isnan(value) || std::isinf(value))
            return value;

        return std::round(value / abs_epsilon) * abs_epsilon;
    }
};

#ifdef NDEBUG
static constexpr ::cista::mode CISTA_MODE = ::cista::mode::UNCHECKED;
#else
static constexpr ::cista::mode CISTA_MODE = ::cista::mode::NONE;
#endif
}  // namespace ygg

#endif
