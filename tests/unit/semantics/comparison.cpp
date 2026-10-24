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

#include "yggdrasil/semantics/comparison.hpp"

#include "yggdrasil/core/types.hpp"
#include "yggdrasil/ids/uint_mixins.hpp"
#include "yggdrasil/semantics/hash.hpp"
#include "yggdrasil/serialization/cista_equal_to.hpp"
#include "yggdrasil/serialization/cista_ordering.hpp"

#include <array>
#include <cmath>
#include <compare>
#include <gtest/gtest.h>
#include <limits>
#include <stdexcept>
#include <tuple>

namespace ygg
{

template<typename T>
void comparison_parent_adl_probe(const T&);

}  // namespace ygg

namespace comparison_fixture
{

namespace
{
struct Value : ygg::comparison::Mixin<Value>
{
    int value;

    constexpr explicit Value(int value) noexcept : value(value) {}

    static constexpr auto identifying_members() noexcept { return ygg::Members<&Value::value> {}; }
};
}  // namespace

template<typename T>
concept FindsParentYggByAdl = requires(const T& value) { comparison_parent_adl_probe(value); };

}  // namespace comparison_fixture

namespace ygg
{

namespace
{
struct ComparisonOwnedValue
{
    int value;

    constexpr explicit ComparisonOwnedValue(int value) noexcept : value(value) {}

    static constexpr auto identifying_members() noexcept { return ygg::Members<&ComparisonOwnedValue::value> {}; }
};
}  // namespace

}  // namespace ygg

namespace ygg::tests
{

struct ComparisonTag;
using ComparisonIndex = ygg::Index<ComparisonTag>;

namespace
{
struct ComparisonUint : ygg::FixedUintMixin<ComparisonUint>
{
    using FixedUintMixin::FixedUintMixin;
};
}  // namespace

static_assert(comparison_fixture::Value(1) == comparison_fixture::Value(1));
static_assert(comparison_fixture::Value(1) != comparison_fixture::Value(2));
static_assert(comparison_fixture::Value(1) < comparison_fixture::Value(2));
static_assert(comparison_fixture::Value(1) <= comparison_fixture::Value(2));
static_assert(comparison_fixture::Value(2) > comparison_fixture::Value(1));
static_assert(comparison_fixture::Value(2) >= comparison_fixture::Value(1));
static_assert((comparison_fixture::Value(1) <=> comparison_fixture::Value(2)) == std::strong_ordering::less);
static_assert(ygg::Comparable<comparison_fixture::Value>);
static_assert(!comparison_fixture::FindsParentYggByAdl<comparison_fixture::Value>);

static_assert(ComparisonOwnedValue(1) == ComparisonOwnedValue(1));
static_assert(ComparisonOwnedValue(1) != ComparisonOwnedValue(2));
static_assert(ComparisonOwnedValue(1) < ComparisonOwnedValue(2));
static_assert(ComparisonOwnedValue(1) <= ComparisonOwnedValue(2));
static_assert(ComparisonOwnedValue(2) > ComparisonOwnedValue(1));
static_assert(ComparisonOwnedValue(2) >= ComparisonOwnedValue(1));
static_assert((ComparisonOwnedValue(1) <=> ComparisonOwnedValue(2)) == std::strong_ordering::less);

static_assert(ComparisonIndex(1) == ComparisonIndex(1));
static_assert(ComparisonIndex(1) != ComparisonIndex(2));
static_assert(ComparisonIndex(1) < ComparisonIndex(2));
static_assert(ComparisonIndex(1) <= ComparisonIndex(2));
static_assert(ComparisonIndex(2) > ComparisonIndex(1));
static_assert(ComparisonIndex(2) >= ComparisonIndex(1));
static_assert((ComparisonIndex(1) <=> ComparisonIndex(2)) == std::strong_ordering::less);

static_assert(ComparisonUint(1) == ComparisonUint(1));
static_assert(ComparisonUint(1) != ComparisonUint(2));
static_assert(ComparisonUint(1) < ComparisonUint(2));
static_assert(ComparisonUint(1) <= ComparisonUint(2));
static_assert(ComparisonUint(2) > ComparisonUint(1));
static_assert(ComparisonUint(2) >= ComparisonUint(1));
static_assert((ComparisonUint(1) <=> ComparisonUint(2)) == std::strong_ordering::less);

constexpr auto quiet_nan = std::numeric_limits<double>::quiet_NaN();
static_assert(ygg::EqualTo<double> {}(quiet_nan, quiet_nan));
static_assert(!ygg::EqualTo<double> {}(quiet_nan, 0.0));
static_assert(ygg::Less<double> {}(0.0, quiet_nan));
static_assert(!ygg::Less<double> {}(quiet_nan, 0.0));
static_assert(!ygg::Less<double> {}(quiet_nan, quiet_nan));
static_assert(ygg::ThreeWayCompare<double> {}(0.0, quiet_nan) == std::strong_ordering::less);
static_assert(ygg::ThreeWayCompare<double> {}(quiet_nan, quiet_nan) == std::strong_ordering::equal);

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

namespace
{
struct ThrowingIdentifiableComparison : ygg::comparison::Mixin<ThrowingIdentifiableComparison>
{
    [[nodiscard]] auto identifying_members() const -> std::tuple<int> { throw std::runtime_error("identity"); }
};
}  // namespace

TEST(YggdrasilTests, CommonComparisonMixinPropagatesIdentityFailures)
{
    const auto value = ThrowingIdentifiableComparison {};
    static_assert(!noexcept(value <=> value));
    EXPECT_THROW((value <=> value), std::runtime_error);
}

/// A class template, so the member pointers are formed while the class is still being instantiated.
namespace
{
template<typename Tag>
struct ListedRecord
{
    int index;
    int name;
    double weight;

    static constexpr auto identifying_members() noexcept { return ygg::Members<&ListedRecord::name, &ListedRecord::weight> {}; }
};

struct TiedRecord
{
    int name;
    double weight;

    [[nodiscard]] auto identifying_members() const noexcept { return std::tie(name, weight); }
};

struct EmptyListedRecord
{
    int index;

    static constexpr auto identifying_members() noexcept { return ygg::Members<> {}; }
};
}  // namespace

TEST(YggdrasilTests, MemberListsIdentifyLikeTiedMembers)
{
    using Listed = ListedRecord<struct ListedTag>;
    static_assert(MemberIdentified<Listed> && !FunctionIdentified<Listed> && Identifiable<Listed>);
    static_assert(FunctionIdentified<TiedRecord> && !MemberIdentified<TiedRecord>);
    static_assert(std::same_as<IdentifyingMember<Listed, 0>, int> && std::same_as<IdentifyingMember<Listed, 1>, double>);

    // The index is not an identifying member.
    const auto a = Listed { 0, 1, 2.5 };
    const auto b = Listed { 7, 1, 2.5 };
    const auto c = Listed { 0, 1, 3.5 };
    const auto d = Listed { 0, 2, 0.5 };
    EXPECT_TRUE(EqualTo<Listed> {}(a, b));
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(EqualTo<Listed> {}(a, c));
    EXPECT_EQ(Hash<Listed> {}(a), Hash<Listed> {}(b));
    // Members are ordered in list order, like the tuple of tied members.
    for (const auto& [lhs, rhs] : { std::pair { a, c }, std::pair { c, a }, std::pair { a, d }, std::pair { d, c }, std::pair { a, b } })
    {
        const auto tied_lhs = TiedRecord { lhs.name, lhs.weight };
        const auto tied_rhs = TiedRecord { rhs.name, rhs.weight };
        EXPECT_EQ(Less<Listed> {}(lhs, rhs), Less<TiedRecord> {}(tied_lhs, tied_rhs));
        EXPECT_EQ(lhs <=> rhs, tied_lhs <=> tied_rhs);
    }

    EXPECT_TRUE(EqualTo<EmptyListedRecord> {}(EmptyListedRecord { 1 }, EmptyListedRecord { 2 }));
    EXPECT_FALSE(Less<EmptyListedRecord> {}(EmptyListedRecord { 1 }, EmptyListedRecord { 2 }));
    EXPECT_EQ(Hash<EmptyListedRecord> {}(EmptyListedRecord { 1 }), Hash<EmptyListedRecord> {}(EmptyListedRecord { 2 }));
}

}  // namespace ygg::tests
