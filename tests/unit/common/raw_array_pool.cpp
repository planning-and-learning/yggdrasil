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
#include <yggdrasil/containers/raw_array_pool.hpp>

#include <algorithm>
#include <array>
#include <vector>

namespace ygg::tests
{

TEST(YggdrasilTests, CommonRawArrayPoolStoresFixedLengthArrays)
{
    auto pool = ygg::RawArrayPool<int, 2>(3);

    EXPECT_TRUE(pool.empty());
    EXPECT_EQ(pool.size(), 0);
    EXPECT_EQ(pool.array_size(), 3);

    auto* first = pool.allocate();
    std::ranges::copy(std::array<int, 3> { 1, 2, 3 }, first);

    auto* second = pool.allocate();
    std::ranges::copy(std::array<int, 3> { 4, 5, 6 }, second);

    EXPECT_FALSE(pool.empty());
    EXPECT_EQ(pool.size(), 2);
    EXPECT_EQ(std::vector<int>(pool[0], pool[0] + pool.array_size()), (std::vector<int> { 1, 2, 3 }));
    EXPECT_EQ(std::vector<int>(pool.front(), pool.front() + pool.array_size()), (std::vector<int> { 1, 2, 3 }));
    EXPECT_EQ(std::vector<int>(pool.back(), pool.back() + pool.array_size()), (std::vector<int> { 4, 5, 6 }));
    EXPECT_EQ(std::vector<int>(pool[1], pool[1] + pool.array_size()), (std::vector<int> { 4, 5, 6 }));
}

TEST(YggdrasilTests, CommonRawArrayPoolClearKeepsCapacityReusable)
{
    auto pool = ygg::RawArrayPool<int, 1>(2);

    auto* first = pool.allocate();
    std::ranges::copy(std::array<int, 2> { 1, 2 }, first);

    pool.clear();

    EXPECT_TRUE(pool.empty());
    EXPECT_EQ(pool.size(), 0);

    auto* second = pool.allocate();
    std::ranges::copy(std::array<int, 2> { 3, 4 }, second);

    EXPECT_FALSE(pool.empty());
    EXPECT_EQ(pool.size(), 1);
    EXPECT_EQ(std::vector<int>(pool[0], pool[0] + pool.array_size()), (std::vector<int> { 3, 4 }));
}

}
