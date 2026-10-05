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

#include "yggdrasil/containers/raw_array_pool.hpp"

#include <algorithm>
#include <array>
#include <concepts>
#include <gtest/gtest.h>
#include <iterator>
#include <limits>
#include <ranges>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

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
static_assert(!std::is_copy_constructible_v<DefaultRawArrayPool>);
static_assert(!std::is_copy_assignable_v<DefaultRawArrayPool>);
static_assert(!std::is_copy_constructible_v<ConcurrentRawArrayPool>);
static_assert(!std::is_copy_assignable_v<ConcurrentRawArrayPool>);
static_assert(std::is_nothrow_move_constructible_v<DefaultRawArrayPool>);
static_assert(std::is_nothrow_move_assignable_v<DefaultRawArrayPool>);
static_assert(std::is_nothrow_move_constructible_v<ConcurrentRawArrayPool>);
static_assert(std::is_nothrow_move_assignable_v<ConcurrentRawArrayPool>);

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

TEST(YggdrasilTests, CommonRawArrayPoolGrowthKeepsPointersStable)
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
}

template<bool ThreadSafe>
void test_raw_array_pool_move()
{
    using Pool = ygg::RawArrayPool<int, 1, ThreadSafe>;
    const auto first = std::array<int, 2> { 1, 2 };
    const auto second = std::array<int, 2> { 3, 4 };

    auto source = Pool(2);
    source.insert(first);
    source.insert(second);
    const auto first_view = source[0];

    auto moved = std::move(source);
    EXPECT_TRUE(source.empty());
    EXPECT_EQ(source.array_size(), 2);
    EXPECT_EQ(source.insert(first), 0);
    EXPECT_EQ(moved.size(), 2);
    EXPECT_EQ(moved[0].data(), first_view.data());
    EXPECT_TRUE(std::ranges::equal(first_view, first));

    auto assigned = Pool(1);
    assigned.insert(std::array<int, 1> { 9 });
    assigned = std::move(moved);
    EXPECT_TRUE(moved.empty());
    EXPECT_EQ(moved.array_size(), 2);
    EXPECT_EQ(moved.insert(second), 0);
    EXPECT_EQ(assigned.array_size(), 2);
    EXPECT_EQ(assigned.size(), 2);
    EXPECT_EQ(assigned[0].data(), first_view.data());
    EXPECT_TRUE(std::ranges::equal(assigned[1], second));
}

TEST(YggdrasilTests, CommonRawArrayPoolMovesLeaveSourcesReusable)
{
    test_raw_array_pool_move<false>();
    test_raw_array_pool_move<true>();
}

template<bool ThreadSafe>
void test_raw_array_pool_failed_fill()
{
    const auto values = std::array { 11, 12, 13 };
    // Fail in a fresh segment, at growth, and in already acquired capacity.
    for (size_t initial_size = 0; initial_size < 3; ++initial_size)
    {
        auto pool = RawArrayPool<int, 1, ThreadSafe>(values.size());
        for (size_t i = 0; i < initial_size; ++i)
            ASSERT_EQ(pool.insert(values), i);
        const auto* first = initial_size == 0 ? nullptr : pool[0].data();
        auto fail = true;
        const auto row = values
                         | std::views::transform(
                             [&](int value)
                             {
                                 if (fail && value == 12)
                                     throw std::runtime_error("fill failed");
                                 return value;
                             });

        EXPECT_THROW(pool.insert(row), std::runtime_error);
        const auto retained = pool.memory_usage();
        EXPECT_THROW(pool.insert(row), std::runtime_error);
        EXPECT_EQ(pool.memory_usage(), retained);
        EXPECT_EQ(pool.size(), initial_size);
        for (size_t i = 0; i < initial_size; ++i)
            EXPECT_TRUE(std::ranges::equal(pool[i], values));

        fail = false;
        EXPECT_EQ(pool.insert(row), initial_size);
        EXPECT_EQ(pool.memory_usage(), retained);
        EXPECT_TRUE(std::ranges::equal(pool[initial_size], values));
        if (first)
            EXPECT_EQ(pool[0].data(), first);
    }
}

TEST(YggdrasilTests, CommonRawArrayPoolFailedRangeFillPreservesNextIndex)
{
    test_raw_array_pool_failed_fill<false>();
    test_raw_array_pool_failed_fill<true>();
}

enum class PoolRangeOperation
{
    None,
    Begin,
    End,
    Size,
    Dereference,
    Increment,
    Compare
};

struct ThrowingPoolRange
{
    std::span<const int> values;
    PoolRangeOperation failure;
    size_t skip;
    mutable size_t visits = 0;

    void check(PoolRangeOperation operation) const
    {
        if (failure == operation && visits++ == skip)
            throw std::runtime_error("range operation failed");
    }

    struct Iterator
    {
        using value_type = int;
        using difference_type = std::ptrdiff_t;
        using iterator_concept = std::forward_iterator_tag;
        const int* position = nullptr;
        const ThrowingPoolRange* range = nullptr;

        const int& operator*() const
        {
            range->check(PoolRangeOperation::Dereference);
            return *position;
        }
        Iterator& operator++()
        {
            range->check(PoolRangeOperation::Increment);
            ++position;
            return *this;
        }
        Iterator operator++(int)
        {
            auto old = *this;
            ++*this;
            return old;
        }
        friend bool operator==(const Iterator& lhs, const Iterator& rhs)
        {
            if (lhs.range)
                lhs.range->check(PoolRangeOperation::Compare);
            return lhs.position == rhs.position;
        }
    };

    Iterator begin() const
    {
        check(PoolRangeOperation::Begin);
        return { values.data(), this };
    }
    Iterator end() const
    {
        check(PoolRangeOperation::End);
        return { values.data() + values.size(), this };
    }
    size_t size() const
    {
        check(PoolRangeOperation::Size);
        return values.size();
    }
};

static_assert(SizedForwardRangeOf<ThrowingPoolRange, int>);

template<bool ThreadSafe>
void test_raw_array_pool_throwing_operations()
{
    const auto values = std::array { 11, 12, 13 };
    const auto failures = std::array { std::pair { PoolRangeOperation::Begin, size_t { 0 } },     std::pair { PoolRangeOperation::End, size_t { 0 } },
                                       std::pair { PoolRangeOperation::Size, size_t { 0 } },      std::pair { PoolRangeOperation::Dereference, size_t { 1 } },
                                       std::pair { PoolRangeOperation::Increment, size_t { 2 } }, std::pair { PoolRangeOperation::Compare, size_t { 3 } } };
    for (const auto [operation, skip] : failures)
    {
        auto pool = RawArrayPool<int, 1, ThreadSafe>(values.size());
        ASSERT_EQ(pool.insert(values), 0);
        const auto* first = pool[0].data();
        auto row = ThrowingPoolRange { values, operation, skip };
        EXPECT_THROW(pool.insert(row), std::runtime_error);
        EXPECT_EQ(pool.size(), 1);
        EXPECT_EQ(pool[0].data(), first);
        EXPECT_TRUE(std::ranges::equal(pool[0], values));
        row.failure = PoolRangeOperation::None;
        EXPECT_EQ(pool.insert(row), 1);
        EXPECT_TRUE(std::ranges::equal(pool[1], values));
    }
}

TEST(YggdrasilTests, CommonRawArrayPoolTraversalExceptionsPreservePublication)
{
    test_raw_array_pool_throwing_operations<false>();
    test_raw_array_pool_throwing_operations<true>();
}

struct ThrowingPoolDataRange
{
    std::span<const int> values;
    bool fail = true;
    const int* begin() const { return values.data(); }
    const int* end() const { return values.data() + values.size(); }
    size_t size() const { return values.size(); }
    const int* data() const
    {
        if (fail)
            throw std::runtime_error("range data failed");
        return values.data();
    }
};

template<bool ThreadSafe>
void test_raw_array_pool_throwing_data()
{
    const auto values = std::array { 11, 12, 13 };
    auto pool = RawArrayPool<int, 1, ThreadSafe>(values.size());
    auto row = ThrowingPoolDataRange { values };
    EXPECT_THROW(pool.insert(row), std::runtime_error);
    EXPECT_TRUE(pool.empty());
    EXPECT_EQ(pool.memory_usage(), 0);
    row.fail = false;
    EXPECT_EQ(pool.insert(row), 0);
    EXPECT_TRUE(std::ranges::equal(pool[0], values));
}

TEST(YggdrasilTests, CommonRawArrayPoolContiguousDataExceptionsPropagate)
{
    test_raw_array_pool_throwing_data<false>();
    test_raw_array_pool_throwing_data<true>();
}

TEST(YggdrasilTests, CommonRawArrayStorageFailedFillPreservesEarlierSegmentSpace)
{
    auto storage = detail::GeometricByteStorage<4, alignof(int), false>();
    auto* first = storage.allocate(3);
    std::byte* failed = nullptr;
    EXPECT_THROW(storage.allocate_with(3,
                                       [&](std::byte* destination)
                                       {
                                           failed = destination;
                                           throw std::runtime_error("fill failed");
                                       }),
                 std::runtime_error);
    const auto retained = storage.memory_usage();
    EXPECT_EQ(storage.allocate(1), first + 3);
    EXPECT_EQ(storage.allocate(3), failed);
    EXPECT_EQ(storage.memory_usage(), retained);
}

struct NoncopyablePoolValue
{
    int value;
    explicit NoncopyablePoolValue(int value_) : value(value_) {}
    NoncopyablePoolValue(const NoncopyablePoolValue&) = delete;
    NoncopyablePoolValue(NoncopyablePoolValue&&) = default;
};

static_assert(TriviallyCopyable<NoncopyablePoolValue>);
static_assert(SizedForwardRangeOf<std::span<const NoncopyablePoolValue>, NoncopyablePoolValue>);

TEST(YggdrasilTests, CommonRawArrayPoolBindsNoncopyableRangeReferences)
{
    const auto values = std::array { NoncopyablePoolValue(11), NoncopyablePoolValue(12) };
    const auto row = values | std::views::transform([](const NoncopyablePoolValue& value) -> const NoncopyablePoolValue& { return value; });
    auto pool = RawArrayPool<NoncopyablePoolValue>(values.size());
    EXPECT_EQ(pool.insert(row), 0);
    EXPECT_EQ(pool[0][0].value, 11);
    EXPECT_EQ(pool[0][1].value, 12);
}

}  // namespace ygg::tests
