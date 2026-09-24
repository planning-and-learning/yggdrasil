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
#include <cstddef>
#include <cstdint>
#include <functional>
#include <gtest/gtest.h>
#include <limits>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#include <yggdrasil/containers/associative_containers.hpp>
#include <yggdrasil/core/observer_ptr_hash.hpp>
#include <yggdrasil/semantics/comparators.hpp>
#include <yggdrasil/semantics/containers/block_array_hash.hpp>
#include <yggdrasil/semantics/containers/dynamic_bitset_hash.hpp>
#include <yggdrasil/semantics/containers/segmented_vector_hash.hpp>
#include <yggdrasil/semantics/hash.hpp>
#include <yggdrasil/serialization/cista_hash.hpp>

namespace ygg::tests
{

struct HashContext
{
};

// Golden values pin scalar hashing and xxHash64 byte hashing across standard libraries and compilers.
TEST(YggdrasilTests, CommonHashValuesMatchDefinedAlgorithms)
{
    static_assert(std::same_as<decltype(ygg::Hash<int> {}(42)), ygg::hash_t>);
    static_assert(sizeof(ygg::hash_t) == 8);
    EXPECT_EQ(ygg::Hash<int> {}(42), 0x000000000000002aULL);
    EXPECT_EQ(ygg::Hash<int> {}(0), 0x0000000000000000ULL);
    EXPECT_EQ(ygg::Hash<int> {}(-1), 0xffffffffffffffffULL);
    if constexpr (sizeof(size_t) == sizeof(uint64_t))
        EXPECT_EQ(ygg::Hash<double> {}(1.5), 0x3ff8000000000000ULL);
    EXPECT_EQ(ygg::Hash<float> {}(1.5F), 0x000000003fc00000ULL);
    EXPECT_EQ(ygg::Hash<double> {}(-0.0), ygg::Hash<double> {}(0.0));
    EXPECT_EQ(ygg::Hash<std::string> {}(std::string("yggdrasil")), 0x79adda5dd5464cb6ULL);
    EXPECT_EQ(ygg::Hash<std::string> {}(std::string("yggdrasil")), ygg::Hash<std::string_view> {}(std::string_view("yggdrasil")));
    EXPECT_EQ(ygg::Hash<std::string> {}(std::string()), 0xef46db3751d8e999ULL);
}

TEST(YggdrasilTests, CommonHashSupportsUnalignedByteRanges)
{
    alignas(uint64_t) const char storage[] = "_0123456789abcdef";
    const auto text = std::string_view(storage + 1, sizeof(storage) - 2);

    ASSERT_NE(reinterpret_cast<uintptr_t>(text.data()) % alignof(uint64_t), 0);
    EXPECT_EQ(ygg::Hash<std::string_view> {}(text), 0x5c5b90c34e376d0bULL);
}

TEST(YggdrasilTests, CommonHashBytesMatchesXxHashReferenceAcrossBlockBoundaries)
{
    auto bytes = std::array<unsigned char, 66> {};
    for (size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<unsigned char>(i * 37);

    // Seed-zero results from the reference xxHash implementation, including unaligned input.
    const auto cases = std::array<std::pair<size_t, ygg::hash_t>, 15> {{
        { 0, 0xef46db3751d8e999ULL },
        { 1, 0x89ac406afd6cd3b7ULL },
        { 3, 0x614123b469e8f204ULL },
        { 4, 0x0b0552d04906db7bULL },
        { 7, 0x547bf259c8d24ca9ULL },
        { 8, 0xfd161ba5079f032fULL },
        { 15, 0x280ecabe993ce430ULL },
        { 16, 0x1bb38a0220bb17b3ULL },
        { 17, 0xb4ff4a6c0661ca93ULL },
        { 31, 0xbd93c8cc6c5f4d8eULL },
        { 32, 0x7fd19ac913684e8aULL },
        { 33, 0xfb548bf9f10abcd1ULL },
        { 63, 0x70781206707beec8ULL },
        { 64, 0x6892b7f3d78396eeULL },
        { 65, 0x4948c637675f5150ULL },
    }};
    EXPECT_EQ(ygg::hashing::hash_bytes(nullptr, 0), 0xef46db3751d8e999ULL);
    for (const auto& [size, expected] : cases)
    {
        SCOPED_TRACE(size);
        EXPECT_EQ(ygg::hashing::hash_bytes(bytes.data() + 1, size), expected);
    }
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

TEST(YggdrasilTests, CommonHashRangeMatchesArrayAndSpanAdapters)
{
    auto values = std::array<uint8_t, 4> { 0, 1, 127, 255 };
    const auto expected = ygg::hashing::hash_bytes(values.data(), values.size());

    EXPECT_EQ(ygg::Hash<> {}(values), expected);
    EXPECT_EQ(ygg::Hash<> {}(std::span<uint8_t>(values)), expected);
    EXPECT_EQ(ygg::Hash<> {}(std::span<const uint8_t>(values)), expected);
    EXPECT_EQ(ygg::Hash<> {}(std::span<uint8_t, 4>(values)), expected);
    EXPECT_EQ(ygg::Hash<> {}(std::span<const uint8_t, 4>(values)), expected);
}

TEST(YggdrasilTests, CommonHashRangeMatchesNativeBytesAcrossContainers)
{
    const auto check = []<typename T>()
    {
        for (const auto size : { 0U, 1U, 3U, 4U, 7U, 8U, 15U, 16U, 17U, 65U, 1000U })
        {
            SCOPED_TRACE(size);
            auto values = std::vector<T> {};
            auto cista_values = ::cista::offset::vector<T> {};
            auto segmented = ygg::SegmentedVector<T, 2> {};
            for (auto i = 0U; i < size; ++i)
            {
                const auto value = static_cast<T>(static_cast<int>(i % 251) - 125);
                values.push_back(value);
                cista_values.emplace_back(value);
                segmented.push_back(value);
            }
            auto transformed = values | std::views::transform([](T value) { return value; });
            static_assert(std::ranges::sized_range<decltype(transformed)>);
            static_assert(!std::ranges::contiguous_range<decltype(transformed)>);
            const auto expected = ygg::hashing::hash_bytes(values.data(), values.size() * sizeof(T));
            const auto context = HashContext {};
            using VectorView = ygg::View<::cista::offset::vector<T>, HashContext>;
            EXPECT_EQ(ygg::Hash<> {}(values), expected);
            EXPECT_EQ(ygg::Hash<> {}(std::span<T>(values)), expected);
            EXPECT_EQ(ygg::Hash<> {}(std::span<const T>(values)), expected);
            EXPECT_EQ(ygg::Hash<> {}(cista_values), expected);
            EXPECT_EQ(ygg::Hash<> {}(VectorView(cista_values, context)), expected);
            EXPECT_EQ(ygg::Hash<> {}(segmented), expected);
            EXPECT_EQ(ygg::hash_range(transformed), expected);
            if constexpr (std::unsigned_integral<T>)
            {
                auto storage = std::vector<T>(size + 1);
                using PackedView = ygg::BasicBitPackedArrayView<T, ygg::bit::ForwardingBlockCoder<T>>;
                auto packed = PackedView(storage.data(), size, std::numeric_limits<T>::digits, 1);
                packed = std::span<const T>(values);
                EXPECT_EQ(ygg::hash_range(packed), expected);
                EXPECT_EQ(ygg::Hash<> {}(packed), expected);
            }
        }
    };

    enum class Code : uint16_t
    {
    };
    check.operator()<uint8_t>();
    check.operator()<uint16_t>();
    check.operator()<uint32_t>();
    check.operator()<uint64_t>();
    check.operator()<int8_t>();
    check.operator()<int16_t>();
    check.operator()<int32_t>();
    check.operator()<int64_t>();
    check.operator()<std::byte>();
    check.operator()<Code>();
}

TEST(YggdrasilTests, CommonHashRangePreservesElementHashSemantics)
{
    const auto elementwise_hash = [](const auto& values)
    {
        auto seed = ygg::hash_t { values.size() };
        for (const auto& value : values)
            ygg::hash_combine(seed, value);
        return seed;
    };

    const auto floats = std::array { -0.0, 1.5, std::numeric_limits<double>::quiet_NaN() };
    EXPECT_EQ(ygg::hash_range(floats), elementwise_hash(floats));
    EXPECT_EQ(ygg::hash_range(floats), ygg::hash_range(std::array { 0.0, 1.5, std::numeric_limits<double>::signaling_NaN() }));
    const auto booleans = std::array { false, true, false };
    const auto packed_booleans = std::vector<bool> { false, true, false };
    EXPECT_EQ(ygg::hash_range(booleans), elementwise_hash(booleans));
    EXPECT_EQ(ygg::hash_range(packed_booleans), elementwise_hash(booleans));

    struct ModuloValue
    {
        uint32_t value;

        auto identifying_members() const noexcept { return std::tuple(value % 10); }
        bool operator==(const ModuloValue& other) const noexcept { return value % 10 == other.value % 10; }
    };
    static_assert(std::has_unique_object_representations_v<ModuloValue>);
    const auto lhs = std::array { ModuloValue { 1 }, ModuloValue { 2 } };
    const auto rhs = std::array { ModuloValue { 11 }, ModuloValue { 22 } };
    ASSERT_EQ(lhs, rhs);
    EXPECT_EQ(ygg::hash_range(lhs), elementwise_hash(lhs));
    EXPECT_EQ(ygg::hash_range(lhs), ygg::hash_range(rhs));
}

TEST(YggdrasilTests, CommonHashRangeDistinguishesLengths)
{
    const auto one = std::array<int, 1> { 0 };
    const auto two = std::array<int, 2> { 0, 0 };

    EXPECT_NE(ygg::hash_range(one), ygg::hash_range(two));
}

TEST(YggdrasilTests, CommonHashRangeAcceptsRangeAdaptors)
{
    const auto values = std::vector<int> { 1, 2, 3, 4 };
    auto even_squares = values | std::views::filter([](int value) { return value % 2 == 0; }) | std::views::transform([](int value) { return value * value; });

    auto expected = ygg::hash_t { 0 };
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

TEST(YggdrasilTests, CommonArrayHashAdaptersHashViews)
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
    EXPECT_EQ(ygg::Hash<BlockView> {}(lhs), ygg::hash_range(lhs_storage));

    const auto context = HashContext {};
    using WrappedView = ygg::View<BlockView, HashContext>;
    EXPECT_EQ(ygg::Hash<WrappedView> {}(WrappedView(lhs, context)), ygg::Hash<WrappedView> {}(WrappedView(rhs, context)));

    auto lhs_bits = std::array<uint8_t, 1> {};
    auto rhs_bits = std::array<uint8_t, 1> {};
    auto different_bits = std::array<uint8_t, 1> {};
    using BitPackedView = ygg::BasicBitPackedArrayView<uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>>;
    auto bit_lhs = BitPackedView(lhs_bits.data(), 2, 2, 0);
    auto bit_rhs = BitPackedView(rhs_bits.data(), 2, 2, 0);
    auto bit_different = BitPackedView(different_bits.data(), 2, 2, 0);
    bit_lhs = std::array<uint8_t, 2> { 1, 2 };
    bit_rhs = std::array<uint8_t, 2> { 1, 2 };
    bit_different = std::array<uint8_t, 2> { 1, 3 };

    EXPECT_EQ(ygg::Hash<BitPackedView> {}(bit_lhs), ygg::Hash<BitPackedView> {}(bit_rhs));
    EXPECT_NE(ygg::Hash<BitPackedView> {}(bit_lhs), ygg::Hash<BitPackedView> {}(bit_different));
    EXPECT_EQ(ygg::hash_range(bit_lhs), ygg::hash_range(lhs_storage));
    EXPECT_EQ(ygg::Hash<BitPackedView> {}(bit_lhs), ygg::hash_range(lhs_storage));

    using WrappedBitPackedView = ygg::View<BitPackedView, HashContext>;
    EXPECT_EQ(ygg::Hash<WrappedBitPackedView> {}(WrappedBitPackedView(bit_lhs, context)),
              ygg::Hash<WrappedBitPackedView> {}(WrappedBitPackedView(bit_rhs, context)));
}

TEST(YggdrasilTests, CommonSegmentedVectorHashAdapterHashesValues)
{
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

TEST(YggdrasilTests, CommonCistaHashAdaptersHashPairAndNestedArrayViews)
{
    using Pair = ::cista::pair<int, ::cista::array<int, 2>>;
    using Array = ::cista::array<Pair, 2>;
    using PairView = ygg::View<Pair, HashContext>;
    using ArrayView = ygg::View<Array, HashContext>;

    const auto context = HashContext {};
    const auto lhs = Array { Pair { 1, { 2, 3 } }, Pair { 4, { 5, 6 } } };
    const auto rhs = lhs;
    const auto different = Array { Pair { 1, { 2, 3 } }, Pair { 4, { 5, 7 } } };

    EXPECT_EQ(ygg::Hash<Pair> {}(lhs[1]), ygg::Hash<Pair> {}(rhs[1]));
    EXPECT_NE(ygg::Hash<Pair> {}(lhs[1]), ygg::Hash<Pair> {}(different[1]));
    EXPECT_EQ(ygg::Hash<PairView> {}(PairView(lhs[1], context)), ygg::Hash<PairView> {}(PairView(rhs[1], context)));
    EXPECT_NE(ygg::Hash<PairView> {}(PairView(lhs[1], context)), ygg::Hash<PairView> {}(PairView(different[1], context)));
    EXPECT_EQ(ygg::Hash<ArrayView> {}(ArrayView(lhs, context)), ygg::Hash<ArrayView> {}(ArrayView(rhs, context)));
    EXPECT_NE(ygg::Hash<ArrayView> {}(ArrayView(lhs, context)), ygg::Hash<ArrayView> {}(ArrayView(different, context)));
}

}  // namespace ygg::tests
