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
#include <string>
#include <string_view>
#include <variant>
#include <vector>
#include <yggdrasil/containers/associative_containers.hpp>
#include <yggdrasil/containers/block_array_hash.hpp>
#include <yggdrasil/containers/dynamic_bitset_hash.hpp>
#include <yggdrasil/containers/raw_vector_hash.hpp>
#include <yggdrasil/containers/segmented_vector_hash.hpp>
#include <yggdrasil/core/observer_ptr_hash.hpp>
#include <yggdrasil/semantics/comparators.hpp>
#include <yggdrasil/semantics/hash.hpp>
#include <yggdrasil/serialization/cista_hash.hpp>

namespace ygg::tests
{

struct HashContext
{
};

// Golden values: ygg::Hash fixes its algorithms (MurmurHash3) precisely so that hash values are
// identical across standard libraries, compilers, and platforms. These constants pin that contract;
// if this test fails, hash-order-dependent results (e.g., downstream test fixtures) change with it.
TEST(YggdrasilTests, CommonHashValuesArePlatformIndependent)
{
    EXPECT_EQ(ygg::Hash<int> {}(42), 0x810879608e4259ccULL);
    EXPECT_EQ(ygg::Hash<int> {}(0), 0x0000000000000000ULL);  // fmix64 fixed point
    EXPECT_EQ(ygg::Hash<int> {}(-1), 0x64b5720b4b825f21ULL);
    EXPECT_EQ(ygg::Hash<double> {}(1.5), 0x885dcc874e75b6f0ULL);
    EXPECT_EQ(ygg::Hash<float> {}(1.5F), 0x58575b497a14b09cULL);
    EXPECT_EQ(ygg::Hash<double> {}(-0.0), ygg::Hash<double> {}(0.0));
    EXPECT_EQ(ygg::Hash<std::string> {}(std::string("yggdrasil")), 0x7728ac0c932a3086ULL);
    EXPECT_EQ(ygg::Hash<std::string> {}(std::string("yggdrasil")), ygg::Hash<std::string_view> {}(std::string_view("yggdrasil")));
    EXPECT_EQ(ygg::Hash<std::string> {}(std::string()), 0x0000000000000000ULL);
}

TEST(YggdrasilTests, CommonHashSupportsUnalignedByteRanges)
{
    alignas(uint64_t) const char storage[] = "_yggdrasil";
    const auto text = std::string_view(storage + 1, sizeof(storage) - 2);

    ASSERT_NE(reinterpret_cast<uintptr_t>(text.data()) % alignof(uint64_t), 0);
    EXPECT_EQ(ygg::Hash<std::string_view> {}(text), ygg::Hash<std::string> {}("yggdrasil"));
}

TEST(YggdrasilTests, CommonHashAdaptersNormalizeFloatingPointNaN)
{
    const auto first_nan = std::numeric_limits<double>::quiet_NaN();
    const auto second_nan = std::numeric_limits<double>::signaling_NaN();

    EXPECT_EQ(ygg::Hash<double> {}(first_nan), ygg::Hash<double> {}(second_nan));
    EXPECT_NE(ygg::Hash<double> {}(first_nan), ygg::Hash<double> {}(0.0));
}

TEST(YggdrasilTests, CommonHashRangeMatchesContainerHash)
{
    const auto values = std::vector<int> { 1, 2, 3 };

    EXPECT_EQ(ygg::hash_range(values), ygg::Hash<std::vector<int>> {}(values));
    EXPECT_EQ(ygg::hash_range(std::span<const int>(values)), ygg::Hash<std::span<const int>> {}(std::span<const int>(values)));
}

TEST(YggdrasilTests, CommonHashRangeKeepsSizeInSeed)
{
    const auto one = std::array<int, 1> { 0 };
    const auto two = std::array<int, 2> { 0, 0 };

    EXPECT_NE(ygg::hash_range(one), ygg::hash_range(two));
}

TEST(YggdrasilTests, CommonHashRangeAcceptsRangeAdaptors)
{
    const auto values = std::vector<int> { 1, 2, 3, 4 };
    auto even_squares = values | std::views::filter([](int value) { return value % 2 == 0; }) | std::views::transform([](int value) { return value * value; });

    auto expected = size_t { 0 };
    ygg::hash_combine(expected, 4);
    ygg::hash_combine(expected, 16);
    EXPECT_EQ(ygg::hash_range(even_squares), expected);
}

TEST(YggdrasilTests, CommonHashAdaptersHashVariantAlternativeIndex)
{
    using Variant = std::variant<int, unsigned>;

    const auto lhs = Variant { 9 };
    const auto rhs = Variant { 9 };
    const auto different_value = Variant { 10 };
    const auto different_type = Variant { 9U };

    EXPECT_EQ(ygg::Hash<Variant> {}(lhs), ygg::Hash<Variant> {}(rhs));
    EXPECT_NE(ygg::Hash<Variant> {}(lhs), ygg::Hash<Variant> {}(different_value));
    EXPECT_NE(ygg::Hash<Variant> {}(lhs), ygg::Hash<Variant> {}(different_type));

    using DuplicateTypeVariant = std::variant<int, int>;
    const auto first_alternative = DuplicateTypeVariant(std::in_place_index<0>, 9);
    const auto second_alternative = DuplicateTypeVariant(std::in_place_index<1>, 9);
    EXPECT_NE(ygg::Hash<DuplicateTypeVariant> {}(first_alternative), ygg::Hash<DuplicateTypeVariant> {}(second_alternative));
}

TEST(YggdrasilTests, CommonHashAdaptersMixOptionalEngagementState)
{
    const auto empty = std::optional<int> {};
    const auto zero = std::optional<int> { 0 };
    const auto another_zero = std::optional<int> { 0 };

    EXPECT_EQ(ygg::Hash<std::optional<int>> {}(zero), ygg::Hash<std::optional<int>> {}(another_zero));
    EXPECT_NE(ygg::Hash<std::optional<int>> {}(empty), ygg::Hash<std::optional<int>> {}(zero));
}

TEST(YggdrasilTests, CommonReferenceWrapperHashAdaptersHashReferencedValues)
{
    auto lhs_value = 7;
    auto rhs_value = 7;
    auto different_value = 8;

    const auto lhs = std::ref(lhs_value);
    const auto rhs = std::ref(rhs_value);
    const auto different = std::ref(different_value);

    EXPECT_EQ(ygg::Hash<std::reference_wrapper<int>> {}(lhs), ygg::Hash<std::reference_wrapper<int>> {}(rhs));
    EXPECT_NE(ygg::Hash<std::reference_wrapper<int>> {}(lhs), ygg::Hash<std::reference_wrapper<int>> {}(different));
}

TEST(YggdrasilTests, CommonCistaHashAdaptersHashOffsetVector)
{
    auto values = ::cista::offset::vector<int> {};
    values.emplace_back(1);
    values.emplace_back(2);

    EXPECT_EQ(ygg::hash_range(values), ygg::Hash<::cista::offset::vector<int>> {}(values));
}

TEST(YggdrasilTests, CommonCistaHashAdaptersHashOffsetStringOptionalAndVariant)
{
    auto lhs_string = ::cista::offset::string {};
    auto rhs_string = ::cista::offset::string {};
    lhs_string = "alpha";
    rhs_string = "alpha";
    EXPECT_EQ(ygg::Hash<::cista::offset::string> {}(lhs_string), ygg::Hash<::cista::offset::string> {}(rhs_string));

    auto lhs_optional = ::cista::optional<int> { 7 };
    auto rhs_optional = ::cista::optional<int> { 7 };
    auto zero_optional = ::cista::optional<int> { 0 };
    auto empty_optional = ::cista::optional<int> {};
    EXPECT_EQ(ygg::Hash<::cista::optional<int>> {}(lhs_optional), ygg::Hash<::cista::optional<int>> {}(rhs_optional));
    EXPECT_NE(ygg::Hash<::cista::optional<int>> {}(lhs_optional), ygg::Hash<::cista::optional<int>> {}(empty_optional));
    EXPECT_NE(ygg::Hash<::cista::optional<int>> {}(empty_optional), ygg::Hash<::cista::optional<int>> {}(zero_optional));

    using Variant = ::cista::offset::variant<int, unsigned>;
    auto lhs_variant = Variant { 9 };
    auto rhs_variant = Variant { 9 };
    auto different_type_variant = Variant { 9U };
    EXPECT_EQ(ygg::Hash<Variant> {}(lhs_variant), ygg::Hash<Variant> {}(rhs_variant));
    EXPECT_NE(ygg::Hash<Variant> {}(lhs_variant), ygg::Hash<Variant> {}(different_type_variant));
    EXPECT_EQ(ygg::Hash<Variant> {}(Variant {}), ygg::Hash<Variant> {}(Variant {}));
    EXPECT_NE(ygg::Hash<Variant> {}(Variant {}), ygg::Hash<Variant> {}(lhs_variant));

    using DuplicateTypeVariant = ::cista::offset::variant<int, int>;
    auto duplicate_first = DuplicateTypeVariant {};
    duplicate_first.emplace<0>(9);
    auto duplicate_second = DuplicateTypeVariant {};
    duplicate_second.emplace<1>(9);
    EXPECT_NE(ygg::Hash<DuplicateTypeVariant> {}(duplicate_first), ygg::Hash<DuplicateTypeVariant> {}(duplicate_second));
}

TEST(YggdrasilTests, CommonObserverPtrHashAdaptersHashPointee)
{
    const auto value = 7;
    const auto ptr = ygg::make_observer(value);

    EXPECT_EQ(ygg::Hash<int> {}(value), ygg::Hash<ygg::ObserverPtr<const int>> {}(ptr));
}

TEST(YggdrasilTests, CommonHashAdaptersHashOrderedAssociativeAliases)
{
    const auto set = ygg::Set<int> { 1, 2 };
    EXPECT_EQ(ygg::hash_range(set), ygg::Hash<ygg::Set<int>> {}(set));

    const auto map = ygg::Map<int, int> { { 1, 2 } };
    EXPECT_EQ(ygg::hash_range(map), (ygg::Hash<ygg::Map<int, int>> {}(map)));
}

TEST(YggdrasilTests, CommonDynamicBitsetHashAdaptersHashBoostDynamicBitsets)
{
    auto lhs = boost::dynamic_bitset<>(8);
    auto rhs = boost::dynamic_bitset<>(8);

    lhs.set(1);
    rhs.set(1);

    EXPECT_EQ(ygg::Hash<boost::dynamic_bitset<>> {}(lhs), ygg::Hash<boost::dynamic_bitset<>> {}(rhs));

    rhs.set(2);

    EXPECT_NE(ygg::Hash<boost::dynamic_bitset<>> {}(lhs), ygg::Hash<boost::dynamic_bitset<>> {}(rhs));
}

TEST(YggdrasilTests, CommonDynamicBitsetHashAdaptersHashBitsetSpans)
{
    const auto lhs_blocks = std::vector<std::uint64_t> { 0b1010 };
    const auto rhs_blocks = std::vector<std::uint64_t> { 0b1010 };
    const auto different_blocks = std::vector<std::uint64_t> { 0b0010 };

    const auto lhs = ygg::BitsetSpan<const std::uint64_t>(lhs_blocks.data(), 4);
    const auto rhs = ygg::BitsetSpan<const std::uint64_t>(rhs_blocks.data(), 4);
    const auto different = ygg::BitsetSpan<const std::uint64_t>(different_blocks.data(), 4);

    EXPECT_EQ(ygg::Hash<ygg::BitsetSpan<const std::uint64_t>> {}(lhs), ygg::Hash<ygg::BitsetSpan<const std::uint64_t>> {}(rhs));
    EXPECT_NE(ygg::Hash<ygg::BitsetSpan<const std::uint64_t>> {}(lhs), ygg::Hash<ygg::BitsetSpan<const std::uint64_t>> {}(different));
}

TEST(YggdrasilTests, CommonCistaHashAdaptersHashViews)
{
    const auto context = HashContext {};

    auto lhs_vector = ::cista::offset::vector<int> {};
    auto rhs_vector = ::cista::offset::vector<int> {};
    lhs_vector.emplace_back(1);
    rhs_vector.emplace_back(1);
    using VectorView = ygg::View<::cista::offset::vector<int>, HashContext>;
    EXPECT_EQ(ygg::Hash<VectorView> {}(VectorView(lhs_vector, context)), ygg::Hash<VectorView> {}(VectorView(rhs_vector, context)));

    auto lhs_optional = ::cista::optional<int> { 7 };
    auto rhs_optional = ::cista::optional<int> { 7 };
    auto zero_optional = ::cista::optional<int> { 0 };
    auto empty_optional = ::cista::optional<int> {};
    using OptionalView = ygg::View<::cista::optional<int>, HashContext>;
    EXPECT_EQ(ygg::Hash<OptionalView> {}(OptionalView(lhs_optional, context)), ygg::Hash<OptionalView> {}(OptionalView(rhs_optional, context)));
    EXPECT_NE(ygg::Hash<OptionalView> {}(OptionalView(empty_optional, context)), ygg::Hash<OptionalView> {}(OptionalView(zero_optional, context)));

    using Variant = ::cista::offset::variant<int, unsigned>;
    auto lhs_variant = Variant { 9U };
    auto rhs_variant = Variant { 9U };
    auto different_type_variant = Variant { 9 };
    using VariantView = ygg::View<Variant, HashContext>;
    EXPECT_EQ(ygg::Hash<VariantView> {}(VariantView(lhs_variant, context)), ygg::Hash<VariantView> {}(VariantView(rhs_variant, context)));
    EXPECT_NE(ygg::Hash<VariantView> {}(VariantView(lhs_variant, context)), ygg::Hash<VariantView> {}(VariantView(different_type_variant, context)));
    EXPECT_EQ(ygg::Hash<VariantView> {}(VariantView(Variant {}, context)), ygg::Hash<VariantView> {}(VariantView(Variant {}, context)));
    EXPECT_NE(ygg::Hash<VariantView> {}(VariantView(Variant {}, context)), ygg::Hash<VariantView> {}(VariantView(lhs_variant, context)));
}

TEST(YggdrasilTests, CommonBlockArrayHashAdaptersHashViews)
{
    auto lhs_storage = std::vector<uint8_t> { 1, 2 };
    auto rhs_storage = std::vector<uint8_t> { 1, 2 };
    auto different_storage = std::vector<uint8_t> { 1, 3 };

    using BlockView = ygg::BasicBlockArrayView<uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>>;
    const auto lhs = BlockView(lhs_storage.data(), lhs_storage.size());
    const auto rhs = BlockView(rhs_storage.data(), rhs_storage.size());
    const auto different = BlockView(different_storage.data(), different_storage.size());

    EXPECT_EQ(ygg::Hash<BlockView> {}(lhs), ygg::Hash<BlockView> {}(rhs));
    EXPECT_NE(ygg::Hash<BlockView> {}(lhs), ygg::Hash<BlockView> {}(different));

    const auto context = HashContext {};
    using WrappedView = ygg::View<BlockView, HashContext>;
    EXPECT_EQ(ygg::Hash<WrappedView> {}(WrappedView(lhs, context)), ygg::Hash<WrappedView> {}(WrappedView(rhs, context)));
}

TEST(YggdrasilTests, CommonRawAndSegmentedVectorHashAdaptersHashValues)
{
    auto pool = ygg::RawVectorPool<uint8_t, int, 32>();
    const auto lhs_index = pool.insert(std::vector<int> { 1, 2 });
    const auto rhs_index = pool.insert(std::vector<int> { 1, 2 });
    const auto different_index = pool.insert(std::vector<int> { 1, 3 });

    EXPECT_EQ((ygg::Hash<ygg::RawVectorView<uint8_t, int>> {}(pool[lhs_index])), (ygg::Hash<ygg::RawVectorView<uint8_t, int>> {}(pool[rhs_index])));
    EXPECT_NE((ygg::Hash<ygg::RawVectorView<uint8_t, int>> {}(pool[lhs_index])), (ygg::Hash<ygg::RawVectorView<uint8_t, int>> {}(pool[different_index])));

    auto lhs = ygg::SegmentedVector<int, 2>();
    auto rhs = ygg::SegmentedVector<int, 2>();
    auto different = ygg::SegmentedVector<int, 2>();
    lhs.push_back(1);
    lhs.push_back(2);
    rhs.push_back(1);
    rhs.push_back(2);
    different.push_back(1);
    different.push_back(3);

    EXPECT_EQ((ygg::Hash<ygg::SegmentedVector<int, 2>> {}(lhs)), (ygg::Hash<ygg::SegmentedVector<int, 2>> {}(rhs)));
    EXPECT_NE((ygg::Hash<ygg::SegmentedVector<int, 2>> {}(lhs)), (ygg::Hash<ygg::SegmentedVector<int, 2>> {}(different)));
}

}  // namespace ygg::tests
