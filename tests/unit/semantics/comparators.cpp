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
#include <compare>
#include <cstdint>
#include <functional>
#include <gtest/gtest.h>
#include <limits>
#include <map>
#include <ranges>
#include <set>
#include <span>
#include <tuple>
#include <variant>
#include <vector>
#include <yggdrasil/containers/associative_containers.hpp>
#include <yggdrasil/core/observer_ptr_ordering.hpp>
#include <yggdrasil/semantics/comparators.hpp>
#include <yggdrasil/semantics/comparison.hpp>
#include <yggdrasil/semantics/containers/block_array_ordering.hpp>
#include <yggdrasil/semantics/containers/dynamic_bitset_ordering.hpp>
#include <yggdrasil/semantics/containers/segmented_vector_ordering.hpp>
#include <yggdrasil/serialization/cista_ordering.hpp>

namespace ygg::tests
{

struct ComparatorContext
{
};

struct IdentifiableComparatorValue
{
    int first;
    int second;

    auto identifying_members() const noexcept { return std::tie(first, second); }
};

template<typename T>
concept OrderedByAllCommonPredicates = ygg::LessFor<ygg::Less<T>, T> && ygg::LessFor<ygg::LessEqual<T>, T> && ygg::LessFor<ygg::Greater<T>, T>
                                       && ygg::LessFor<ygg::GreaterEqual<T>, T> && requires(const T& lhs, const T& rhs) {
                                              { ygg::ThreeWayCompare<T> {}(lhs, rhs) } -> std::same_as<std::strong_ordering>;
                                          };

TEST(YggdrasilTests, CommonOrderingPredicatesCoverHashAndEqualToFamilies)
{
    using BlockView = ygg::BasicBlockArrayView<uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>>;
    using BitPackedView = ygg::BasicBitPackedArrayView<uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>>;
    using CistaVector = ::cista::offset::vector<int>;
    using CistaOptional = ::cista::optional<int>;
    using CistaVariant = ::cista::offset::variant<int, unsigned>;

    static_assert(OrderedByAllCommonPredicates<double>);
    static_assert(OrderedByAllCommonPredicates<IdentifiableComparatorValue>);
    static_assert(OrderedByAllCommonPredicates<std::array<int, 2>>);
    static_assert(OrderedByAllCommonPredicates<std::vector<int>>);
    static_assert(OrderedByAllCommonPredicates<std::set<int>>);
    static_assert(OrderedByAllCommonPredicates<std::map<int, int>>);
    static_assert(OrderedByAllCommonPredicates<ygg::Set<int>>);
    static_assert(OrderedByAllCommonPredicates<ygg::Map<int, int>>);
    static_assert(OrderedByAllCommonPredicates<std::pair<int, int>>);
    static_assert(OrderedByAllCommonPredicates<std::tuple<int, int>>);
    static_assert(OrderedByAllCommonPredicates<std::variant<int, unsigned>>);
    static_assert(OrderedByAllCommonPredicates<std::optional<int>>);
    static_assert(OrderedByAllCommonPredicates<std::reference_wrapper<int>>);
    static_assert(OrderedByAllCommonPredicates<std::span<const int>>);
    static_assert(OrderedByAllCommonPredicates<::cista::offset::string>);
    static_assert(OrderedByAllCommonPredicates<CistaVector>);
    static_assert(OrderedByAllCommonPredicates<ygg::View<CistaVector, ComparatorContext>>);
    static_assert(OrderedByAllCommonPredicates<CistaOptional>);
    static_assert(OrderedByAllCommonPredicates<ygg::View<CistaOptional, ComparatorContext>>);
    static_assert(OrderedByAllCommonPredicates<CistaVariant>);
    static_assert(OrderedByAllCommonPredicates<ygg::View<CistaVariant, ComparatorContext>>);
    static_assert(OrderedByAllCommonPredicates<boost::dynamic_bitset<>>);
    static_assert(OrderedByAllCommonPredicates<ygg::BitsetSpan<uint64_t>>);
    static_assert(OrderedByAllCommonPredicates<BlockView>);
    static_assert(OrderedByAllCommonPredicates<ygg::View<BlockView, ComparatorContext>>);
    static_assert(OrderedByAllCommonPredicates<BitPackedView>);
    static_assert(OrderedByAllCommonPredicates<ygg::View<BitPackedView, ComparatorContext>>);
    static_assert(OrderedByAllCommonPredicates<ygg::SegmentedVector<int, 2>>);
    static_assert(OrderedByAllCommonPredicates<ygg::ObserverPtr<const int>>);

    const auto lhs_value = 1;
    const auto rhs_value = 2;
    const auto lhs = ygg::make_observer(lhs_value);
    const auto rhs = ygg::make_observer(rhs_value);

    EXPECT_TRUE(ygg::Less<ygg::ObserverPtr<const int>> {}(lhs, rhs));
    EXPECT_TRUE(ygg::LessEqual<ygg::ObserverPtr<const int>> {}(lhs, rhs));
    EXPECT_TRUE(ygg::Greater<ygg::ObserverPtr<const int>> {}(rhs, lhs));
    EXPECT_TRUE(ygg::GreaterEqual<ygg::ObserverPtr<const int>> {}(rhs, lhs));
    EXPECT_FALSE(ygg::Greater<ygg::ObserverPtr<const int>> {}(lhs, rhs));
}

TEST(YggdrasilTests, CommonLessRangeMatchesContainerLess)
{
    const auto lhs = std::vector<int> { 1, 2, 3 };
    const auto rhs = std::vector<int> { 1, 2, 4 };

    EXPECT_TRUE(ygg::less_range(lhs, rhs));
    EXPECT_EQ(ygg::less_range(lhs, rhs), ygg::Less<std::vector<int>> {}(lhs, rhs));
    EXPECT_EQ(ygg::less_range(std::span<const int>(lhs), std::span<const int>(rhs)),
              ygg::Less<std::span<const int>> {}(std::span<const int>(lhs), std::span<const int>(rhs)));
}

TEST(YggdrasilTests, CommonLessOrdersNaNAfterNumbers)
{
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    const auto less = ygg::Less<double> {};

    EXPECT_TRUE(less(1.0, nan));
    EXPECT_FALSE(less(nan, 1.0));
    EXPECT_FALSE(less(nan, nan));
    EXPECT_TRUE(less(1.0, 2.0));

    const auto equivalent = [&](double lhs, double rhs) { return !less(lhs, rhs) && !less(rhs, lhs); };
    EXPECT_TRUE(equivalent(nan, nan));
    EXPECT_FALSE(equivalent(nan, 1.0));
}

TEST(YggdrasilTests, CommonLessRangeUsesElementLess)
{
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    const auto lhs = std::vector<double> { nan, 1.0 };
    const auto rhs = std::vector<double> { nan, 2.0 };

    EXPECT_TRUE(ygg::less_range(lhs, rhs));
    EXPECT_TRUE(ygg::Less<std::vector<double>> {}(lhs, rhs));
}

TEST(YggdrasilTests, CommonLessRangeOrdersPrefixes)
{
    const auto lhs = std::vector<int> { 1, 2 };
    const auto rhs = std::vector<int> { 1, 2, 3 };

    EXPECT_TRUE(ygg::less_range(lhs, rhs));
    EXPECT_FALSE(ygg::less_range(rhs, lhs));
}

TEST(YggdrasilTests, CommonLessRangeAcceptsRangeAdaptors)
{
    const auto values = std::vector<int> { 1, 2, 3, 4 };
    auto even_squares = values | std::views::filter([](int value) { return value % 2 == 0; }) | std::views::transform([](int value) { return value * value; });

    EXPECT_TRUE(ygg::less_range(even_squares, std::array<int, 2> { 4, 17 }));
    EXPECT_FALSE(ygg::less_range(even_squares, std::array<int, 2> { 4, 16 }));
}

TEST(YggdrasilTests, CommonLessOrdersVariantAlternativesByIndex)
{
    using Variant = std::variant<int, unsigned>;

    const auto first_alternative = Variant { 9 };
    const auto second_alternative = Variant { 0U };

    EXPECT_TRUE(ygg::Less<Variant> {}(first_alternative, second_alternative));
    EXPECT_FALSE(ygg::Less<Variant> {}(second_alternative, first_alternative));

    using DuplicateTypeVariant = std::variant<int, int>;
    const auto duplicate_first = DuplicateTypeVariant(std::in_place_index<0>, 9);
    const auto duplicate_second = DuplicateTypeVariant(std::in_place_index<1>, 0);

    EXPECT_TRUE(ygg::Less<DuplicateTypeVariant> {}(duplicate_first, duplicate_second));
    EXPECT_FALSE(ygg::Less<DuplicateTypeVariant> {}(duplicate_second, duplicate_first));
}

TEST(YggdrasilTests, CommonReferenceWrapperComparatorOrdersReferencedValues)
{
    auto lhs_value = 1;
    auto rhs_value = 2;

    const auto lhs = std::ref(lhs_value);
    const auto rhs = std::ref(rhs_value);

    EXPECT_TRUE(ygg::Less<std::reference_wrapper<int>> {}(lhs, rhs));
    EXPECT_FALSE(ygg::Less<std::reference_wrapper<int>> {}(rhs, lhs));
}

TEST(YggdrasilTests, CommonBlockArrayComparatorOrdersViews)
{
    auto lhs_storage = std::vector<uint8_t> { 1, 2 };
    auto rhs_storage = std::vector<uint8_t> { 1, 3 };

    using View = ygg::BasicBlockArrayView<uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>>;
    auto lhs = View(lhs_storage.data(), lhs_storage.size());
    auto rhs = View(rhs_storage.data(), rhs_storage.size());

    EXPECT_TRUE(ygg::Less<View> {}(lhs, rhs));
    EXPECT_FALSE(ygg::Less<View> {}(rhs, lhs));
}

TEST(YggdrasilTests, CommonBitPackedArrayComparatorOrdersViews)
{
    auto lhs_storage = std::vector<uint8_t>(1, 0);
    auto rhs_storage = std::vector<uint8_t>(1, 0);

    using View = ygg::BasicBitPackedArrayView<uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>>;
    auto lhs = View(lhs_storage.data(), 2, 3, 0);
    auto rhs = View(rhs_storage.data(), 2, 3, 0);

    const auto lhs_values = std::array<uint8_t, 2> { 1, 2 };
    const auto rhs_values = std::array<uint8_t, 2> { 1, 3 };
    lhs = std::span<const uint8_t>(lhs_values);
    rhs = std::span<const uint8_t>(rhs_values);

    EXPECT_TRUE(ygg::Less<View> {}(lhs, rhs));
    EXPECT_FALSE(ygg::Less<View> {}(rhs, lhs));
}

TEST(YggdrasilTests, CommonDynamicBitsetComparatorOrdersBoostDynamicBitsets)
{
    auto lhs = boost::dynamic_bitset<>(8);
    auto rhs = boost::dynamic_bitset<>(8);

    lhs.set(1);
    rhs.set(2);

    EXPECT_TRUE(ygg::Less<boost::dynamic_bitset<>> {}(lhs, rhs));
    EXPECT_FALSE(ygg::Less<boost::dynamic_bitset<>> {}(rhs, lhs));
}

TEST(YggdrasilTests, CommonDynamicBitsetComparatorOrdersBitsetSpans)
{
    auto lhs_blocks = std::vector<uint64_t>(ygg::BitsetSpan<uint64_t>::num_blocks(8), 0);
    auto rhs_blocks = std::vector<uint64_t>(ygg::BitsetSpan<uint64_t>::num_blocks(8), 0);

    auto lhs = ygg::BitsetSpan<uint64_t>(lhs_blocks.data(), 8);
    auto rhs = ygg::BitsetSpan<uint64_t>(rhs_blocks.data(), 8);

    lhs.set(1);
    rhs.set(2);

    EXPECT_TRUE(ygg::Less<ygg::BitsetSpan<uint64_t>> {}(lhs, rhs));
    EXPECT_FALSE(ygg::Less<ygg::BitsetSpan<uint64_t>> {}(rhs, lhs));
}

TEST(YggdrasilTests, CommonObserverPtrComparatorOrdersPointees)
{
    const auto lhs_value = 1;
    const auto rhs_value = 2;

    const auto lhs = ygg::make_observer(lhs_value);
    const auto rhs = ygg::make_observer(rhs_value);

    EXPECT_TRUE(ygg::Less<ygg::ObserverPtr<const int>> {}(lhs, rhs));
    EXPECT_FALSE(ygg::Less<ygg::ObserverPtr<const int>> {}(rhs, lhs));
}

TEST(YggdrasilTests, CommonSegmentedVectorComparatorOrdersValues)
{
    auto lhs = ygg::SegmentedVector<int, 2>();
    lhs.push_back(1);
    lhs.push_back(2);

    auto rhs = ygg::SegmentedVector<int, 2>();
    rhs.push_back(1);
    rhs.push_back(3);

    EXPECT_TRUE((ygg::Less<ygg::SegmentedVector<int, 2>> {}(lhs, rhs)));
    EXPECT_FALSE((ygg::Less<ygg::SegmentedVector<int, 2>> {}(rhs, lhs)));
}

TEST(YggdrasilTests, CommonStlComparatorsOrderAssociativeContainers)
{
    const auto lhs_set = std::set<int> { 1, 2 };
    const auto rhs_set = std::set<int> { 1, 3 };
    EXPECT_TRUE(ygg::Less<std::set<int>> {}(lhs_set, rhs_set));

    const auto lhs_map = std::map<int, int> { { 1, 2 } };
    const auto rhs_map = std::map<int, int> { { 1, 3 } };
    EXPECT_TRUE((ygg::Less<std::map<int, int>> {}(lhs_map, rhs_map)));
}

TEST(YggdrasilTests, CommonOrderedAssociativeContainerAliasesOrderValues)
{
    const auto lhs_set = ygg::Set<int> { 1, 2 };
    const auto rhs_set = ygg::Set<int> { 1, 3 };
    EXPECT_TRUE(ygg::Less<ygg::Set<int>> {}(lhs_set, rhs_set));
    EXPECT_FALSE(ygg::Less<ygg::Set<int>> {}(rhs_set, lhs_set));

    const auto lhs_map = ygg::Map<int, int> { { 1, 2 } };
    const auto rhs_map = ygg::Map<int, int> { { 1, 3 } };
    EXPECT_TRUE((ygg::Less<ygg::Map<int, int>> {}(lhs_map, rhs_map)));
    EXPECT_FALSE((ygg::Less<ygg::Map<int, int>> {}(rhs_map, lhs_map)));
}

TEST(YggdrasilTests, CommonCistaLessAdaptersOrderOffsetString)
{
    auto lhs = ::cista::offset::string {};
    lhs = "ab";

    auto rhs = ::cista::offset::string {};
    rhs = "ac";

    EXPECT_TRUE(ygg::Less<::cista::offset::string> {}(lhs, rhs));
    EXPECT_FALSE(ygg::Less<::cista::offset::string> {}(rhs, lhs));
}

TEST(YggdrasilTests, CommonCistaLessAdaptersOrderOffsetVector)
{
    auto lhs = ::cista::offset::vector<int> {};
    lhs.emplace_back(1);
    lhs.emplace_back(2);

    auto rhs = ::cista::offset::vector<int> {};
    rhs.emplace_back(1);
    rhs.emplace_back(3);

    EXPECT_TRUE(ygg::Less<::cista::offset::vector<int>> {}(lhs, rhs));
    EXPECT_FALSE(ygg::Less<::cista::offset::vector<int>> {}(rhs, lhs));
}

TEST(YggdrasilTests, CommonCistaLessAdaptersOrderOffsetVectorViews)
{
    auto lhs = ::cista::offset::vector<int> {};
    lhs.emplace_back(1);
    lhs.emplace_back(2);

    auto rhs = ::cista::offset::vector<int> {};
    rhs.emplace_back(1);
    rhs.emplace_back(3);

    const auto context = ComparatorContext {};
    using Vector = ::cista::offset::vector<int>;
    using VectorView = ygg::View<Vector, ComparatorContext>;

    EXPECT_TRUE(ygg::Less<VectorView> {}(VectorView(lhs, context), VectorView(rhs, context)));
    EXPECT_FALSE(ygg::Less<VectorView> {}(VectorView(rhs, context), VectorView(lhs, context)));
}

TEST(YggdrasilTests, CommonCistaLessAdaptersOrderOptionalViews)
{
    auto lhs = ::cista::optional<int> {};
    lhs = 2;

    auto rhs = ::cista::optional<int> {};
    rhs = 3;

    const auto context = ComparatorContext {};
    using OptionalView = ygg::View<::cista::optional<int>, ComparatorContext>;

    auto empty = ::cista::optional<int> {};

    EXPECT_TRUE(ygg::Less<::cista::optional<int>> {}(empty, lhs));
    EXPECT_FALSE(ygg::Less<::cista::optional<int>> {}(lhs, empty));
    EXPECT_TRUE(ygg::Less<OptionalView> {}(OptionalView(lhs, context), OptionalView(rhs, context)));
    EXPECT_FALSE(ygg::Less<OptionalView> {}(OptionalView(rhs, context), OptionalView(lhs, context)));
    EXPECT_TRUE(ygg::Less<OptionalView> {}(OptionalView(empty, context), OptionalView(lhs, context)));
    EXPECT_FALSE(ygg::Less<OptionalView> {}(OptionalView(lhs, context), OptionalView(empty, context)));
}

TEST(YggdrasilTests, CommonIdentifiableComparatorOrdersMembersAndTuples)
{
    const auto lhs = IdentifiableComparatorValue { 1, 2 };
    const auto rhs = IdentifiableComparatorValue { 1, 3 };
    const auto rhs_members = std::tie(rhs.first, rhs.second);

    EXPECT_TRUE(ygg::Less<IdentifiableComparatorValue> {}(lhs, rhs));
    EXPECT_TRUE(ygg::Less<IdentifiableComparatorValue> {}(lhs, rhs_members));
    EXPECT_FALSE(ygg::Less<IdentifiableComparatorValue> {}(rhs_members, lhs));
    EXPECT_TRUE(ygg::LessEqual<IdentifiableComparatorValue> {}(lhs, rhs_members));
    EXPECT_TRUE(ygg::Greater<IdentifiableComparatorValue> {}(rhs_members, lhs));
    EXPECT_TRUE(ygg::GreaterEqual<IdentifiableComparatorValue> {}(rhs_members, lhs));
    EXPECT_EQ(ygg::ThreeWayCompare<IdentifiableComparatorValue> {}(lhs, rhs_members), std::strong_ordering::less);
    EXPECT_EQ(ygg::ThreeWayCompare<void> {}(lhs, rhs_members), std::strong_ordering::less);
}

TEST(YggdrasilTests, CommonCistaLessAdaptersOrderVariantViews)
{
    using Variant = ::cista::offset::variant<int, unsigned>;

    auto lhs = Variant {};
    lhs = 2;

    auto rhs = Variant {};
    rhs = 3;

    auto different_alternative = Variant {};
    different_alternative = 0U;

    const auto context = ComparatorContext {};
    using VariantView = ygg::View<Variant, ComparatorContext>;

    EXPECT_TRUE(ygg::Less<Variant> {}(lhs, rhs));
    EXPECT_FALSE(ygg::Less<Variant> {}(rhs, lhs));
    EXPECT_TRUE(ygg::Less<Variant> {}(lhs, different_alternative));
    EXPECT_FALSE(ygg::Less<Variant> {}(different_alternative, lhs));

    auto invalid = Variant {};
    EXPECT_TRUE(ygg::Less<Variant> {}(invalid, lhs));
    EXPECT_FALSE(ygg::Less<Variant> {}(lhs, invalid));
    EXPECT_FALSE(ygg::Less<Variant> {}(invalid, Variant {}));

    using DuplicateTypeVariant = ::cista::offset::variant<int, int>;
    auto duplicate_first = DuplicateTypeVariant {};
    duplicate_first.emplace<0>(9);
    auto duplicate_second = DuplicateTypeVariant {};
    duplicate_second.emplace<1>(0);
    EXPECT_TRUE(ygg::Less<DuplicateTypeVariant> {}(duplicate_first, duplicate_second));
    EXPECT_FALSE(ygg::Less<DuplicateTypeVariant> {}(duplicate_second, duplicate_first));

    EXPECT_TRUE(ygg::Less<VariantView> {}(VariantView(lhs, context), VariantView(rhs, context)));
    EXPECT_FALSE(ygg::Less<VariantView> {}(VariantView(rhs, context), VariantView(lhs, context)));
    EXPECT_TRUE(ygg::Less<VariantView> {}(VariantView(lhs, context), VariantView(different_alternative, context)));
    EXPECT_FALSE(ygg::Less<VariantView> {}(VariantView(different_alternative, context), VariantView(lhs, context)));
    EXPECT_TRUE(ygg::Less<VariantView> {}(VariantView(invalid, context), VariantView(lhs, context)));
    EXPECT_FALSE(ygg::Less<VariantView> {}(VariantView(lhs, context), VariantView(invalid, context)));
    EXPECT_FALSE(ygg::Less<VariantView> {}(VariantView(invalid, context), VariantView(Variant {}, context)));
}

TEST(YggdrasilTests, CommonCistaLessAdaptersOrderPairAndNestedArrayViews)
{
    using Pair = ::cista::pair<int, ::cista::array<int, 2>>;
    using Array = ::cista::array<Pair, 2>;
    using PairView = ygg::View<Pair, ComparatorContext>;
    using ArrayView = ygg::View<Array, ComparatorContext>;

    static_assert(OrderedByAllCommonPredicates<Pair>);
    static_assert(OrderedByAllCommonPredicates<PairView>);
    static_assert(OrderedByAllCommonPredicates<ArrayView>);

    const auto context = ComparatorContext {};
    const auto lhs = Array { Pair { 1, { 2, 3 } }, Pair { 4, { 5, 6 } } };
    const auto rhs = Array { Pair { 1, { 2, 3 } }, Pair { 4, { 5, 7 } } };
    const auto lower_first = Pair { 3, { 9, 9 } };

    EXPECT_TRUE(ygg::Less<Pair> {}(lower_first, lhs[1]));
    EXPECT_TRUE(ygg::Less<Pair> {}(lhs[1], rhs[1]));
    EXPECT_FALSE(ygg::Less<Pair> {}(rhs[1], lhs[1]));
    EXPECT_TRUE(ygg::Less<PairView> {}(PairView(lhs[1], context), PairView(rhs[1], context)));
    EXPECT_TRUE(ygg::Less<ArrayView> {}(ArrayView(lhs, context), ArrayView(rhs, context)));
}

}  // namespace ygg::tests
