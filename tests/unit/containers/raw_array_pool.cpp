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

#include <algorithm>
#include <array>
#include <gtest/gtest.h>
#include <limits>
#include <stdexcept>
#include <vector>
#include <yggdrasil/containers/raw_array_pool.hpp>

namespace ygg::tests
{

TEST(YggdrasilTests, CommonRawArrayPoolRejectsImpossibleSegmentSizes)
{
    constexpr auto arrays_per_segment = size_t { 2 };
    constexpr auto too_large = std::numeric_limits<size_t>::max() / arrays_per_segment + 1;

    EXPECT_THROW((ygg::RawArrayPool<int, arrays_per_segment>(too_large)), std::length_error);
}

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

TEST(YggdrasilTests, CommonRawArrayPoolEmptyAccessThrows)
{
    auto pool = ygg::RawArrayPool<int, 2>(3);
    const auto& const_pool = pool;

    EXPECT_THROW(pool.front(), std::out_of_range);
    EXPECT_THROW(const_pool.front(), std::out_of_range);
    EXPECT_THROW(pool.back(), std::out_of_range);
    EXPECT_THROW(const_pool.back(), std::out_of_range);
}

TEST(YggdrasilTests, CommonRawArrayPoolAtChecksBounds)
{
    auto pool = ygg::RawArrayPool<int, 2>(2);
    auto* first = pool.allocate();
    std::ranges::copy(std::array<int, 2> { 1, 2 }, first);
    const auto& const_pool = pool;

    pool.at(0)[1] = 5;

    EXPECT_EQ(std::vector<int>(pool.at(0), pool.at(0) + pool.array_size()), (std::vector<int> { 1, 5 }));
    EXPECT_EQ(std::vector<int>(const_pool.at(0), const_pool.at(0) + const_pool.array_size()), (std::vector<int> { 1, 5 }));
    EXPECT_THROW(pool.at(1), std::out_of_range);
    EXPECT_THROW(const_pool.at(1), std::out_of_range);
}

TEST(YggdrasilTests, CommonRawArrayPoolStoresZeroLengthArrays)
{
    auto pool = ygg::RawArrayPool<int, 2>(0);

    EXPECT_EQ(pool.array_size(), 0);
    EXPECT_TRUE(pool.empty());

    EXPECT_EQ(pool.allocate(), nullptr);
    EXPECT_EQ(pool.allocate(), nullptr);

    EXPECT_EQ(pool.size(), 2);
    EXPECT_FALSE(pool.empty());
    EXPECT_EQ(pool[0], nullptr);
    EXPECT_EQ(pool.front(), nullptr);
    EXPECT_EQ(pool.back(), nullptr);
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

TEST(YggdrasilTests, CommonRawArrayPoolGrowthKeepsPointersStableAndCopiesRemainIndependent)
{
    auto pool = ygg::RawArrayPool<int, 1>(2);
    auto* first = pool.allocate();
    std::ranges::copy(std::array<int, 2> { 1, 2 }, first);

    auto* second = pool.allocate();
    std::ranges::copy(std::array<int, 2> { 3, 4 }, second);

    EXPECT_EQ(first, pool[0]);
    EXPECT_EQ(std::vector<int>(first, first + pool.array_size()), (std::vector<int> { 1, 2 }));

    auto copy = pool;
    copy[0][0] = 9;
    EXPECT_EQ(pool[0][0], 1);
    EXPECT_EQ(copy[0][0], 9);
    EXPECT_EQ(copy.memory_usage(), pool.memory_usage());

    auto assigned = ygg::RawArrayPool<int, 1>(2);
    assigned = pool;
    assigned[1][1] = 8;
    EXPECT_EQ(pool[1][1], 4);
    EXPECT_EQ(assigned[1][1], 8);
    EXPECT_EQ(assigned.memory_usage(), pool.memory_usage());
}

}  // namespace ygg::tests
