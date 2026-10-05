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

#include "yggdrasil/containers/raw_array_set.hpp"

#include "yggdrasil/containers/bit_packed_array_pool.hpp"
#include "yggdrasil/containers/block_array_pool.hpp"

#include <algorithm>
#include <array>
#include <concepts>
#include <gtest/gtest.h>
#include <limits>
#include <ranges>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace ygg::tests
{

using DefaultRawArraySet = RawArraySet<int, 2>;
using ConcurrentRawArraySet = RawArraySet<int, 2, true>;

static_assert(!DefaultRawArraySet::thread_safe);
static_assert(ConcurrentRawArraySet::thread_safe);
static_assert(std::same_as<decltype(std::declval<DefaultRawArraySet&>()[0]), std::span<const int>>);
static_assert(std::same_as<decltype(std::declval<ConcurrentRawArraySet&>()[0]), std::span<const int>>);
static_assert(!std::is_copy_constructible_v<DefaultRawArraySet>);
static_assert(std::is_move_constructible_v<ConcurrentRawArraySet>);
static_assert(std::is_move_assignable_v<ConcurrentRawArraySet>);

struct RawArraySetCountingElement
{
    int value;
    static inline size_t hash_calls = 0;

    friend bool operator==(const RawArraySetCountingElement&, const RawArraySetCountingElement&) = default;
};

}  // namespace ygg::tests

namespace ygg
{

template<>
struct Hash<tests::RawArraySetCountingElement>
{
    hash_t operator()(const tests::RawArraySetCountingElement& value) const noexcept
    {
        ++tests::RawArraySetCountingElement::hash_calls;
        return static_cast<hash_t>(value.value);
    }
};

}  // namespace ygg

namespace ygg::tests
{

TEST(YggdrasilTests, CommonRawArraySetRejectsWrongLengthInputs)
{
    auto set = ygg::RawArraySet<int, 2>(3);
    const auto too_short = std::array<int, 2> { 1, 2 };
    const auto too_long = std::array<int, 4> { 1, 2, 3, 4 };

    EXPECT_THROW(set.insert(too_short), std::invalid_argument);
    EXPECT_THROW(set.find(too_short), std::invalid_argument);
    EXPECT_THROW(set.contains(too_long), std::invalid_argument);
}

TEST(YggdrasilTests, CommonRawArraySetStoresFixedLengthArrays)
{
    auto set = ygg::RawArraySet<int, 2>(3);

    const auto first = std::vector<int> { 1, 2, 3 };
    const auto second = std::vector<int> { 1, 2, 4 };
    const auto third = std::array<int, 3> { 1, 2, 5 };

    EXPECT_TRUE(set.empty());

    EXPECT_EQ(set.insert(first), 0);
    EXPECT_EQ(set.insert(second), 1);
    EXPECT_EQ(set.insert(third), 2);
    EXPECT_EQ(set.insert(first), 0);

    EXPECT_EQ(set.size(), 3);
    EXPECT_FALSE(set.empty());
    EXPECT_TRUE(set.contains(first));
    EXPECT_TRUE(set.contains(second));
    EXPECT_TRUE(set.contains(third));
    EXPECT_FALSE(set.contains(std::array<int, 3> { 9, 9, 9 }));
    EXPECT_EQ(set.find(first), 0);
    EXPECT_EQ(set.find(second), 1);
    EXPECT_EQ(set.find(third), 2);
    EXPECT_EQ(set.find(std::array<int, 3> { 9, 9, 9 }), std::nullopt);

    EXPECT_TRUE(std::ranges::equal(set[0], first));
    EXPECT_TRUE(std::ranges::equal(set.front(), first));
    EXPECT_TRUE(std::ranges::equal(set.back(), third));
    EXPECT_TRUE(std::ranges::equal(set[1], second));
    EXPECT_TRUE(std::ranges::equal(set.at(1), second));
    EXPECT_TRUE(std::ranges::equal(set[2], third));
    EXPECT_THROW(set.at(3), std::out_of_range);
}

TEST(YggdrasilTests, CommonRawArraySetEmptyAccessThrows)
{
    auto set = ygg::RawArraySet<int, 2>(3);
    const auto& const_set = set;

    EXPECT_THROW(set.front(), std::out_of_range);
    EXPECT_THROW(const_set.front(), std::out_of_range);
    EXPECT_THROW(set.back(), std::out_of_range);
    EXPECT_THROW(const_set.back(), std::out_of_range);
}

TEST(YggdrasilTests, CommonRawArraySetStoresSingleZeroLengthArray)
{
    auto set = ygg::RawArraySet<int, 2>(0);
    const auto value = std::array<int, 0> {};

    EXPECT_TRUE(set.empty());
    EXPECT_EQ(set.array_size(), 0);

    EXPECT_EQ(set.insert(value), 0);
    EXPECT_EQ(set.insert(value), 0);

    EXPECT_EQ(set.size(), 1);
    EXPECT_FALSE(set.empty());
    EXPECT_TRUE(set.contains(value));
    EXPECT_EQ(set.find(value), 0);
    EXPECT_TRUE(set[0].empty());
    EXPECT_EQ(set[0].data(), nullptr);
    EXPECT_TRUE(set.front().empty());
    EXPECT_TRUE(set.back().empty());
}

TEST(YggdrasilTests, CommonRawArraySetClearKeepsContainerReusable)
{
    auto set = ygg::RawArraySet<int, 1>(2);

    EXPECT_EQ(set.insert(std::vector<int>({ 1, 2 })), 0);
    set.clear();
    EXPECT_TRUE(set.empty());

    const auto value = std::vector<int> { 3, 4 };
    EXPECT_EQ(set.insert(value), 0);
    EXPECT_EQ(set.size(), 1);
    EXPECT_TRUE(set.contains(value));
    EXPECT_EQ(set.find(value), 0);
}

TEST(YggdrasilTests, CommonRawArraySetInsertHashesEachElementOnce)
{
    auto set = ygg::RawArraySet<RawArraySetCountingElement, 2>(2);
    const auto value = std::array<RawArraySetCountingElement, 2> { RawArraySetCountingElement { 1 }, RawArraySetCountingElement { 2 } };

    RawArraySetCountingElement::hash_calls = 0;
    EXPECT_EQ(set.insert(value), 0);
    EXPECT_EQ(RawArraySetCountingElement::hash_calls, value.size());

    RawArraySetCountingElement::hash_calls = 0;
    EXPECT_EQ(set.insert(value), 0);
    EXPECT_EQ(RawArraySetCountingElement::hash_calls, value.size());
}

TEST(YggdrasilTests, CommonRawArraySetMoveKeepsHashFunctorsBoundToStorage)
{
    const auto value = std::array<int, 2> { 1, 2 };
    auto source = ygg::RawArraySet<int, 2>(2);
    EXPECT_EQ(source.insert(value), 0);

    auto moved = std::move(source);
    EXPECT_TRUE(moved.contains(value));

    auto assigned = ygg::RawArraySet<int, 2>(2);
    assigned = std::move(moved);
    EXPECT_EQ(assigned.find(value), 0);
    EXPECT_TRUE(std::ranges::equal(assigned[0], value));
}

template<bool ThreadSafe>
void check_decoded_raw_array_ranges()
{
    using BlockView = BasicBlockArrayView<const uint_t, bit::ForwardingBlockCoder<uint_t>>;
    using PackedView = BasicBitPackedArrayView<uint_t, bit::ForwardingBlockCoder<uint_t>>;
    static_assert(SizedForwardRangeOf<BlockView, uint_t>);
    static_assert(SizedForwardRangeOf<PackedView, uint_t>);
    const auto values = std::array<uint_t, 3> { 2, 5, 7 };
    const auto blocks = BlockView(values.data(), values.size());
    auto packed_storage = std::array<uint_t, 1> {};
    auto packed = PackedView(packed_storage.data(), values.size(), 3, 1);
    packed = std::span<const uint_t>(values);
    auto set = RawArraySet<uint_t, 1, ThreadSafe>(values.size());
    EXPECT_EQ(set.insert(blocks), 0);
    const auto memory = set.memory_usage();
    EXPECT_EQ(set.insert(packed), 0);
    EXPECT_EQ(set.insert(values), 0);
    EXPECT_EQ(set.size(), 1);
    EXPECT_EQ(set.memory_usage(), memory);
    EXPECT_TRUE(set.contains(packed));
    EXPECT_EQ(set.find(blocks), 0);
    EXPECT_TRUE(std::ranges::equal(set[0], values));
    EXPECT_THROW(set.insert(BlockView(values.data(), 2)), std::invalid_argument);
    EXPECT_THROW(set.find(BlockView(values.data(), 2)), std::invalid_argument);
    EXPECT_EQ(set.size(), 1);
    set.clear();
    EXPECT_EQ(set.insert(packed), 0);
    EXPECT_EQ(set.memory_usage(), memory);

    auto empty = RawArraySet<uint_t, 1, ThreadSafe>(0);
    const auto empty_row = BlockView(nullptr, 0);
    EXPECT_EQ(empty.insert(empty_row), 0);
    EXPECT_EQ(empty.insert(std::span<const uint_t>()), 0);
    EXPECT_TRUE(empty.contains(empty_row));
    EXPECT_EQ(empty.size(), 1);
}

TEST(YggdrasilTests, CommonRawArraySetAcceptsDecodedRanges)
{
    check_decoded_raw_array_ranges<false>();
    check_decoded_raw_array_ranges<true>();
}

template<bool ThreadSafe>
void test_raw_array_set_failed_range()
{
    const auto values = std::array { 11, 12, 13 };
    // The first traversal hashes, the second fills the unpublished row.
    for (const auto failure : { size_t { 0 }, size_t { 1 }, size_t { 3 }, size_t { 4 } })
    {
        auto set = RawArraySet<int, 1, ThreadSafe>(values.size());
        auto reads = size_t { 0 };
        auto throw_at = failure;
        const auto row = values
                         | std::views::transform(
                             [&](int value)
                             {
                                 if (reads++ == throw_at)
                                     throw std::runtime_error("range traversal failed");
                                 return value;
                             });
        EXPECT_THROW(set.insert(row), std::runtime_error);
        EXPECT_TRUE(set.empty());
        const auto retained = set.memory_usage();
        reads = 0;
        EXPECT_THROW(set.insert(row), std::runtime_error);
        EXPECT_EQ(set.memory_usage(), retained);

        throw_at = std::numeric_limits<size_t>::max();
        EXPECT_EQ(set.insert(row), 0);
        const auto* first = set[0].data();
        // A duplicate's second traversal compares against its stored row.
        throw_at = values.size() + 1;
        reads = 0;
        EXPECT_THROW(set.insert(row), std::runtime_error);
        reads = 0;
        EXPECT_THROW(set.contains(row), std::runtime_error);
        throw_at = 0;
        reads = 0;
        EXPECT_THROW(set.find(row), std::runtime_error);
        EXPECT_EQ(set.size(), 1);
        EXPECT_EQ(set[0].data(), first);
        EXPECT_TRUE(std::ranges::equal(set[0], values));

        throw_at = std::numeric_limits<size_t>::max();
        EXPECT_EQ(set.find(row), 0);
        EXPECT_EQ(set.insert(row), 0);
        EXPECT_EQ(set.insert(std::array { 21, 22, 23 }), 1);
        EXPECT_EQ(set.find(values), 0);
    }
}

TEST(YggdrasilTests, CommonRawArraySetRangeExceptionsLeaveNoPublishedPlaceholder)
{
    test_raw_array_set_failed_range<false>();
    test_raw_array_set_failed_range<true>();
}

template<bool ThreadSafe>
void test_raw_array_set_populated_fill_failure()
{
    const auto first = std::array { 11, 12, 13 };
    const auto second = std::array { 21, 22, 23 };
    auto set = RawArraySet<int, 1, ThreadSafe>(first.size());
    ASSERT_EQ(set.insert(first), 0);
    const auto* first_storage = set[0].data();
    auto second_value_reads = size_t { 0 };
    auto fail = true;
    const auto row = second
                     | std::views::transform(
                         [&](int value)
                         {
                             // Collision probes reject at 21 != 11. Reading 22 twice reaches the fill.
                             if (value == 22 && second_value_reads++ == 1 && fail)
                                 throw std::runtime_error("fill failed");
                             return value;
                         });
    EXPECT_THROW(set.insert(row), std::runtime_error);
    const auto retained = set.memory_usage();
    second_value_reads = 0;
    EXPECT_THROW(set.insert(row), std::runtime_error);
    EXPECT_EQ(set.memory_usage(), retained);
    EXPECT_EQ(set.size(), 1);
    EXPECT_EQ(set.find(first), 0);
    EXPECT_FALSE(set.contains(second));
    EXPECT_EQ(set[0].data(), first_storage);
    fail = false;
    EXPECT_EQ(set.insert(row), 1);
    EXPECT_EQ(set.insert(first), 0);
    EXPECT_EQ(set.find(second), 1);
}

TEST(YggdrasilTests, CommonRawArraySetFailedFillPreservesExistingIndexZero)
{
    test_raw_array_set_populated_fill_failure<false>();
    test_raw_array_set_populated_fill_failure<true>();
}

template<bool ThreadSafe>
void test_raw_array_set_proxy_range()
{
    auto values = std::vector<bool> { true, false, true };
    const auto row = std::ranges::subrange(values.begin(), values.end());
    static_assert(SizedForwardRangeOf<decltype(row), bool>);
    auto set = RawArraySet<bool, 1, ThreadSafe>(values.size());
    EXPECT_EQ(set.insert(row), 0);
    EXPECT_EQ(set.insert(std::array { true, false, true }), 0);
    EXPECT_TRUE(set.contains(row));
    EXPECT_EQ(set.find(row), 0);
    EXPECT_EQ(set.size(), 1);
}

TEST(YggdrasilTests, CommonRawArraySetConvertsProxyRangeValuesConsistently)
{
    test_raw_array_set_proxy_range<false>();
    test_raw_array_set_proxy_range<true>();
}

}  // namespace ygg::tests
