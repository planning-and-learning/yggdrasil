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
#include <concepts>
#include <gtest/gtest.h>
#include <limits>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>
#include <yggdrasil/containers/raw_array_pool.hpp>

namespace ygg::tests
{

template<typename Pool>
concept HasAllocate = requires(Pool& pool) { pool.allocate(); };

using DefaultRawArrayPool = RawArrayPool<int, 2>;
using ConcurrentRawArrayPool = RawArrayPool<int, 2, true>;

static_assert(!DefaultRawArrayPool::thread_safe);
static_assert(ConcurrentRawArrayPool::thread_safe);
static_assert(!HasAllocate<DefaultRawArrayPool>);
static_assert(std::same_as<decltype(std::declval<DefaultRawArrayPool&>()[0]), std::span<const int>>);
static_assert(std::same_as<decltype(std::declval<ConcurrentRawArrayPool&>()[0]), std::span<const int>>);
static_assert(std::is_copy_constructible_v<DefaultRawArrayPool>);
static_assert(std::is_copy_assignable_v<DefaultRawArrayPool>);
static_assert(!std::is_copy_constructible_v<ConcurrentRawArrayPool>);

TEST(YggdrasilTests, CommonRawArrayPoolRejectsImpossibleSegmentSizes)
{
    constexpr auto first_segment_size = size_t { 2 };
    constexpr auto too_large = std::numeric_limits<size_t>::max() / first_segment_size + 1;

    EXPECT_THROW((ygg::RawArrayPool<int, first_segment_size>(too_large)), std::length_error);
}

TEST(YggdrasilTests, CommonRawArrayPoolStoresFixedLengthArrays)
{
    auto pool = ygg::RawArrayPool<int, 2>(3);

    EXPECT_TRUE(pool.empty());
    EXPECT_EQ(pool.size(), 0);
    EXPECT_EQ(pool.array_size(), 3);

    const auto first = std::array<int, 3> { 1, 2, 3 };
    const auto second = std::array<int, 3> { 4, 5, 6 };
    EXPECT_EQ(pool.insert(first), 0);
    EXPECT_EQ(pool.insert(second), 1);

    EXPECT_FALSE(pool.empty());
    EXPECT_EQ(pool.size(), 2);
    EXPECT_TRUE(std::ranges::equal(pool[0], first));
    EXPECT_TRUE(std::ranges::equal(pool.front(), first));
    EXPECT_TRUE(std::ranges::equal(pool.back(), second));
    EXPECT_TRUE(std::ranges::equal(pool[1], second));
}

TEST(YggdrasilTests, CommonRawArrayPoolRejectsWrongLengthWithoutPublishing)
{
    auto pool = ygg::RawArrayPool<int, 2>(3);

    EXPECT_THROW(pool.insert(std::array<int, 2> { 1, 2 }), std::invalid_argument);
    EXPECT_TRUE(pool.empty());
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
    const auto first = std::array<int, 2> { 1, 2 };
    EXPECT_EQ(pool.insert(first), 0);
    const auto& const_pool = pool;

    EXPECT_TRUE(std::ranges::equal(pool.at(0), first));
    EXPECT_TRUE(std::ranges::equal(const_pool.at(0), first));
    EXPECT_THROW(pool.at(1), std::out_of_range);
    EXPECT_THROW(const_pool.at(1), std::out_of_range);
}

TEST(YggdrasilTests, CommonRawArrayPoolStoresZeroLengthArrays)
{
    auto pool = ygg::RawArrayPool<int, 2>(0);

    EXPECT_EQ(pool.array_size(), 0);
    EXPECT_TRUE(pool.empty());

    const auto empty = std::array<int, 0> {};
    EXPECT_EQ(pool.insert(empty), 0);
    EXPECT_EQ(pool.insert(empty), 1);

    EXPECT_EQ(pool.size(), 2);
    EXPECT_FALSE(pool.empty());
    EXPECT_TRUE(pool[0].empty());
    EXPECT_EQ(pool[0].data(), nullptr);
    EXPECT_TRUE(pool.front().empty());
    EXPECT_TRUE(pool.back().empty());
}

TEST(YggdrasilTests, CommonRawArrayPoolClearKeepsCapacityReusable)
{
    auto pool = ygg::RawArrayPool<int, 1>(2);

    EXPECT_EQ(pool.insert(std::array<int, 2> { 1, 2 }), 0);

    pool.clear();

    EXPECT_TRUE(pool.empty());
    EXPECT_EQ(pool.size(), 0);

    const auto second = std::array<int, 2> { 3, 4 };
    EXPECT_EQ(pool.insert(second), 0);

    EXPECT_FALSE(pool.empty());
    EXPECT_EQ(pool.size(), 1);
    EXPECT_TRUE(std::ranges::equal(pool[0], second));
}

TEST(YggdrasilTests, CommonRawArrayPoolGrowthKeepsPointersStableAndCopiesRemainIndependent)
{
    auto pool = ygg::RawArrayPool<int, 1>(2);
    const auto first_value = std::array<int, 2> { 1, 2 };
    const auto second_value = std::array<int, 2> { 3, 4 };
    const auto third_value = std::array<int, 2> { 5, 6 };
    EXPECT_EQ(pool.insert(first_value), 0);
    const auto* first_storage = pool[0].data();
    EXPECT_EQ(pool.insert(second_value), 1);
    EXPECT_EQ(pool.insert(third_value), 2);

    EXPECT_EQ(first_storage, pool[0].data());
    EXPECT_TRUE(std::ranges::equal(pool[0], first_value));
    EXPECT_TRUE(std::ranges::equal(pool[2], third_value));
    EXPECT_EQ(pool.memory_usage(), 6 * sizeof(int));

    auto copy = pool;
    EXPECT_NE(copy[0].data(), pool[0].data());
    EXPECT_TRUE(std::ranges::equal(copy[0], first_value));
    EXPECT_EQ(copy.memory_usage(), pool.memory_usage());

    auto assigned = ygg::RawArrayPool<int, 1>(2);
    assigned = pool;
    EXPECT_NE(assigned[1].data(), pool[1].data());
    EXPECT_TRUE(std::ranges::equal(assigned[1], second_value));
    EXPECT_TRUE(std::ranges::equal(assigned[2], third_value));
    EXPECT_EQ(assigned.memory_usage(), pool.memory_usage());
}

}  // namespace ygg::tests
