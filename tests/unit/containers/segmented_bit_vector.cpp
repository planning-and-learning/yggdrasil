/*
 * Copyright (C) 2026 Dominik Drexler
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

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <ranges>
#include <stdexcept>
#include <utility>
#include <yggdrasil/containers/segmented_bit_vector.hpp>

namespace ygg::tests
{
TEST(CommonSegmentedBitVectorTest, SupportsPackedRandomAccessAcrossSegments)
{
    using Vector = SegmentedBitVector<uint8_t, 2>;

    static_assert(std::same_as<Vector::value_type, bool>);
    static_assert(std::same_as<decltype(std::declval<Vector&>()[0]), Vector::reference>);
    static_assert(std::same_as<decltype(std::declval<const Vector&>()[0]), bool>);
    static_assert(std::random_access_iterator<Vector::iterator>);
    static_assert(std::random_access_iterator<Vector::const_iterator>);
    static_assert(std::ranges::random_access_range<Vector>);
    static_assert(std::ranges::random_access_range<const Vector>);

    auto bits = Vector {};
    for (size_t i = 0; i < 9; ++i)
        bits.push_back(i % 3 == 0);

    EXPECT_EQ(bits.size(), 9);
    EXPECT_EQ(bits.capacity(), 14);
    EXPECT_GT(bits.memory_usage(), 0);
    EXPECT_TRUE(bits.front());
    EXPECT_FALSE(bits.back());

    bits[1] = true;
    bits.at(3) = false;

    EXPECT_TRUE(bits[1]);
    EXPECT_FALSE(bits[3]);
    EXPECT_EQ(bits.end() - bits.begin(), 9);
    EXPECT_TRUE(bits.begin()[1]);
    EXPECT_TRUE(*(6 + bits.begin()));
    EXPECT_TRUE(std::as_const(bits)[6]);
    EXPECT_TRUE(*std::as_const(bits).cbegin());
}

TEST(CommonSegmentedBitVectorTest, ResizeOverwritesHiddenBitsAndRetainsCapacity)
{
    auto bits = SegmentedBitVector<uint16_t, 2> {};
    bits.resize(10, true);

    EXPECT_EQ(bits.size(), 10);
    for (bool bit : std::as_const(bits))
        EXPECT_TRUE(bit);

    const auto capacity = bits.capacity();
    bits.resize(2);
    bits.resize(10);

    EXPECT_EQ(bits.capacity(), capacity);
    EXPECT_TRUE(bits[0]);
    EXPECT_TRUE(bits[1]);
    for (size_t i = 2; i < bits.size(); ++i)
        EXPECT_FALSE(bits[i]);

    bits.resize(17, true);
    for (size_t i = 10; i < bits.size(); ++i)
        EXPECT_TRUE(bits[i]);

    bits.resize(0);
    EXPECT_TRUE(bits.empty());
    EXPECT_EQ(bits.capacity(), 30);

    bits.resize(4);
    for (bool bit : std::as_const(bits))
        EXPECT_FALSE(bit);
}

TEST(CommonSegmentedBitVectorTest, PopClearAndCheckedAccessMatchVectorSemantics)
{
    auto bits = SegmentedBitVector<> {};
    bits.push_back(false);
    bits.push_back(true);
    bits.pop_back();

    EXPECT_EQ(bits.size(), 1);
    EXPECT_FALSE(bits.back());
    EXPECT_THROW(bits.at(1), std::out_of_range);

    bits.clear();
    EXPECT_TRUE(bits.empty());
    EXPECT_THROW(bits.front(), std::out_of_range);
    EXPECT_THROW(bits.back(), std::out_of_range);
    EXPECT_THROW(bits.pop_back(), std::out_of_range);
}

TEST(CommonSegmentedBitVectorTest, SupportsUnsignedBlockWidths)
{
    const auto check = []<typename Block>()
    {
        auto bits = SegmentedBitVector<Block, 1> {};
        bits.resize(3, true);
        bits[1] = false;
        EXPECT_TRUE(bits[0]);
        EXPECT_FALSE(bits[1]);
        EXPECT_TRUE(bits[2]);
    };

    check.template operator()<uint8_t>();
    check.template operator()<uint16_t>();
    check.template operator()<uint32_t>();
    check.template operator()<uint64_t>();
}
}  // namespace ygg::tests
