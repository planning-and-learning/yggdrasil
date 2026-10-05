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

#include "yggdrasil/containers/dynamic_bitset.hpp"

#include "yggdrasil/core/config.hpp"
#include "yggdrasil/formatting/dynamic_bitset_formatters.hpp"
#include "yggdrasil/semantics/containers/dynamic_bitset_equal_to.hpp"
#include "yggdrasil/semantics/containers/dynamic_bitset_hash.hpp"
#include "yggdrasil/semantics/containers/dynamic_bitset_ordering.hpp"

#include <boost/dynamic_bitset.hpp>
#include <algorithm>
#include <array>
#include <ranges>
#include <concepts>
#include <gtest/gtest.h>
#include <stdexcept>
#include <vector>

namespace ygg::tests
{

TEST(YggdrasilTests, CommonDynamicBitset)
{
    auto lhs_blocks = std::vector<uint64_t>(ygg::BitsetSpan<uint64_t>::num_blocks(70), 0);
    auto rhs_blocks = std::vector<uint64_t>(ygg::BitsetSpan<uint64_t>::num_blocks(70), 0);

    auto lhs = ygg::BitsetSpan<uint64_t>(lhs_blocks.data(), 70);
    auto rhs = ygg::BitsetSpan<uint64_t>(rhs_blocks.data(), 70);

    EXPECT_EQ(lhs.size(), 70);
    EXPECT_FALSE(lhs.empty());
    EXPECT_TRUE(lhs.none());
    EXPECT_FALSE(lhs.any());
    EXPECT_FALSE(lhs.all());

    lhs[1] = true;
    lhs.set(69);
    lhs.set(68, false);
    rhs.set(1);
    rhs.set(2);
    rhs.set(69);

    EXPECT_TRUE(lhs[1]);
    EXPECT_FALSE(lhs[2]);
    EXPECT_TRUE(lhs.any());
    EXPECT_FALSE(lhs.none());
    EXPECT_FALSE(lhs.all());

    auto lhs_raw_blocks = lhs.blocks();
    lhs_raw_blocks.back() |= ~ygg::BitsetSpan<uint64_t>::last_mask(lhs.size());
    EXPECT_FALSE(lhs.trailing_bits_zero());
    lhs.clear_trailing_bits();
    EXPECT_TRUE(lhs.trailing_bits_zero());

    EXPECT_TRUE(lhs.intersects(rhs));
    EXPECT_TRUE(lhs.is_subset_of(rhs));
    EXPECT_TRUE(lhs.is_proper_subset_of(rhs));
    EXPECT_TRUE(rhs.is_superset_of(lhs));
    EXPECT_TRUE(rhs.is_proper_superset_of(lhs));
    EXPECT_FALSE(rhs.is_subset_of(lhs));

    lhs[2].flip();
    EXPECT_TRUE(lhs == rhs);
    EXPECT_TRUE(lhs.is_subset_of(rhs));
    EXPECT_FALSE(lhs.is_proper_subset_of(rhs));

    lhs.flip();
    EXPECT_FALSE(lhs.intersects(rhs));
    EXPECT_TRUE(lhs.trailing_bits_zero());

    lhs ^= rhs;
    EXPECT_TRUE(lhs.all());
}

TEST(YggdrasilTests, CommonDynamicBitsetAtChecksBounds)
{
    auto blocks = std::vector<uint64_t>(ygg::BitsetSpan<uint64_t>::num_blocks(8), 0);
    auto bitset = ygg::BitsetSpan<uint64_t>(blocks.data(), 8);
    using Bitset = decltype(bitset);
    using ConstBitset = ygg::BitsetSpan<const uint64_t>;
    static_assert(std::convertible_to<Bitset, ConstBitset>);

    bitset.at(3) = true;
    EXPECT_TRUE(bitset.at(3));

    bitset.at(3).flip();
    EXPECT_FALSE(bitset.at(3));

    const auto const_bitset = ConstBitset(bitset);
    EXPECT_FALSE(const_bitset.at(3));
    EXPECT_EQ(bitset.data(), blocks.data());
    EXPECT_EQ(const_bitset.data(), blocks.data());

    EXPECT_THROW(bitset.at(8), std::out_of_range);
    EXPECT_THROW(const_bitset.at(8), std::out_of_range);
}

TEST(YggdrasilTests, CommonDynamicBitsetAtRejectsInvalidNonEmptySpan)
{
    auto bitset = ygg::BitsetSpan<uint64_t>(nullptr, 1);
    const auto const_bitset = ygg::BitsetSpan<const uint64_t>(nullptr, 1);

    EXPECT_THROW(bitset.at(0), std::logic_error);
    EXPECT_THROW(const_bitset.at(0), std::logic_error);

    auto empty_bitset = ygg::BitsetSpan<uint64_t>(nullptr, 0);
    const auto empty_const_bitset = ygg::BitsetSpan<const uint64_t>(nullptr, 0);

    EXPECT_THROW(empty_bitset.at(0), std::out_of_range);
    EXPECT_THROW(empty_const_bitset.at(0), std::out_of_range);
}

TEST(YggdrasilTests, CommonDynamicBitsetFindNextHandlesSentinelAndEndPositions)
{
    auto blocks = std::vector<uint64_t>(ygg::BitsetSpan<uint64_t>::num_blocks(70), 0);
    auto bitset = ygg::BitsetSpan<uint64_t>(blocks.data(), 70);

    EXPECT_EQ(bitset.find_first(), ygg::BitsetSpan<uint64_t>::npos);
    EXPECT_EQ(bitset.find_next(ygg::BitsetSpan<uint64_t>::npos), ygg::BitsetSpan<uint64_t>::npos);
    EXPECT_EQ(bitset.find_next_zero(ygg::BitsetSpan<uint64_t>::npos), ygg::BitsetSpan<uint64_t>::npos);

    bitset.set(69);
    EXPECT_EQ(bitset.find_first(), 69);
    EXPECT_EQ(bitset.find_next(68), 69);
    EXPECT_EQ(bitset.find_next(69), ygg::BitsetSpan<uint64_t>::npos);
    EXPECT_EQ(bitset.find_next_zero(68), ygg::BitsetSpan<uint64_t>::npos);

    bitset.reset();
    bitset.set();
    EXPECT_EQ(bitset.find_first_zero(), ygg::BitsetSpan<uint64_t>::npos);
    EXPECT_EQ(bitset.find_next_zero(69), ygg::BitsetSpan<uint64_t>::npos);
}

TEST(YggdrasilTests, CommonDynamicBitsetRejectsMismatchedSpanSizes)
{
    auto lhs_blocks = std::vector<uint64_t>(ygg::BitsetSpan<uint64_t>::num_blocks(8), 0);
    auto rhs_blocks = std::vector<uint64_t>(ygg::BitsetSpan<uint64_t>::num_blocks(9), 0);

    auto lhs = ygg::BitsetSpan<uint64_t>(lhs_blocks.data(), 8);
    auto rhs = ygg::BitsetSpan<uint64_t>(rhs_blocks.data(), 9);

    EXPECT_FALSE(lhs == rhs);
    EXPECT_THROW(lhs.intersects(rhs), std::invalid_argument);
    EXPECT_THROW(lhs.count_intersection(rhs), std::invalid_argument);
    EXPECT_THROW(lhs.is_subset_of(rhs), std::invalid_argument);
    EXPECT_THROW(lhs.is_proper_subset_of(rhs), std::invalid_argument);
    EXPECT_THROW(lhs.is_superset_of(rhs), std::invalid_argument);
    EXPECT_THROW(lhs.is_proper_superset_of(rhs), std::invalid_argument);
    EXPECT_THROW(lhs.copy_from(rhs), std::invalid_argument);
    EXPECT_THROW(lhs.diff_from(rhs), std::invalid_argument);
    EXPECT_THROW(lhs &= rhs, std::invalid_argument);
    EXPECT_THROW(lhs |= rhs, std::invalid_argument);
    EXPECT_THROW(lhs ^= rhs, std::invalid_argument);
    EXPECT_THROW(lhs -= rhs, std::invalid_argument);
    EXPECT_THROW(ygg::for_each_bit([](size_t) {}, [](uint64_t left, uint64_t right) { return left & right; }, lhs, rhs), std::invalid_argument);
}

TEST(YggdrasilTests, CommonDynamicBitsetBoostHelpersTreatOutOfRangeTestAsFalseAndResizeOnSet)
{
    auto bitset = boost::dynamic_bitset<>();

    EXPECT_FALSE(ygg::test(3, bitset));

    ygg::set(3, true, bitset);
    EXPECT_EQ(bitset.size(), 4);
    EXPECT_TRUE(ygg::test(3, bitset));

    ygg::set(6, false, bitset);
    EXPECT_EQ(bitset.size(), 7);
    EXPECT_FALSE(ygg::test(6, bitset));
    EXPECT_TRUE(ygg::test(3, bitset));
}

TEST(YggdrasilTests, CommonDynamicBitsetTrimTrailingZeros)
{
    auto bitset = boost::dynamic_bitset<>(10);
    bitset.set(2);
    bitset.set(7);

    ygg::trim_trailing_zeros(bitset);
    EXPECT_EQ(bitset.size(), 8);

    bitset.reset(7);
    ygg::trim_trailing_zeros(bitset);
    EXPECT_EQ(bitset.size(), 3);
    EXPECT_TRUE(bitset.test(2));

    bitset.reset();
    ygg::trim_trailing_zeros(bitset);
    EXPECT_TRUE(bitset.empty());
}

TEST(YggdrasilTests, CommonDynamicBitsetAdaptersHashAndCompareSpans)
{
    auto lhs_blocks = std::vector<uint64_t>(ygg::BitsetSpan<uint64_t>::num_blocks(8), 0);
    auto rhs_blocks = std::vector<uint64_t>(ygg::BitsetSpan<uint64_t>::num_blocks(8), 0);

    auto lhs = ygg::BitsetSpan<uint64_t>(lhs_blocks.data(), 8);
    auto rhs = ygg::BitsetSpan<uint64_t>(rhs_blocks.data(), 8);

    lhs.set(1);
    rhs.set(1);

    EXPECT_TRUE(ygg::EqualTo<ygg::BitsetSpan<uint64_t>> {}(lhs, rhs));
    EXPECT_EQ(ygg::Hash<ygg::BitsetSpan<uint64_t>> {}(lhs), ygg::Hash<ygg::BitsetSpan<uint64_t>> {}(rhs));

    rhs.set(2);

    EXPECT_FALSE(ygg::EqualTo<ygg::BitsetSpan<uint64_t>> {}(lhs, rhs));
    EXPECT_NE(ygg::Hash<ygg::BitsetSpan<uint64_t>> {}(lhs), ygg::Hash<ygg::BitsetSpan<uint64_t>> {}(rhs));
    EXPECT_EQ(fmt::format("{}", lhs), "{1}");
}

static_assert(std::ranges::forward_range<SetBitIndices<uint64_t>>);
static_assert(std::ranges::borrowed_range<SetBitIndices<uint64_t>>);
static_assert(std::same_as<std::ranges::range_value_t<SetBitIndices<uint64_t>>, size_t>);

TEST(YggdrasilTests, CommonDynamicBitsetCountsIntersectionsAcrossWordBoundaries)
{
    for (const auto size : { 0U, 1U, 63U, 64U, 65U, 130U })
    {
        auto lhs_blocks = std::vector<uint64_t>(BitsetSpan<uint64_t>::num_blocks(size));
        auto rhs_blocks = lhs_blocks;
        auto lhs = BitsetSpan<uint64_t>(lhs_blocks.data(), size);
        auto rhs = BitsetSpan<uint64_t>(rhs_blocks.data(), size);
        EXPECT_EQ(lhs.count_intersection(rhs), 0);
        lhs.set();
        rhs.set();
        EXPECT_EQ(lhs.count_intersection(BitsetSpan<const uint64_t>(rhs)), size);
        lhs.reset();
        rhs.reset();
        size_t expected = 0;
        for (size_t i = 0; i < size; ++i)
        {
            lhs.set(i, i % 2 == 0);
            rhs.set(i, i % 3 == 0);
            expected += i % 6 == 0;
        }
        EXPECT_EQ(BitsetSpan<const uint64_t>(lhs).count_intersection(rhs), expected);
        rhs.copy_from(lhs);
        rhs.flip();
        EXPECT_EQ(lhs.count_intersection(rhs), 0);
    }
}

TEST(YggdrasilTests, CommonDynamicBitsetSetIndicesBorrowStorageNotWrappers)
{
    auto blocks = std::array<uint64_t, 3> {};
    auto bits = BitsetSpan<uint64_t>(blocks.data(), 130);
    for (const auto position : { 0U, 63U, 64U, 65U, 129U })
        bits.set(position);
    const auto expected = std::array<size_t, 5> { 0, 63, 64, 65, 129 };
    EXPECT_TRUE(std::ranges::equal(set_bit_indices(bits), expected));
    EXPECT_TRUE(std::ranges::equal(set_bit_indices(BitsetSpan<const uint64_t>(bits)), expected));

    auto iterator = set_bit_indices(BitsetSpan<const uint64_t>(blocks.data(), 130)).begin();
    const auto end = set_bit_indices(bits).end();
    auto copy = iterator;
    EXPECT_EQ(*copy, 0);
    EXPECT_EQ(*iterator++, 0);
    EXPECT_EQ(*iterator, 63);
    EXPECT_EQ(*copy, 0);
    ++copy;
    EXPECT_EQ(copy, iterator);
    while (iterator != end)
        ++iterator;
    EXPECT_EQ(iterator, end);
    EXPECT_TRUE(SetBitIndices<uint64_t>().empty());
    EXPECT_TRUE(set_bit_indices(BitsetSpan<const uint64_t>(nullptr, 0)).empty());
    auto other_blocks = blocks;
    EXPECT_NE(set_bit_indices(bits).begin(), set_bit_indices(BitsetSpan<const uint64_t>(other_blocks.data(), 130)).begin());
}

TEST(YggdrasilTests, CommonDynamicBitsetSetIndicesSkipEmptyWordsAndPreserveEndIdentity)
{
    const auto check = []<typename Block>()
    {
        using Bits = BitsetSpan<Block>;
        using Iterator = typename SetBitIndices<Block>::Iterator;
        constexpr size_t size = Bits::Digits * 4 + 3;
        auto blocks = std::vector<Block>(Bits::num_blocks(size));
        auto bits = Bits(blocks.data(), size);
        const auto expected = std::array<size_t, 4> { 0, Bits::Digits - 1, Bits::Digits * 3 + 2, size - 1 };
        for (const auto position : expected)
            bits.set(position);
        EXPECT_TRUE(std::ranges::equal(set_bit_indices(bits), expected));
        auto iterator = set_bit_indices(bits).begin();
        for (const auto position : expected)
        {
            auto copy = iterator;
            EXPECT_EQ(*iterator, position);
            ++iterator;
            EXPECT_EQ(*copy, position);
            ++copy;
            EXPECT_EQ(copy, iterator);
        }
        EXPECT_EQ(iterator, std::default_sentinel);
        EXPECT_EQ(iterator, Iterator(bits, Bits::npos));
        EXPECT_EQ(Iterator(), std::default_sentinel);

        bits.set();
        EXPECT_TRUE(std::ranges::equal(set_bit_indices(bits), std::views::iota(size_t { 0 }, size)));
        bits.reset();
        bits.set(size - 1);
        EXPECT_TRUE(std::ranges::equal(set_bit_indices(bits), std::array<size_t, 1> { size - 1 }));
        bits.reset();
        EXPECT_TRUE(set_bit_indices(bits).empty());
    };
    check.template operator()<uint8_t>();
    check.template operator()<uint32_t>();
    check.template operator()<uint64_t>();
}

}  // namespace ygg::tests
