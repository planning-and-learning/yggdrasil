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
#include <gtest/gtest.h>
#include <span>
#include <stdexcept>
#include <vector>
#include <yggdrasil/containers/bit_packed_array_set.hpp>
#include <yggdrasil/core/config.hpp>

namespace ygg::tests
{

TEST(YggdrasilTests, CommonBitPackedArraySetOutOfRange)
{
    auto set = ygg::BitPackedArraySet<ygg::uint_t, ygg::bit::ForwardingBlockCoder<ygg::uint_t>, 1>(2, 2);

    // 4 requires width 3, which exceeds the limit of 2.
    EXPECT_THROW(set.insert(std::vector<ygg::uint_t>({ 1, 4 })), std::out_of_range);
    EXPECT_EQ(set.size(), 0);

    const auto [index, inserted] = set.insert(std::vector<ygg::uint_t>({ 1, 3 }));
    EXPECT_TRUE(inserted);
    EXPECT_EQ(index, 0);

    // 3 elements are too much for a pool that stores arrays of length 2.
    EXPECT_THROW(set.insert(std::vector<ygg::uint_t>({ 1, 1, 1 })), std::invalid_argument);
    EXPECT_THROW(set.find(std::vector<ygg::uint_t>({ 1, 1, 1 })), std::invalid_argument);
    EXPECT_THROW(set.contains(std::vector<ygg::uint_t>({ 1 })), std::invalid_argument);
}

TEST(YggdrasilTests, CommonBitPackedArraySetDeduplicatesZeroLengthArrays)
{
    auto set = ygg::BitPackedArraySet<uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>, 1>(0, 1);
    const auto empty = std::vector<uint8_t> {};

    const auto [first_index, first_inserted] = set.insert(empty);
    const auto [second_index, second_inserted] = set.insert(empty);

    EXPECT_TRUE(first_inserted);
    EXPECT_FALSE(second_inserted);
    EXPECT_EQ(first_index, 0);
    EXPECT_EQ(second_index, first_index);
    EXPECT_TRUE(set.contains(empty));
    EXPECT_EQ(set.find(empty), first_index);
    EXPECT_EQ(set.size(), 1);
    EXPECT_EQ(set.length(), 0);
    EXPECT_EQ(set.width(), 1);
    EXPECT_TRUE(set[first_index].empty());
}

TEST(YggdrasilTests, CommonBitPackedArraySetSupportsHashAwareInsertion)
{
    using Set = ygg::BitPackedArraySet<uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>, 1>;
    auto set = Set(2, 3);
    const auto first = std::array<uint8_t, 2> { 1, 2 };
    const auto second = std::array<uint8_t, 2> { 3, 4 };
    const auto first_hash = Set::hash(first);
    const auto second_hash = Set::hash(second);

    EXPECT_EQ(set.find_with_hash(first, first_hash), std::nullopt);

    const auto [first_index, first_inserted] = set.insert_with_hash(first_hash, first);
    EXPECT_TRUE(first_inserted);
    EXPECT_EQ(first_index, 0);
    EXPECT_TRUE(set.contains_with_hash(first, first_hash));
    EXPECT_EQ(set.find_with_hash(first, first_hash), first_index);

    const auto [duplicate_index, duplicate_inserted] = set.insert_with_hash(first_hash, first);
    EXPECT_FALSE(duplicate_inserted);
    EXPECT_EQ(duplicate_index, first_index);

    EXPECT_EQ(set.insert_new_with_hash(second_hash, second), 1);
    EXPECT_EQ(set.find_with_hash(second, second_hash), 1);
}

TEST(YggdrasilTests, CommonBitPackedArraySetEmptyAccessThrows)
{
    auto set = ygg::BitPackedArraySet<uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>, 1>(2, 3);

    EXPECT_THROW(set.front(), std::out_of_range);
    EXPECT_THROW(set.back(), std::out_of_range);
}

TEST(YggdrasilTests, CommonBitPackedArraySet)
{
    auto set = ygg::BitPackedArraySet<uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>, 1>(2, 3);

    // Repeat 5 times on cleared pool.
    for (size_t i = 0; i < 5; ++i)
    {
        set.clear();

        // Write
        for (uint8_t a = 0; a < 4; ++a)
        {
            for (uint8_t b = 0; b < 4; ++b)
            {
                const size_t idx = a * 4 + b;

                const auto value = std::vector<uint8_t>({ a, b });

                const auto [i1, inserted1] = set.insert(value);
                EXPECT_EQ(i1, idx);
                EXPECT_TRUE(inserted1);
                EXPECT_TRUE(set.contains(value));
                EXPECT_EQ(set.find(value), i1);
                EXPECT_TRUE(set.contains_with_hash(value, ygg::BitPackedArraySet<uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>, 1>::hash(value)));

                const auto [i2, inserted2] = set.insert(value);
                EXPECT_FALSE(inserted2);
                EXPECT_EQ(i2, idx);
                EXPECT_EQ(set[i1], std::span<const uint8_t>(value));
                EXPECT_EQ(set.at(i1), std::span<const uint8_t>(value));
            }
        }

        EXPECT_THROW(set.at(set.size()), std::out_of_range);

        const auto first = std::array<uint8_t, 2> { 0, 0 };
        const auto last = std::array<uint8_t, 2> { 3, 3 };
        EXPECT_EQ(set.front(), std::span<const uint8_t>(first));
        EXPECT_EQ(set.back(), std::span<const uint8_t>(last));

        // 16 arrays stored.
        EXPECT_EQ(set.size(), 16);
        EXPECT_EQ(set.segments().size(), 5);
        EXPECT_EQ(set.segments()[0].size(),
                  1);  /// capacity 1 array requires 6 bits   = 1  uint8_t
        EXPECT_EQ(set.segments()[1].size(),
                  2);  /// capacity 2 arrays require 12 bits  = 2  uint8_t
        EXPECT_EQ(set.segments()[2].size(),
                  3);  /// capacity 4 arrays require 24 bits  = 3  uint8_t
        EXPECT_EQ(set.segments()[3].size(),
                  6);  /// capacity 8 arrays require 48 bits  = 6  uint8_t
        EXPECT_EQ(set.segments()[4].size(),
                  12);  /// capacity 16 arrays require 96 bits = 12 uint8_t
        // 2^{5+1} - 1 = 31
        EXPECT_EQ(set.capacity(), 31);
        EXPECT_GT(set.memory_usage(), 0);
    }
}

}  // namespace ygg::tests
