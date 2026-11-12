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

#ifndef YGG_CORE_UNSIGNED_INTEGER_HPP_
#define YGG_CORE_UNSIGNED_INTEGER_HPP_

#include "yggdrasil/core/config.hpp"

#include <compare>
#include <urd/elements/concepts.hpp>

namespace ygg
{
/// A nonnegative integer of its own type (a parameter's position, an FDR value): no index, so nothing resolves it. It
/// starts at 0, reads as its value (explicitly), and compares, hashes and serializes as it. Derive:
/// `struct ParameterIndex : ygg::UnsignedInteger<ParameterIndex> { using UnsignedInteger::UnsignedInteger; };`.
template<typename Derived>
struct UnsignedInteger
{
    using value_type = uint_t;

    value_type value = 0;

    constexpr UnsignedInteger() = default;
    constexpr explicit UnsignedInteger(value_type value_) noexcept : value(value_) {}

    constexpr explicit operator value_type() const noexcept { return value; }

    friend constexpr auto operator<=>(const UnsignedInteger&, const UnsignedInteger&) = default;
    static constexpr auto identifying_members() noexcept { return urd::Members<&UnsignedInteger::value> {}; }
    static constexpr auto cista_members() noexcept { return urd::Members<&UnsignedInteger::value> {}; }
};
}  // namespace ygg

#endif
