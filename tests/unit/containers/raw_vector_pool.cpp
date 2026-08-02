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
#include <cstdint>
#include <gtest/gtest.h>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>
#include <yggdrasil/containers/raw_vector_pool.hpp>

namespace ygg::tests
{

using DefaultRawVectorPool = RawVectorPool<uint8_t, int, 32>;
using ConcurrentRawVectorPool = RawVectorPool<uint8_t, int, 32, true>;

struct alignas(64) OverAlignedValue
{
    int value;
};

static_assert(!DefaultRawVectorPool::thread_safe);
static_assert(ConcurrentRawVectorPool::thread_safe);
static_assert(std::same_as<decltype(std::declval<DefaultRawVectorPool&>()[0]), RawVectorView<const uint8_t, const int>>);
static_assert(std::same_as<decltype(std::declval<ConcurrentRawVectorPool&>()[0]), RawVectorView<const uint8_t, const int>>);

TEST(YggdrasilTests, CommonRawVectorPoolRejectsInvalidInsertArguments)
{
    auto pool = ygg::RawVectorPool<uint8_t, int, 32>();
    const auto too_large = std::vector<int>(static_cast<size_t>(std::numeric_limits<uint8_t>::max()) + 1);
    const auto value = int { 0 };

    EXPECT_THROW(pool.insert(too_large), std::out_of_range);
    EXPECT_THROW(pool.insert(nullptr, 1), std::invalid_argument);
    EXPECT_NO_THROW(pool.insert(nullptr, 0));

    auto huge_pool = ygg::RawVectorPool<size_t, int, 32>();
    const auto impossible_size = std::numeric_limits<size_t>::max() / sizeof(int) + 1;
    EXPECT_THROW(huge_pool.insert(&value, impossible_size), std::length_error);
}

TEST(YggdrasilTests, CommonRawVectorViewReportsWhetherItReferencesStorage)
{
    auto empty_mutable_view = ygg::RawVectorView<uint8_t, int> {};
    auto empty_const_view = ygg::RawVectorView<const uint8_t, const int> {};

    EXPECT_FALSE(empty_mutable_view.valid());
    EXPECT_FALSE(empty_mutable_view);
    EXPECT_EQ(empty_mutable_view.raw_data(), nullptr);
    EXPECT_FALSE(empty_const_view.valid());
    EXPECT_FALSE(empty_const_view);
    EXPECT_EQ(empty_const_view.raw_data(), nullptr);
    EXPECT_THROW(empty_mutable_view.at(0), std::logic_error);
    EXPECT_THROW(empty_const_view.at(0), std::logic_error);
    EXPECT_THROW(empty_mutable_view.front(), std::logic_error);
    EXPECT_THROW(empty_const_view.front(), std::logic_error);
    EXPECT_THROW(empty_mutable_view.back(), std::logic_error);
    EXPECT_THROW(empty_const_view.back(), std::logic_error);

    auto pool = ygg::RawVectorPool<uint8_t, int, 32>();
    const auto index = pool.insert(std::vector<int> { 1, 2 });
    const auto view = pool[index];
    const auto const_view = std::as_const(pool)[index];
    using View = decltype(view);
    using ConstView = decltype(const_view);
    static_assert(std::same_as<View, ConstView>);

    EXPECT_TRUE(view.valid());
    EXPECT_TRUE(view);
    EXPECT_NE(view.raw_data(), nullptr);
    EXPECT_TRUE(const_view.valid());
    EXPECT_TRUE(const_view);
    EXPECT_NE(const_view.raw_data(), nullptr);
    EXPECT_EQ(const_view.raw_data(), view.raw_data());
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

TEST(YggdrasilTests, CommonRawVectorPoolEmptyAccessThrows)
{
    auto pool = ygg::RawVectorPool<uint8_t, int, 32>();
    const auto& const_pool = pool;

    EXPECT_THROW(pool.front(), std::out_of_range);
    EXPECT_THROW(const_pool.front(), std::out_of_range);
    EXPECT_THROW(pool.back(), std::out_of_range);
    EXPECT_THROW(const_pool.back(), std::out_of_range);

    const auto empty_index = pool.insert(std::vector<int> {});
    const auto empty_view = pool[empty_index];
    const auto empty_const_view = const_pool[empty_index];

    EXPECT_THROW(empty_view.front(), std::out_of_range);
    EXPECT_THROW(empty_const_view.front(), std::out_of_range);
    EXPECT_THROW(empty_view.back(), std::out_of_range);
    EXPECT_THROW(empty_const_view.back(), std::out_of_range);
}

TEST(YggdrasilTests, CommonRawVectorPoolAtChecksBounds)
{
    auto pool = ygg::RawVectorPool<uint8_t, int, 32>();
    const auto index = pool.insert(std::vector<int> { 1, 2 });
    const auto& const_pool = pool;

    EXPECT_EQ(pool.at(index).at(1), 2);
    EXPECT_EQ(const_pool.at(index).at(1), 2);
    EXPECT_THROW(pool.at(1), std::out_of_range);
    EXPECT_THROW(const_pool.at(1), std::out_of_range);
    EXPECT_THROW(pool.at(index).at(2), std::out_of_range);
    EXPECT_THROW(const_pool.at(index).at(2), std::out_of_range);
}

TEST(YggdrasilTests, CommonRawVectorPoolClearKeepsCapacityReusable)
{
    auto pool = ygg::RawVectorPool<uint8_t, int, 32>();
    const auto value = std::vector<int> { 1, 2 };

    for (size_t i = 0; i < 7; ++i)
        EXPECT_EQ(pool.insert(value), i);
    const auto memory_usage = pool.memory_usage();
    pool.clear();

    EXPECT_TRUE(pool.empty());
    EXPECT_EQ(pool.size(), 0);

    for (size_t i = 0; i < 7; ++i)
        EXPECT_EQ(pool.insert(value), i);

    const auto view = pool[0];
    EXPECT_FALSE(pool.empty());
    EXPECT_EQ(pool.size(), 7);
    EXPECT_FALSE(view.empty());
    EXPECT_EQ(std::vector<int>(view.begin(), view.end()), value);
    EXPECT_EQ(pool.memory_usage(), memory_usage);
}

TEST(YggdrasilTests, CommonRawVectorPoolGrowthKeepsViewsStableAndAligned)
{
    auto pool = ygg::RawVectorPool<uint8_t, int, 16>();
    const auto first_index = pool.insert(std::array<int, 1> { 7 });
    const auto first = pool[first_index];
    const auto* first_storage = first.raw_data();
    const auto large = std::array<int, 8> { 1, 2, 3, 4, 5, 6, 7, 8 };

    static_cast<void>(pool.insert(large));
    const auto memory_usage = pool.memory_usage();
    static_cast<void>(pool.insert(large));

    EXPECT_EQ(pool[first_index].raw_data(), first_storage);
    EXPECT_EQ(first.front(), 7);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(first.data()) % alignof(int), 0);
    EXPECT_EQ(pool.memory_usage() - memory_usage, 128);
}

TEST(YggdrasilTests, CommonRawVectorPoolPreservesOverAlignment)
{
    auto pool = ygg::RawVectorPool<uint8_t, OverAlignedValue, 64>();
    const auto index = pool.insert(std::array<OverAlignedValue, 1> { OverAlignedValue { 7 } });

    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(pool[index].data()) % alignof(OverAlignedValue), 0);
    EXPECT_EQ(pool[index].front().value, 7);
}

}  // namespace ygg::tests
