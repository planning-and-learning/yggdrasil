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

#include <array>
#include <cmath>
#include <gtest/gtest.h>
#include <limits>
#include <tuple>
#include <yggdrasil/ids/index_mixins.hpp>
#include <yggdrasil/ids/uint_mixins.hpp>
#include <yggdrasil/semantics/comparison.hpp>
#include <yggdrasil/serialization/cista_equal_to.hpp>
#include <yggdrasil/serialization/cista_ordering.hpp>

namespace ygg
{

template<typename T>
void comparison_parent_adl_probe(const T&);

}  // namespace ygg

namespace comparison_fixture
{

struct Value : ygg::comparison::Mixin<Value>
{
    int value;

    constexpr explicit Value(int value) noexcept : value(value) {}

    constexpr auto identifying_members() const noexcept { return std::tie(value); }
};

template<typename T>
concept FindsParentYggByAdl = requires(const T& value) { comparison_parent_adl_probe(value); };

}  // namespace comparison_fixture

namespace ygg
{

struct ComparisonOwnedValue
{
    int value;

    constexpr explicit ComparisonOwnedValue(int value) noexcept : value(value) {}

    constexpr auto identifying_members() const noexcept { return std::tie(value); }
};

}  // namespace ygg

namespace ygg::tests
{

struct ComparisonIndex : ygg::IndexMixin<ComparisonIndex>
{
    using IndexMixin::IndexMixin;
};

struct ComparisonUint : ygg::FixedUintMixin<ComparisonUint>
{
    using FixedUintMixin::FixedUintMixin;
};

static_assert(comparison_fixture::Value(1) == comparison_fixture::Value(1));
static_assert(comparison_fixture::Value(1) != comparison_fixture::Value(2));
static_assert(comparison_fixture::Value(1) < comparison_fixture::Value(2));
static_assert(comparison_fixture::Value(1) <= comparison_fixture::Value(2));
static_assert(comparison_fixture::Value(2) > comparison_fixture::Value(1));
static_assert(comparison_fixture::Value(2) >= comparison_fixture::Value(1));
static_assert(ygg::Comparable<comparison_fixture::Value>);
static_assert(!comparison_fixture::FindsParentYggByAdl<comparison_fixture::Value>);

static_assert(ComparisonOwnedValue(1) == ComparisonOwnedValue(1));
static_assert(ComparisonOwnedValue(1) != ComparisonOwnedValue(2));
static_assert(ComparisonOwnedValue(1) < ComparisonOwnedValue(2));
static_assert(ComparisonOwnedValue(1) <= ComparisonOwnedValue(2));
static_assert(ComparisonOwnedValue(2) > ComparisonOwnedValue(1));
static_assert(ComparisonOwnedValue(2) >= ComparisonOwnedValue(1));

static_assert(ComparisonIndex(1) == ComparisonIndex(1));
static_assert(ComparisonIndex(1) != ComparisonIndex(2));
static_assert(ComparisonIndex(1) < ComparisonIndex(2));
static_assert(ComparisonIndex(1) <= ComparisonIndex(2));
static_assert(ComparisonIndex(2) > ComparisonIndex(1));
static_assert(ComparisonIndex(2) >= ComparisonIndex(1));

static_assert(ComparisonUint(1) == ComparisonUint(1));
static_assert(ComparisonUint(1) != ComparisonUint(2));
static_assert(ComparisonUint(1) < ComparisonUint(2));
static_assert(ComparisonUint(1) <= ComparisonUint(2));
static_assert(ComparisonUint(2) > ComparisonUint(1));
static_assert(ComparisonUint(2) >= ComparisonUint(1));

constexpr auto quiet_nan = std::numeric_limits<double>::quiet_NaN();
static_assert(ygg::EqualTo<double> {}(quiet_nan, quiet_nan));
static_assert(!ygg::EqualTo<double> {}(quiet_nan, 0.0));
static_assert(ygg::Less<double> {}(0.0, quiet_nan));
static_assert(!ygg::Less<double> {}(quiet_nan, 0.0));
static_assert(!ygg::Less<double> {}(quiet_nan, quiet_nan));

constexpr auto cista_pair_lhs = ::cista::pair<int, int> { 1, 2 };
constexpr auto cista_pair_rhs = ::cista::pair<int, int> { 1, 3 };
static_assert(ygg::EqualTo<::cista::pair<int, int>> {}(cista_pair_lhs, cista_pair_lhs));
static_assert(ygg::Less<::cista::pair<int, int>> {}(cista_pair_lhs, cista_pair_rhs));

using CistaVariant = ::cista::offset::variant<int, unsigned>;
constexpr auto empty_cista_variant = CistaVariant {};
static_assert(ygg::EqualTo<CistaVariant> {}(empty_cista_variant, empty_cista_variant));
static_assert(!ygg::Less<CistaVariant> {}(empty_cista_variant, empty_cista_variant));

TEST(YggdrasilTests, CommonFloatingPointComparisonPreservesRuntimeNaNSemantics)
{
    const auto values = std::array {
        -std::numeric_limits<double>::infinity(),    -1.0, -0.0, 0.0, 1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::signaling_NaN()
    };

    for (const auto lhs : values)
    {
        for (const auto rhs : values)
        {
            const auto lhs_nan = std::isnan(lhs);
            const auto rhs_nan = std::isnan(rhs);
            const auto expected_equal = lhs_nan || rhs_nan ? lhs_nan && rhs_nan : lhs == rhs;
            const auto expected_less = lhs_nan || rhs_nan ? !lhs_nan && rhs_nan : lhs < rhs;

            EXPECT_EQ(ygg::EqualTo<double> {}(lhs, rhs), expected_equal);
            EXPECT_EQ(ygg::Less<double> {}(lhs, rhs), expected_less);
        }
    }
}

}  // namespace ygg::tests
