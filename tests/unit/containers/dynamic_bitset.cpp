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

#include <boost/dynamic_bitset.hpp>
#include <concepts>
#include <gtest/gtest.h>
#include <stdexcept>
#include <vector>
#include <yggdrasil/containers/dynamic_bitset.hpp>
#include <yggdrasil/core/config.hpp>
#include <yggdrasil/formatting/dynamic_bitset_formatters.hpp>
#include <yggdrasil/semantics/containers/dynamic_bitset_equal_to.hpp>
#include <yggdrasil/semantics/containers/dynamic_bitset_hash.hpp>
#include <yggdrasil/semantics/containers/dynamic_bitset_ordering.hpp>

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

}  // namespace ygg::tests
