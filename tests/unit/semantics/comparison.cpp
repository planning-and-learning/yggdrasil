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


#include <urd/elements/flat.hpp>

#include <cista/containers/pair.h>
#include <cista/containers/variant.h>

#include <array>
#include <cmath>
#include <compare>
#include <gtest/gtest.h>
#include <limits>
#include <stdexcept>
#include <tuple>
#include <urd/elements/equal_to.hpp>
#include <urd/elements/hash.hpp>
#include <urd/elements/less.hpp>

namespace ygg
{

template<typename T>
void comparison_parent_adl_probe(const T&);

}  // namespace ygg

namespace comparison_fixture
{

namespace
{
struct Value : urd::compared_by_identity<Value>
{
    int value;

    constexpr explicit Value(int value_) noexcept : value(value_) {}

    static constexpr auto identifying_members() noexcept { return urd::Members<&Value::value> {}; }
};
}  // namespace

template<typename T>
concept FindsParentYggByAdl = requires(const T& value) { comparison_parent_adl_probe(value); };

}  // namespace comparison_fixture

namespace ygg::tests
{

struct ComparisonTag;
using ComparisonIndex = urd::index<ComparisonTag>;

static_assert(comparison_fixture::Value(1) == comparison_fixture::Value(1));
static_assert(comparison_fixture::Value(1) != comparison_fixture::Value(2));
static_assert(comparison_fixture::Value(1) < comparison_fixture::Value(2));
static_assert(comparison_fixture::Value(1) <= comparison_fixture::Value(2));
static_assert(comparison_fixture::Value(2) > comparison_fixture::Value(1));
static_assert(comparison_fixture::Value(2) >= comparison_fixture::Value(1));
static_assert((comparison_fixture::Value(1) <=> comparison_fixture::Value(2)) == std::strong_ordering::less);
static_assert(std::three_way_comparable<comparison_fixture::Value, std::strong_ordering>);
static_assert(!comparison_fixture::FindsParentYggByAdl<comparison_fixture::Value>);

static_assert(ComparisonIndex(1) == ComparisonIndex(1));
static_assert(ComparisonIndex(1) != ComparisonIndex(2));
static_assert(ComparisonIndex(1) < ComparisonIndex(2));
static_assert(ComparisonIndex(1) <= ComparisonIndex(2));
static_assert(ComparisonIndex(2) > ComparisonIndex(1));
static_assert(ComparisonIndex(2) >= ComparisonIndex(1));
static_assert((ComparisonIndex(1) <=> ComparisonIndex(2)) == std::strong_ordering::less);

constexpr auto quiet_nan = std::numeric_limits<double>::quiet_NaN();
static_assert(urd::EqualTo<double> {}(quiet_nan, quiet_nan));
static_assert(!urd::EqualTo<double> {}(quiet_nan, 0.0));
static_assert(urd::Less<double> {}(0.0, quiet_nan));
static_assert(!urd::Less<double> {}(quiet_nan, 0.0));
static_assert(!urd::Less<double> {}(quiet_nan, quiet_nan));
static_assert(urd::ThreeWayCompare<double> {}(0.0, quiet_nan) == std::strong_ordering::less);
static_assert(urd::ThreeWayCompare<double> {}(quiet_nan, quiet_nan) == std::strong_ordering::equal);

constexpr auto cista_pair_lhs = ::cista::pair<int, int> { 1, 2 };
constexpr auto cista_pair_rhs = ::cista::pair<int, int> { 1, 3 };
static_assert(urd::EqualTo<::cista::pair<int, int>> {}(cista_pair_lhs, cista_pair_lhs));
static_assert(urd::Less<::cista::pair<int, int>> {}(cista_pair_lhs, cista_pair_rhs));

using CistaVariant = ::cista::offset::variant<int, unsigned>;
constexpr auto empty_cista_variant = CistaVariant {};
static_assert(urd::EqualTo<CistaVariant> {}(empty_cista_variant, empty_cista_variant));
static_assert(!urd::Less<CistaVariant> {}(empty_cista_variant, empty_cista_variant));

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

            EXPECT_EQ(urd::EqualTo<double> {}(lhs, rhs), expected_equal);
            EXPECT_EQ(urd::Less<double> {}(lhs, rhs), expected_less);
        }
    }
}

namespace
{
struct ThrowingIdentifiableComparison : urd::compared_by_identity<ThrowingIdentifiableComparison>
{
    [[nodiscard]] int identity() const { throw std::runtime_error("identity"); }
    static constexpr auto identifying_members() noexcept { return urd::Members<&ThrowingIdentifiableComparison::identity> {}; }
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

    static constexpr auto identifying_members() noexcept { return urd::Members<&ListedRecord::name, &ListedRecord::weight> {}; }
};

/// Identified through getters: as the record listing its data members.
struct TiedRecord
{
    int name;
    double weight;

    [[nodiscard]] int get_name() const noexcept { return name; }
    [[nodiscard]] double get_weight() const noexcept { return weight; }
    static constexpr auto identifying_members() noexcept { return urd::Members<&TiedRecord::get_name, &TiedRecord::get_weight> {}; }
};

struct EmptyListedRecord
{
    int index;

    static constexpr auto identifying_members() noexcept { return urd::Members<> {}; }
};
}  // namespace

TEST(YggdrasilTests, MemberListsIdentifyLikeTiedMembers)
{
    using Listed = ListedRecord<struct ListedTag>;
    static_assert(urd::Identifiable<Listed> && urd::Identifiable<TiedRecord>);
    static_assert(std::same_as<std::remove_cvref_t<std::tuple_element_t<0, urd::detail::identity_t<TiedRecord>>>, int> && std::same_as<std::remove_cvref_t<std::tuple_element_t<1, urd::detail::identity_t<TiedRecord>>>, double>);
    static_assert(std::same_as<std::remove_cvref_t<std::tuple_element_t<0, urd::detail::identity_t<Listed>>>, int> && std::same_as<std::remove_cvref_t<std::tuple_element_t<1, urd::detail::identity_t<Listed>>>, double>);

    // The index is not an identifying member.
    const auto a = Listed { 0, 1, 2.5 };
    const auto b = Listed { 7, 1, 2.5 };
    const auto c = Listed { 0, 1, 3.5 };
    const auto d = Listed { 0, 2, 0.5 };
    EXPECT_TRUE(urd::EqualTo<Listed> {}(a, b));
    EXPECT_FALSE(urd::EqualTo<Listed> {}(a, c));
    EXPECT_EQ(urd::Hash<Listed> {}(a), urd::Hash<Listed> {}(b));
    // Members are ordered in list order, like the tuple of tied members.
    for (const auto& [lhs, rhs] : { std::pair { a, c }, std::pair { c, a }, std::pair { a, d }, std::pair { d, c }, std::pair { a, b } })
    {
        const auto tied_lhs = TiedRecord { lhs.name, lhs.weight };
        const auto tied_rhs = TiedRecord { rhs.name, rhs.weight };
        EXPECT_EQ(urd::Less<Listed> {}(lhs, rhs), urd::Less<TiedRecord> {}(tied_lhs, tied_rhs));
        EXPECT_EQ(urd::ThreeWayCompare<Listed> {}(lhs, rhs), urd::ThreeWayCompare<TiedRecord> {}(tied_lhs, tied_rhs));
    }

    EXPECT_TRUE(urd::EqualTo<EmptyListedRecord> {}(EmptyListedRecord { 1 }, EmptyListedRecord { 2 }));
    EXPECT_FALSE(urd::Less<EmptyListedRecord> {}(EmptyListedRecord { 1 }, EmptyListedRecord { 2 }));
    EXPECT_EQ(urd::Hash<EmptyListedRecord> {}(EmptyListedRecord { 1 }), urd::Hash<EmptyListedRecord> {}(EmptyListedRecord { 2 }));
}

}  // namespace ygg::tests
