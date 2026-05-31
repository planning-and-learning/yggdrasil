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

#include <gtest/gtest.h>
#include <yggdrasil/containers/block_array_set.hpp>
#include <yggdrasil/core/config.hpp>

#include <array>
#include <span>
#include <vector>

namespace ygg::tests
{

TEST(YggdrasilTests, CommonBlockArrayViewReportsEmpty)
{
    auto storage = std::array<uint8_t, 2> { 1, 2 };
    auto view = ygg::BasicBlockArrayView<uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>>(storage.data(), storage.size());
    auto empty_view = ygg::BasicBlockArrayView<uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>>(storage.data(), 0);

    EXPECT_EQ(view.size(), 2);
    EXPECT_FALSE(view.empty());
    EXPECT_EQ(empty_view.size(), 0);
    EXPECT_TRUE(empty_view.empty());
}

TEST(YggdrasilTests, CommonBlockArraySetOutOfRange)
{
    auto set = BlockArraySet<ygg::uint_t, bit::ForwardingBlockCoder<ygg::uint_t>, 1>(2);

    // 3 elements are too much for a pool that stores arrays of length 2.
    EXPECT_THROW(set.insert(std::vector<ygg::uint_t>({ 1, 1, 1 })), std::invalid_argument);
    EXPECT_THROW(set.find(std::vector<ygg::uint_t>({ 1, 1, 1 })), std::invalid_argument);
    EXPECT_THROW(set.contains(std::vector<ygg::uint_t>({ 1 })), std::invalid_argument);
}

TEST(YggdrasilTests, CommonBlockArraySet)
{
    auto set = BlockArraySet<uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>, 1>(2);

    for (size_t i = 0; i < 5; ++i)
    {
        set.clear();

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
                EXPECT_TRUE(set.contains_with_hash(value, BlockArraySet<uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>, 1>::hash(value)));

                const auto [i2, inserted2] = set.insert(value);
                EXPECT_FALSE(inserted2);
                EXPECT_EQ(i2, idx);
                EXPECT_EQ(set[i1], std::span<const uint8_t>(value));
            }
        }

        const auto first = std::array<uint8_t, 2> { 0, 0 };
        EXPECT_EQ(set.front(), std::span<const uint8_t>(first));
        EXPECT_EQ(set.size(), 16);
        EXPECT_EQ(set.segments().size(), 5);
        EXPECT_EQ(set.segments()[0].size(), 2);   /// capacity 1 array stores 2 uint8_t blocks.
        EXPECT_EQ(set.segments()[1].size(), 4);   /// capacity 2 arrays store 4 uint8_t blocks.
        EXPECT_EQ(set.segments()[2].size(), 8);   /// capacity 4 arrays store 8 uint8_t blocks.
        EXPECT_EQ(set.segments()[3].size(), 16);  /// capacity 8 arrays store 16 uint8_t blocks.
        EXPECT_EQ(set.segments()[4].size(), 32);  /// capacity 16 arrays store 32 uint8_t blocks.
        EXPECT_EQ(set.capacity(), 31);
    }
}

}
