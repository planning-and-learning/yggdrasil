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
#include <concepts>
#include <gtest/gtest.h>
#include <limits>
#include <ranges>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>
#include <yggdrasil/containers/block_array_set.hpp>
#include <yggdrasil/core/config.hpp>

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

TEST(YggdrasilTests, CommonBlockArrayViewHasBorrowedRandomAccessIterators)
{
    using Pool = ygg::BlockArrayPool<uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>, 1>;
    using View = typename Pool::ArrayView;
    using ConstView = typename Pool::ConstArrayView;

    static_assert(std::random_access_iterator<typename View::iterator>);
    static_assert(std::random_access_iterator<typename View::const_iterator>);
    static_assert(std::random_access_iterator<typename ConstView::const_iterator>);
    static_assert(std::convertible_to<View, ConstView>);
    static_assert(std::ranges::random_access_range<View>);
    static_assert(std::ranges::random_access_range<const View>);
    static_assert(std::ranges::borrowed_range<View>);
    static_assert(std::ranges::borrowed_range<ConstView>);
    static_assert(std::random_access_iterator<typename Pool::iterator>);
    static_assert(std::random_access_iterator<typename Pool::const_iterator>);
    static_assert(std::ranges::random_access_range<Pool>);
    static_assert(std::ranges::random_access_range<const Pool>);

    auto pool = Pool(5);
    pool.push_back(std::array<uint8_t, 5> { 1, 2, 3, 4, 5 });

    auto begin = pool[0].begin();
    const auto end = pool[0].end();
    EXPECT_EQ(end - begin, 5);
    EXPECT_EQ(begin[2], 3);
    EXPECT_EQ(*(begin + 4), 5);
    EXPECT_EQ(*(3 + begin), 4);
    EXPECT_LT(begin, end);

    begin += 4;
    EXPECT_EQ(*begin, 5);
    begin -= 3;
    EXPECT_EQ(*begin, 2);
    EXPECT_EQ(*--begin, 1);

    auto const_begin = std::as_const(pool)[0].begin();
    const auto const_end = std::as_const(pool)[0].end();
    EXPECT_EQ(const_end - const_begin, 5);
    const_begin += 3;
    EXPECT_EQ(*const_begin, 4);
    EXPECT_EQ(*--const_begin, 3);

    const auto converted = ConstView(pool[0]);
    EXPECT_EQ(converted.back(), 5);
    EXPECT_EQ(pool.end() - pool.begin(), 1);
    EXPECT_EQ(pool.begin()[0][2], 3);
    EXPECT_GT(pool.memory_usage(), 0);
}

TEST(YggdrasilTests, CommonBlockArrayRejectsUnrepresentableLengths)
{
    if constexpr (std::numeric_limits<size_t>::max() > static_cast<size_t>(std::numeric_limits<std::ptrdiff_t>::max()))
    {
        using View = ygg::BasicBlockArrayView<uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>>;
        using Pool = ygg::BlockArrayPool<uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>, 1>;
        const auto length = static_cast<size_t>(std::numeric_limits<std::ptrdiff_t>::max()) + 1;

        EXPECT_THROW((View(nullptr, length)), std::overflow_error);
        EXPECT_THROW((Pool(length)), std::overflow_error);
    }
}

TEST(YggdrasilTests, CommonBlockArrayViewAtChecksBounds)
{
    auto storage = std::array<uint8_t, 2> { 1, 2 };
    auto view = ygg::BasicBlockArrayView<uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>>(storage.data(), storage.size());
    const auto const_view = ygg::BasicBlockArrayView<const uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>>(storage.data(), storage.size());

    view.at(1) = 7;

    EXPECT_EQ(view.at(1), 7);
    EXPECT_EQ(const_view.at(1), 7);
    EXPECT_THROW(view.at(2), std::out_of_range);
    EXPECT_THROW(const_view.at(2), std::out_of_range);

    auto invalid_view = ygg::BasicBlockArrayView<uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>>(nullptr, 2);
    const auto invalid_const_view = ygg::BasicBlockArrayView<const uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>>(nullptr, 2);

    EXPECT_THROW(invalid_view.at(0), std::logic_error);
    EXPECT_THROW(invalid_const_view.at(0), std::logic_error);
    EXPECT_THROW((invalid_view = std::span<const uint8_t>(storage)), std::logic_error);
    EXPECT_EQ(invalid_view.end() - invalid_view.begin(), 2);
    EXPECT_EQ(invalid_const_view.end() - invalid_const_view.begin(), 2);
}

TEST(YggdrasilTests, CommonBlockArrayViewFrontBackCheckEmptyAndInvalidViews)
{
    auto storage = std::array<uint8_t, 2> { 1, 2 };
    auto view = ygg::BasicBlockArrayView<uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>>(storage.data(), storage.size());
    const auto const_view = ygg::BasicBlockArrayView<const uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>>(storage.data(), storage.size());

    EXPECT_EQ(view.front(), 1);
    EXPECT_EQ(const_view.front(), 1);
    EXPECT_EQ(view.back(), 2);
    EXPECT_EQ(const_view.back(), 2);

    auto empty_view = ygg::BasicBlockArrayView<uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>>(storage.data(), 0);
    const auto empty_const_view = ygg::BasicBlockArrayView<const uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>>(storage.data(), 0);

    EXPECT_THROW(empty_view.front(), std::out_of_range);
    EXPECT_THROW(empty_const_view.front(), std::out_of_range);
    EXPECT_THROW(empty_view.back(), std::out_of_range);
    EXPECT_THROW(empty_const_view.back(), std::out_of_range);

    auto invalid_view = ygg::BasicBlockArrayView<uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>>(nullptr, 2);
    const auto invalid_const_view = ygg::BasicBlockArrayView<const uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>>(nullptr, 2);

    EXPECT_THROW(invalid_view.front(), std::logic_error);
    EXPECT_THROW(invalid_const_view.front(), std::logic_error);
    EXPECT_THROW(invalid_view.back(), std::logic_error);
    EXPECT_THROW(invalid_const_view.back(), std::logic_error);
}

TEST(YggdrasilTests, CommonBlockArrayPoolReturnsInsertedIndex)
{
    auto pool = ygg::BlockArrayPool<uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>, 1>(2);

    EXPECT_EQ(pool.push_back(std::vector<uint8_t>({ 1, 2 })), 0);
    EXPECT_EQ(pool.push_back(std::vector<uint8_t>({ 3, 4 })), 1);
    EXPECT_EQ(pool.size(), 2);
}

TEST(YggdrasilTests, CommonConcurrentBlockArrayPoolChecksIndexBoundBeforePublishing)
{
    using Pool = ygg::BlockArrayPool<uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>, 1, true>;
    auto pool = Pool(2);
    const auto first = std::array<uint8_t, 2> { 1, 2 };
    const auto second = std::array<uint8_t, 2> { 3, 4 };

    EXPECT_EQ(pool.push_back_bounded(first, 0), 0);
    EXPECT_THROW(pool.push_back_bounded(second, 0), std::length_error);
    EXPECT_EQ(pool.size(), 1);
    EXPECT_EQ(pool[0], std::span<const uint8_t>(first));
}

TEST(YggdrasilTests, CommonBlockArrayPoolAtChecksBounds)
{
    auto pool = ygg::BlockArrayPool<uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>, 1>(2);
    pool.push_back(std::vector<uint8_t>({ 1, 2 }));
    const auto& const_pool = pool;

    pool.at(0).at(1) = 5;

    EXPECT_EQ(pool.at(0), (std::vector<uint8_t> { 1, 5 }));
    EXPECT_EQ(const_pool.at(0), (std::vector<uint8_t> { 1, 5 }));
    EXPECT_THROW(pool.at(1), std::out_of_range);
    EXPECT_THROW(const_pool.at(1), std::out_of_range);
}

TEST(YggdrasilTests, CommonBlockArrayPoolStoresZeroLengthArrays)
{
    auto pool = ygg::BlockArrayPool<uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>, 1>(0);
    const auto empty = std::vector<uint8_t> {};

    EXPECT_EQ(pool.push_back(empty), 0);
    EXPECT_EQ(pool.push_back(empty), 1);
    EXPECT_EQ(pool.size(), 2);
    EXPECT_EQ(pool.length(), 0);
    EXPECT_TRUE(pool[0].empty());
    EXPECT_TRUE(pool[1].empty());
}

TEST(YggdrasilTests, CommonBlockArraySetOutOfRange)
{
    auto set = BlockArraySet<ygg::uint_t, bit::ForwardingBlockCoder<ygg::uint_t>, 1>(2);

    // 3 elements are too much for a pool that stores arrays of length 2.
    EXPECT_THROW(set.insert(std::vector<ygg::uint_t>({ 1, 1, 1 })), std::invalid_argument);
    EXPECT_THROW(set.find(std::vector<ygg::uint_t>({ 1, 1, 1 })), std::invalid_argument);
    EXPECT_THROW(set.contains(std::vector<ygg::uint_t>({ 1 })), std::invalid_argument);
}

TEST(YggdrasilTests, CommonBlockArraySetEmptyAccessThrows)
{
    auto set = BlockArraySet<uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>, 1>(2);

    EXPECT_THROW(set.front(), std::out_of_range);
    EXPECT_THROW(set.back(), std::out_of_range);
}

TEST(YggdrasilTests, CommonBlockArraySetDeduplicatesZeroLengthArrays)
{
    auto set = BlockArraySet<uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>, 1>(0);
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
    EXPECT_TRUE(set[first_index].empty());
}

TEST(YggdrasilTests, CommonBlockArraySetSupportsHashAwareInsertion)
{
    using Set = ygg::BlockArraySet<uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>, 1>;
    auto set = Set(2);
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

    EXPECT_THROW(set.insert_new_with_hash(first_hash, first), std::logic_error);
    EXPECT_EQ(set.size(), 1);
    EXPECT_EQ(set.find_with_hash(first, first_hash), first_index);

    EXPECT_EQ(set.insert_new_with_hash(second_hash, second), 1);
    EXPECT_EQ(set.find_with_hash(second, second_hash), 1);
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
                EXPECT_EQ(set.at(i1), std::span<const uint8_t>(value));
            }
        }

        EXPECT_THROW(set.at(set.size()), std::out_of_range);

        const auto first = std::array<uint8_t, 2> { 0, 0 };
        const auto last = std::array<uint8_t, 2> { 3, 3 };
        EXPECT_EQ(set.front(), std::span<const uint8_t>(first));
        EXPECT_EQ(set.back(), std::span<const uint8_t>(last));
        EXPECT_EQ(set.size(), 16);
        EXPECT_EQ(set.segments().size(), 5);
        EXPECT_EQ(set.segments()[0].size(),
                  2);  /// capacity 1 array stores 2 uint8_t blocks.
        EXPECT_EQ(set.segments()[1].size(),
                  4);  /// capacity 2 arrays store 4 uint8_t blocks.
        EXPECT_EQ(set.segments()[2].size(),
                  8);  /// capacity 4 arrays store 8 uint8_t blocks.
        EXPECT_EQ(set.segments()[3].size(),
                  16);  /// capacity 8 arrays store 16 uint8_t blocks.
        EXPECT_EQ(set.segments()[4].size(),
                  32);  /// capacity 16 arrays store 32 uint8_t blocks.
        EXPECT_EQ(set.capacity(), 31);
        EXPECT_GT(set.memory_usage(), 0);
    }
}

}  // namespace ygg::tests
