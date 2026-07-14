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
#include <cstdint>
#include <functional>
#include <gtest/gtest.h>
#include <limits>
#include <ranges>
#include <span>
#include <variant>
#include <vector>
#include <yggdrasil/containers/associative_containers.hpp>
#include <yggdrasil/containers/block_array_equal_to.hpp>
#include <yggdrasil/containers/dynamic_bitset_equal_to.hpp>
#include <yggdrasil/containers/raw_vector_equal_to.hpp>
#include <yggdrasil/containers/segmented_vector_equal_to.hpp>
#include <yggdrasil/core/observer_ptr_equal_to.hpp>
#include <yggdrasil/semantics/comparators.hpp>
#include <yggdrasil/semantics/equal_to.hpp>
#include <yggdrasil/serialization/cista_equal_to.hpp>

namespace ygg::tests
{

struct EqualToContext
{
};

TEST(YggdrasilTests, CommonEqualToAdaptersTreatFloatingPointNaNsAsEqual)
{
    const auto first_nan = std::numeric_limits<double>::quiet_NaN();
    const auto second_nan = std::numeric_limits<double>::signaling_NaN();

    EXPECT_TRUE(ygg::EqualTo<double> {}(first_nan, second_nan));
    EXPECT_FALSE(ygg::EqualTo<double> {}(first_nan, 0.0));
    EXPECT_TRUE(ygg::EqualTo<double> {}(1.0, 1.0));
}

TEST(YggdrasilTests, CommonEqualRangeMatchesContainerEqualTo)
{
    const auto lhs = std::vector<int> { 1, 2, 3 };
    const auto rhs = std::vector<int> { 1, 2, 3 };

    EXPECT_TRUE(ygg::equal_range(lhs, rhs));
    EXPECT_EQ(ygg::equal_range(lhs, rhs), ygg::EqualTo<std::vector<int>> {}(lhs, rhs));
    EXPECT_EQ(ygg::equal_range(std::span<const int>(lhs), std::span<const int>(rhs)),
              ygg::EqualTo<std::span<const int>> {}(std::span<const int>(lhs), std::span<const int>(rhs)));
}

TEST(YggdrasilTests, CommonEqualRangeUsesElementEqualTo)
{
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    const auto lhs = std::vector<double> { nan };
    const auto rhs = std::vector<double> { nan };

    EXPECT_TRUE(ygg::equal_range(lhs, rhs));
    EXPECT_TRUE(ygg::EqualTo<std::vector<double>> {}(lhs, rhs));
}

TEST(YggdrasilTests, CommonEqualRangeChecksSize)
{
    const auto lhs = std::vector<int> { 1, 2 };
    const auto rhs = std::vector<int> { 1, 2, 3 };

    EXPECT_FALSE(ygg::equal_range(lhs, rhs));
}

TEST(YggdrasilTests, CommonEqualRangeAcceptsRangeAdaptors)
{
    const auto values = std::vector<int> { 1, 2, 3, 4 };
    auto even_squares = values | std::views::filter([](int value) { return value % 2 == 0; }) | std::views::transform([](int value) { return value * value; });

    EXPECT_TRUE(ygg::equal_range(even_squares, std::array<int, 2> { 4, 16 }));
    EXPECT_FALSE(ygg::equal_range(even_squares, std::array<int, 2> { 4, 9 }));
}

TEST(YggdrasilTests, CommonEqualToAdaptersCompareVariantAlternativeIndex)
{
    using Variant = std::variant<int, unsigned>;

    const auto lhs = Variant { 9 };
    const auto rhs = Variant { 9 };
    const auto different_value = Variant { 10 };
    const auto different_type = Variant { 9U };

    EXPECT_TRUE(ygg::EqualTo<Variant> {}(lhs, rhs));
    EXPECT_FALSE(ygg::EqualTo<Variant> {}(lhs, different_value));
    EXPECT_FALSE(ygg::EqualTo<Variant> {}(lhs, different_type));

    using DuplicateTypeVariant = std::variant<int, int>;
    const auto first_alternative = DuplicateTypeVariant(std::in_place_index<0>, 9);
    const auto second_alternative = DuplicateTypeVariant(std::in_place_index<1>, 9);
    EXPECT_FALSE(ygg::EqualTo<DuplicateTypeVariant> {}(first_alternative, second_alternative));
}

TEST(YggdrasilTests, CommonReferenceWrapperEqualToAdaptersCompareReferencedValues)
{
    auto lhs_value = 7;
    auto rhs_value = 7;
    auto different_value = 8;

    const auto lhs = std::ref(lhs_value);
    const auto rhs = std::ref(rhs_value);
    const auto different = std::ref(different_value);

    EXPECT_TRUE(ygg::EqualTo<std::reference_wrapper<int>> {}(lhs, rhs));
    EXPECT_FALSE(ygg::EqualTo<std::reference_wrapper<int>> {}(lhs, different));
}

TEST(YggdrasilTests, CommonCistaEqualToAdaptersCompareOffsetVector)
{
    auto lhs = ::cista::offset::vector<int> {};
    lhs.emplace_back(1);
    lhs.emplace_back(2);

    auto rhs = ::cista::offset::vector<int> {};
    rhs.emplace_back(1);
    rhs.emplace_back(2);

    EXPECT_TRUE(ygg::EqualTo<::cista::offset::vector<int>> {}(lhs, rhs));
}

TEST(YggdrasilTests, CommonCistaEqualToAdaptersCompareOffsetStringOptionalAndVariant)
{
    auto lhs_string = ::cista::offset::string {};
    auto rhs_string = ::cista::offset::string {};
    auto different_string = ::cista::offset::string {};
    lhs_string = "alpha";
    rhs_string = "alpha";
    different_string = "beta";
    EXPECT_TRUE(ygg::EqualTo<::cista::offset::string> {}(lhs_string, rhs_string));
    EXPECT_FALSE(ygg::EqualTo<::cista::offset::string> {}(lhs_string, different_string));

    auto lhs_optional = ::cista::optional<int> { 7 };
    auto rhs_optional = ::cista::optional<int> { 7 };
    auto different_optional = ::cista::optional<int> { 8 };
    auto empty_optional = ::cista::optional<int> {};
    EXPECT_TRUE(ygg::EqualTo<::cista::optional<int>> {}(lhs_optional, rhs_optional));
    EXPECT_FALSE(ygg::EqualTo<::cista::optional<int>> {}(lhs_optional, different_optional));
    EXPECT_FALSE(ygg::EqualTo<::cista::optional<int>> {}(lhs_optional, empty_optional));

    using Variant = ::cista::offset::variant<int, unsigned>;
    auto lhs_variant = Variant { 9 };
    auto rhs_variant = Variant { 9 };
    auto different_value_variant = Variant { 10 };
    auto different_type_variant = Variant { 9U };
    EXPECT_TRUE(ygg::EqualTo<Variant> {}(lhs_variant, rhs_variant));
    EXPECT_FALSE(ygg::EqualTo<Variant> {}(lhs_variant, different_value_variant));
    EXPECT_FALSE(ygg::EqualTo<Variant> {}(lhs_variant, different_type_variant));
    EXPECT_TRUE(ygg::EqualTo<Variant> {}(Variant {}, Variant {}));
    EXPECT_FALSE(ygg::EqualTo<Variant> {}(Variant {}, lhs_variant));

    using DuplicateTypeVariant = ::cista::offset::variant<int, int>;
    auto duplicate_first = DuplicateTypeVariant {};
    duplicate_first.emplace<0>(9);
    auto duplicate_second = DuplicateTypeVariant {};
    duplicate_second.emplace<1>(9);
    EXPECT_FALSE(ygg::EqualTo<DuplicateTypeVariant> {}(duplicate_first, duplicate_second));
}

TEST(YggdrasilTests, CommonObserverPtrEqualToAdaptersComparePointees)
{
    const auto lhs_value = 7;
    const auto rhs_value = 7;
    const auto lhs = ygg::make_observer(lhs_value);
    const auto rhs = ygg::make_observer(rhs_value);

    EXPECT_TRUE(ygg::EqualTo<ygg::ObserverPtr<const int>> {}(lhs, rhs));
}

TEST(YggdrasilTests, CommonEqualToAdaptersCompareOrderedAssociativeAliases)
{
    const auto lhs_set = ygg::Set<int> { 1, 2 };
    const auto rhs_set = ygg::Set<int> { 1, 2 };
    const auto different_set = ygg::Set<int> { 1, 3 };
    EXPECT_TRUE(ygg::EqualTo<ygg::Set<int>> {}(lhs_set, rhs_set));
    EXPECT_FALSE(ygg::EqualTo<ygg::Set<int>> {}(lhs_set, different_set));

    const auto lhs_map = ygg::Map<int, int> { { 1, 2 } };
    const auto rhs_map = ygg::Map<int, int> { { 1, 2 } };
    const auto different_map = ygg::Map<int, int> { { 1, 3 } };
    EXPECT_TRUE((ygg::EqualTo<ygg::Map<int, int>> {}(lhs_map, rhs_map)));
    EXPECT_FALSE((ygg::EqualTo<ygg::Map<int, int>> {}(lhs_map, different_map)));
}

TEST(YggdrasilTests, CommonDynamicBitsetEqualToAdaptersCompareBoostDynamicBitsets)
{
    auto lhs = boost::dynamic_bitset<>(8);
    auto rhs = boost::dynamic_bitset<>(8);

    lhs.set(1);
    rhs.set(1);

    EXPECT_TRUE(ygg::EqualTo<boost::dynamic_bitset<>> {}(lhs, rhs));

    rhs.set(2);

    EXPECT_FALSE(ygg::EqualTo<boost::dynamic_bitset<>> {}(lhs, rhs));
}

TEST(YggdrasilTests, CommonDynamicBitsetEqualToAdaptersCompareBitsetSpans)
{
    const auto lhs_blocks = std::vector<std::uint64_t> { 0b1010 };
    const auto rhs_blocks = std::vector<std::uint64_t> { 0b1010 };
    const auto different_blocks = std::vector<std::uint64_t> { 0b0010 };

    const auto lhs = ygg::BitsetSpan<const std::uint64_t>(lhs_blocks.data(), 4);
    const auto rhs = ygg::BitsetSpan<const std::uint64_t>(rhs_blocks.data(), 4);
    const auto different = ygg::BitsetSpan<const std::uint64_t>(different_blocks.data(), 4);

    EXPECT_TRUE(ygg::EqualTo<ygg::BitsetSpan<const std::uint64_t>> {}(lhs, rhs));
    EXPECT_FALSE(ygg::EqualTo<ygg::BitsetSpan<const std::uint64_t>> {}(lhs, different));
}

TEST(YggdrasilTests, CommonCistaEqualToAdaptersCompareViews)
{
    const auto context = EqualToContext {};

    auto lhs_vector = ::cista::offset::vector<int> {};
    auto rhs_vector = ::cista::offset::vector<int> {};
    auto different_vector = ::cista::offset::vector<int> {};
    lhs_vector.emplace_back(1);
    rhs_vector.emplace_back(1);
    different_vector.emplace_back(2);
    using VectorView = ygg::View<::cista::offset::vector<int>, EqualToContext>;
    EXPECT_TRUE(ygg::EqualTo<VectorView> {}(VectorView(lhs_vector, context), VectorView(rhs_vector, context)));
    EXPECT_FALSE(ygg::EqualTo<VectorView> {}(VectorView(lhs_vector, context), VectorView(different_vector, context)));

    auto lhs_optional = ::cista::optional<int> { 7 };
    auto rhs_optional = ::cista::optional<int> { 7 };
    auto different_optional = ::cista::optional<int> { 8 };
    using OptionalView = ygg::View<::cista::optional<int>, EqualToContext>;
    EXPECT_TRUE(ygg::EqualTo<OptionalView> {}(OptionalView(lhs_optional, context), OptionalView(rhs_optional, context)));
    EXPECT_FALSE(ygg::EqualTo<OptionalView> {}(OptionalView(lhs_optional, context), OptionalView(different_optional, context)));

    using Variant = ::cista::offset::variant<int, unsigned>;
    auto lhs_variant = Variant { 9U };
    auto rhs_variant = Variant { 9U };
    auto different_variant = Variant { 10U };
    using VariantView = ygg::View<Variant, EqualToContext>;
    EXPECT_TRUE(ygg::EqualTo<VariantView> {}(VariantView(lhs_variant, context), VariantView(rhs_variant, context)));
    EXPECT_FALSE(ygg::EqualTo<VariantView> {}(VariantView(lhs_variant, context), VariantView(different_variant, context)));
    EXPECT_TRUE(ygg::EqualTo<VariantView> {}(VariantView(Variant {}, context), VariantView(Variant {}, context)));
    EXPECT_FALSE(ygg::EqualTo<VariantView> {}(VariantView(Variant {}, context), VariantView(lhs_variant, context)));
}

TEST(YggdrasilTests, CommonBlockArrayEqualToAdaptersCompareViews)
{
    auto lhs_storage = std::vector<uint8_t> { 1, 2 };
    auto rhs_storage = std::vector<uint8_t> { 1, 2 };
    auto different_storage = std::vector<uint8_t> { 1, 3 };

    using BlockView = ygg::BasicBlockArrayView<uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>>;
    const auto lhs = BlockView(lhs_storage.data(), lhs_storage.size());
    const auto rhs = BlockView(rhs_storage.data(), rhs_storage.size());
    const auto different = BlockView(different_storage.data(), different_storage.size());

    EXPECT_TRUE(ygg::EqualTo<BlockView> {}(lhs, rhs));
    EXPECT_FALSE(ygg::EqualTo<BlockView> {}(lhs, different));

    const auto context = EqualToContext {};
    using WrappedView = ygg::View<BlockView, EqualToContext>;
    EXPECT_TRUE(ygg::EqualTo<WrappedView> {}(WrappedView(lhs, context), WrappedView(rhs, context)));
    EXPECT_FALSE(ygg::EqualTo<WrappedView> {}(WrappedView(lhs, context), WrappedView(different, context)));
}

TEST(YggdrasilTests, CommonRawAndSegmentedVectorEqualToAdaptersCompareValues)
{
    auto pool = ygg::RawVectorPool<uint8_t, int, 32>();
    const auto lhs_index = pool.insert(std::vector<int> { 1, 2 });
    const auto rhs_index = pool.insert(std::vector<int> { 1, 2 });
    const auto different_index = pool.insert(std::vector<int> { 1, 3 });

    EXPECT_TRUE((ygg::EqualTo<ygg::RawVectorView<uint8_t, int>> {}(pool[lhs_index], pool[rhs_index])));
    EXPECT_FALSE((ygg::EqualTo<ygg::RawVectorView<uint8_t, int>> {}(pool[lhs_index], pool[different_index])));

    auto lhs = ygg::SegmentedVector<int, 2>();
    auto rhs = ygg::SegmentedVector<int, 2>();
    auto different = ygg::SegmentedVector<int, 2>();
    lhs.push_back(1);
    lhs.push_back(2);
    rhs.push_back(1);
    rhs.push_back(2);
    different.push_back(1);
    different.push_back(3);

    EXPECT_TRUE((ygg::EqualTo<ygg::SegmentedVector<int, 2>> {}(lhs, rhs)));
    EXPECT_FALSE((ygg::EqualTo<ygg::SegmentedVector<int, 2>> {}(lhs, different)));
}

TEST(YggdrasilTests, CommonCistaEqualToAdaptersComparePairAndNestedArrayViews)
{
    using Pair = ::cista::pair<int, ::cista::array<int, 2>>;
    using Array = ::cista::array<Pair, 2>;
    using PairView = ygg::View<Pair, EqualToContext>;
    using ArrayView = ygg::View<Array, EqualToContext>;

    const auto context = EqualToContext {};
    const auto lhs = Array { Pair { 1, { 2, 3 } }, Pair { 4, { 5, 6 } } };
    const auto rhs = lhs;
    const auto different = Array { Pair { 1, { 2, 3 } }, Pair { 4, { 5, 7 } } };

    EXPECT_TRUE(ygg::EqualTo<Pair> {}(lhs[1], rhs[1]));
    EXPECT_FALSE(ygg::EqualTo<Pair> {}(lhs[1], different[1]));
    EXPECT_TRUE(ygg::EqualTo<PairView> {}(PairView(lhs[1], context), PairView(rhs[1], context)));
    EXPECT_FALSE(ygg::EqualTo<PairView> {}(PairView(lhs[1], context), PairView(different[1], context)));
    EXPECT_TRUE(ygg::EqualTo<ArrayView> {}(ArrayView(lhs, context), ArrayView(rhs, context)));
    EXPECT_FALSE(ygg::EqualTo<ArrayView> {}(ArrayView(lhs, context), ArrayView(different, context)));
}

}  // namespace ygg::tests
