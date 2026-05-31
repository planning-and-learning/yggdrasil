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
#include <yggdrasil/containers/raw_vector_pool.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace ygg::tests
{

TEST(YggdrasilTests, CommonRawVectorPoolRejectsLengthsBeyondSizeType)
{
    auto pool = ygg::RawVectorPool<uint8_t, int, 32>();
    const auto too_large = std::vector<int>(static_cast<size_t>(std::numeric_limits<uint8_t>::max()) + 1);

    EXPECT_THROW(pool.insert(too_large), std::out_of_range);
}


TEST(YggdrasilTests, CommonRawVectorPoolStoresVariableLengthVectors)
{
    auto pool = ygg::RawVectorPool<uint8_t, int, 32>();

    EXPECT_TRUE(pool.empty());
    EXPECT_EQ(pool.size(), 0);

    const auto first = std::vector<int> { 1, 2 };
    const auto second = std::vector<int> {};
    const auto third = std::array<int, 3> { 3, 4, 5 };

    EXPECT_EQ(pool.insert(first), 0);
    EXPECT_EQ(pool.insert(second), 1);
    EXPECT_EQ(pool.insert(third), 2);

    EXPECT_FALSE(pool.empty());
    EXPECT_EQ(pool.size(), 3);

    const auto first_view = pool[0];
    EXPECT_FALSE(first_view.empty());
    EXPECT_EQ(first_view.size(), first.size());
    EXPECT_EQ(std::vector<int>(first_view.begin(), first_view.end()), first);
    EXPECT_EQ(first_view.front(), 1);
    EXPECT_EQ(pool.front().front(), 1);
    EXPECT_EQ(first_view.back(), 2);
    EXPECT_EQ(pool.back().back(), 5);
    EXPECT_TRUE(std::ranges::equal(first_view, first));

    const auto second_view = pool[1];
    EXPECT_TRUE(second_view.empty());
    EXPECT_EQ(second_view.size(), 0);
    EXPECT_EQ(second_view.begin(), second_view.end());

    const auto third_view = pool[2];
    EXPECT_FALSE(third_view.empty());
    EXPECT_EQ(std::vector<int>(third_view.begin(), third_view.end()), std::vector<int>(third.begin(), third.end()));
}

TEST(YggdrasilTests, CommonRawVectorPoolClearKeepsCapacityReusable)
{
    auto pool = ygg::RawVectorPool<uint8_t, int, 32>();

    EXPECT_EQ(pool.insert(std::vector<int> { 1, 2 }), 0);
    pool.clear();

    EXPECT_TRUE(pool.empty());
    EXPECT_EQ(pool.size(), 0);

    const auto value = std::vector<int> { 3, 4, 5 };
    EXPECT_EQ(pool.insert(value), 0);

    const auto view = pool[0];
    EXPECT_FALSE(pool.empty());
    EXPECT_EQ(pool.size(), 1);
    EXPECT_FALSE(view.empty());
    EXPECT_EQ(std::vector<int>(view.begin(), view.end()), value);
}

}
