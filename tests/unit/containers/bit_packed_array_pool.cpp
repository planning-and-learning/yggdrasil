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
#include <cstdint>
#include <gtest/gtest.h>
#include <limits>
#include <ranges>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>
#include <yggdrasil/containers/bit_packed_array_pool.hpp>
#include <yggdrasil/core/config.hpp>

namespace ygg::tests
{

TEST(YggdrasilTests, CommonBitPackedArrayViewReportsEmpty)
{
    auto storage = std::array<uint8_t, 1> {};
    auto view = ygg::BasicBitPackedArrayView<uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>>(storage.data(), 2, 3, 0);
    auto empty_view = ygg::BasicBitPackedArrayView<uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>>(storage.data(), 0, 3, 0);

    EXPECT_EQ(view.size(), 2);
    EXPECT_FALSE(view.empty());
    EXPECT_EQ(view.width(), 3);
    EXPECT_EQ(empty_view.size(), 0);
    EXPECT_TRUE(empty_view.empty());
}

TEST(YggdrasilTests, CommonBitPackedArrayViewAtChecksBounds)
{
    auto storage = std::array<uint8_t, 1> {};
    auto view = ygg::BasicBitPackedArrayView<uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>>(storage.data(), 2, 3, 0);
    const auto const_view = ygg::BasicBitPackedArrayView<const uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>>(storage.data(), 2, 3, 0);

    view.at(1) = 5;

    EXPECT_EQ(view.at(1), 5);
    EXPECT_EQ(const_view.at(1), 5);
    EXPECT_THROW(view.at(2), std::out_of_range);
    EXPECT_THROW(const_view.at(2), std::out_of_range);

    auto invalid_view = ygg::BasicBitPackedArrayView<uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>>(nullptr, 2, 3, 0);
    const auto invalid_const_view = ygg::BasicBitPackedArrayView<const uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>>(nullptr, 2, 3, 0);

    EXPECT_THROW(invalid_view.at(0), std::logic_error);
    EXPECT_THROW(invalid_const_view.at(0), std::logic_error);
    EXPECT_THROW((invalid_view = std::span<const uint8_t>(storage)), std::logic_error);
    EXPECT_EQ(invalid_view.end() - invalid_view.begin(), 2);
    EXPECT_EQ(invalid_const_view.end() - invalid_const_view.begin(), 2);
}

TEST(YggdrasilTests, CommonBitPackedArrayViewFrontBackCheckEmptyAndInvalidViews)
{
    auto storage = std::array<uint8_t, 1> {};
    auto view = ygg::BasicBitPackedArrayView<uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>>(storage.data(), 2, 3, 0);
    const auto const_view = ygg::BasicBitPackedArrayView<const uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>>(storage.data(), 2, 3, 0);

    view = std::span<const uint8_t>(std::array<uint8_t, 2> { 1, 5 });

    EXPECT_EQ(view.front(), 1);
    EXPECT_EQ(const_view.front(), 1);
    EXPECT_EQ(view.back(), 5);
    EXPECT_EQ(const_view.back(), 5);

    auto empty_view = ygg::BasicBitPackedArrayView<uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>>(storage.data(), 0, 3, 0);
    const auto empty_const_view = ygg::BasicBitPackedArrayView<const uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>>(storage.data(), 0, 3, 0);

    EXPECT_THROW(empty_view.front(), std::out_of_range);
    EXPECT_THROW(empty_const_view.front(), std::out_of_range);
    EXPECT_THROW(empty_view.back(), std::out_of_range);
    EXPECT_THROW(empty_const_view.back(), std::out_of_range);

    auto invalid_view = ygg::BasicBitPackedArrayView<uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>>(nullptr, 2, 3, 0);
    const auto invalid_const_view = ygg::BasicBitPackedArrayView<const uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>>(nullptr, 2, 3, 0);

    EXPECT_THROW(invalid_view.front(), std::logic_error);
    EXPECT_THROW(invalid_const_view.front(), std::logic_error);
    EXPECT_THROW(invalid_view.back(), std::logic_error);
    EXPECT_THROW(invalid_const_view.back(), std::logic_error);
}

TEST(YggdrasilTests, CommonBitPackedArrayRejectsInvalidWidths)
{
    auto storage = std::array<uint8_t, 1> {};

    EXPECT_THROW((ygg::BasicBitPackedArrayView<uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>>(storage.data(), 1, 0, 0)), std::invalid_argument);
    EXPECT_THROW((ygg::BasicBitPackedArrayView<uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>>(storage.data(), 1, 9, 0)), std::invalid_argument);
    EXPECT_THROW((ygg::BasicBitPackedArrayView<uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>>(storage.data(), 1, 1, 8)), std::invalid_argument);
    EXPECT_THROW((ygg::BitPackedArrayPool<uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>, 1>(1, 0)), std::invalid_argument);
    EXPECT_THROW((ygg::BitPackedArrayPool<uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>, 1>(1, 9)), std::invalid_argument);
}

TEST(YggdrasilTests, CommonBitPackedArrayViewHasBorrowedRandomAccessIterators)
{
    using Pool = ygg::BitPackedArrayPool<uint32_t, ygg::bit::ForwardingBlockCoder<uint32_t>, 1>;
    using View = typename Pool::ArrayView;
    using ConstView = typename Pool::ConstArrayView;

    static_assert(std::random_access_iterator<typename View::iterator>);
    static_assert(std::random_access_iterator<typename View::const_iterator>);
    static_assert(std::random_access_iterator<typename ConstView::const_iterator>);
    static_assert(std::ranges::random_access_range<View>);
    static_assert(std::ranges::random_access_range<const View>);
    static_assert(std::ranges::borrowed_range<View>);
    static_assert(std::ranges::borrowed_range<ConstView>);

    auto pool = Pool(5, 5);
    pool.push_back(std::array<uint32_t, 5> { 0, 0, 0, 0, 0 });
    pool.push_back(std::array<uint32_t, 5> { 1, 1, 1, 1, 1 });
    pool.push_back(std::array<uint32_t, 5> { 2, 3, 5, 7, 11 });

    auto begin = pool[2].begin();
    const auto end = pool[2].end();
    EXPECT_EQ(end - begin, 5);
    EXPECT_EQ(begin[2], 5);
    EXPECT_EQ(*(begin + 4), 11);
    EXPECT_EQ(*(3 + begin), 7);
    EXPECT_LT(begin, end);

    begin += 4;
    EXPECT_EQ(*begin, 11);
    begin -= 3;
    EXPECT_EQ(*begin, 3);
    EXPECT_EQ(*--begin, 2);

    auto const_begin = std::as_const(pool)[2].begin();
    const auto const_end = std::as_const(pool)[2].end();
    EXPECT_EQ(const_end - const_begin, 5);
    const_begin += 3;
    EXPECT_EQ(*const_begin, 7);
    EXPECT_EQ(*--const_begin, 5);
}

TEST(YggdrasilTests, CommonBitPackedArrayPoolSupportsEveryBlockWidth)
{
    const auto check = []<typename Block>()
    {
        constexpr size_t length = 11;
        constexpr size_t num_rows = 8;
        constexpr auto digits = std::numeric_limits<Block>::digits;

        for (size_t width = 1; width <= digits; ++width)
        {
            SCOPED_TRACE(static_cast<int>(digits));
            SCOPED_TRACE(static_cast<int>(width));

            const auto mask = width == digits ? std::numeric_limits<Block>::max() : static_cast<Block>((Block { 1 } << width) - 1);
            auto pool = ygg::BitPackedArrayPool<Block, ygg::bit::ForwardingBlockCoder<Block>, 1>(length, static_cast<uint8_t>(width));
            auto expected = std::vector<std::vector<Block>> {};
            expected.reserve(num_rows);

            for (size_t row = 0; row < num_rows; ++row)
            {
                auto values = std::vector<Block>(length);
                for (size_t column = 0; column < length; ++column)
                    values[column] = static_cast<Block>(row * length + column) & mask;
                values.front() = 0;
                values.back() = mask;

                EXPECT_EQ(pool.push_back(values), row);
                expected.push_back(std::move(values));
            }

            for (size_t row = 0; row < num_rows; ++row)
                EXPECT_EQ(pool[row], std::span<const Block>(expected[row]));

            expected[3][5] = static_cast<Block>(expected[3][5] + 1) & mask;
            pool[3] = std::span<const Block>(expected[3]);
            EXPECT_EQ(pool[2], std::span<const Block>(expected[2]));
            EXPECT_EQ(pool[3], std::span<const Block>(expected[3]));
            EXPECT_EQ(pool[4], std::span<const Block>(expected[4]));
        }
    };

    check.template operator()<uint8_t>();
    check.template operator()<uint16_t>();
    check.template operator()<uint32_t>();
    check.template operator()<uint64_t>();
}

TEST(YggdrasilTests, CommonBitPackedArrayRejectsUnrepresentableLengths)
{
    if constexpr (std::numeric_limits<size_t>::max() > static_cast<size_t>(std::numeric_limits<std::ptrdiff_t>::max()))
    {
        using View = ygg::BasicBitPackedArrayView<uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>>;
        using Pool = ygg::BitPackedArrayPool<uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>, 1>;
        const auto length = static_cast<size_t>(std::numeric_limits<std::ptrdiff_t>::max()) + 1;

        EXPECT_THROW((View(nullptr, length, 1, 0)), std::overflow_error);
        EXPECT_THROW((Pool(length, 1)), std::overflow_error);
    }
}

TEST(YggdrasilTests, CommonBitPackedArrayPoolOutOfRange)
{
    auto pool = ygg::BitPackedArrayPool<ygg::uint_t, ygg::bit::ForwardingBlockCoder<ygg::uint_t>, 1>(4, 7);
    const auto first = std::vector<ygg::uint_t> { 1, 2, 3, 4 };
    const auto second = std::vector<ygg::uint_t> { 5, 6, 7, 8 };

    EXPECT_EQ(pool.push_back(first), 0);
    EXPECT_EQ(pool.push_back(second), 1);

    EXPECT_THROW(pool.push_back(std::vector<ygg::uint_t>({ 9, 10, 11, 128 })), std::out_of_range);
    EXPECT_EQ(pool.size(), 2);
    EXPECT_EQ(pool[0], first);
    EXPECT_EQ(pool[1], second);

    const auto replacement = std::vector<ygg::uint_t> { 12, 13, 14, 15 };
    EXPECT_EQ(pool.push_back(replacement), 2);
    EXPECT_EQ(pool[2], replacement);

    EXPECT_THROW(pool.push_back(std::vector<ygg::uint_t>({ 1, 1, 1 })), std::invalid_argument);
}

TEST(YggdrasilTests, CommonBitPackedArrayPoolStoresZeroLengthArrays)
{
    auto pool = ygg::BitPackedArrayPool<uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>, 1>(0, 1);
    const auto empty = std::vector<uint8_t> {};

    EXPECT_EQ(pool.push_back(empty), 0);
    EXPECT_EQ(pool.push_back(empty), 1);
    EXPECT_EQ(pool.size(), 2);
    EXPECT_EQ(pool.length(), 0);
    EXPECT_EQ(pool.width(), 1);
    EXPECT_TRUE(pool[0].empty());
    EXPECT_TRUE(pool[1].empty());
}

TEST(YggdrasilTests, CommonBitPackedArrayPoolAtChecksBounds)
{
    auto pool = ygg::BitPackedArrayPool<uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>, 1>(2, 3);
    pool.push_back(std::vector<uint8_t>({ 1, 2 }));
    const auto& const_pool = pool;

    pool.at(0).at(1) = 6;

    EXPECT_EQ(pool.at(0), (std::vector<uint8_t> { 1, 6 }));
    EXPECT_EQ(const_pool.at(0), (std::vector<uint8_t> { 1, 6 }));
    EXPECT_THROW(pool.at(1), std::out_of_range);
    EXPECT_THROW(const_pool.at(1), std::out_of_range);
}

TEST(YggdrasilTests, CommonBitPackedArrayPool)
{
    auto pool = ygg::BitPackedArrayPool<uint8_t, ygg::bit::ForwardingBlockCoder<uint8_t>, 1>(2, 3);

    // Repeat 5 times on cleared pool.
    for (size_t i = 0; i < 5; ++i)
    {
        pool.clear();

        // Write
        for (uint8_t a = 0; a < 4; ++a)
        {
            for (uint8_t b = 0; b < 4; ++b)
            {
                const size_t idx = a * 4 + b;
                EXPECT_EQ(pool.push_back(std::vector<uint8_t>({ a, b })), idx);
            }
        }

        // 16 arrays stored.
        EXPECT_EQ(pool.size(), 16);
        EXPECT_EQ(pool.segments().size(), 5);
        EXPECT_EQ(pool.segments()[0].size(),
                  1);  /// capacity 1 array requires 6 bits   = 1  uint8_t
        EXPECT_EQ(pool.segments()[1].size(),
                  2);  /// capacity 2 arrays require 12 bits  = 2  uint8_t
        EXPECT_EQ(pool.segments()[2].size(),
                  3);  /// capacity 4 arrays require 24 bits  = 3  uint8_t
        EXPECT_EQ(pool.segments()[3].size(),
                  6);  /// capacity 8 arrays require 48 bits  = 6  uint8_t
        EXPECT_EQ(pool.segments()[4].size(),
                  12);  /// capacity 16 arrays require 96 bits = 12 uint8_t
        // 2^{5+1} - 1 = 31
        EXPECT_EQ(pool.capacity(), 31);

        // Read
        for (uint8_t a = 0; a < 4; ++a)
        {
            for (uint8_t b = 0; b < 4; ++b)
            {
                const size_t idx = a * 4 + b;
                EXPECT_EQ(pool[idx], (std::vector<uint8_t> { a, b }));
            }
        }

        // Re-write
        for (uint8_t a = 0; a < 4; ++a)
        {
            for (uint8_t b = 0; b < 4; ++b)
            {
                const size_t idx = (pool.size() - 1) - (a * 4 + b);
                pool[idx] = std::vector<uint8_t>({ a, b });
            }
        }

        // Re-read
        for (uint8_t a = 0; a < 4; ++a)
        {
            for (uint8_t b = 0; b < 4; ++b)
            {
                const size_t idx = (pool.size() - 1) - (a * 4 + b);
                EXPECT_EQ(pool[idx], (std::vector<uint8_t> { a, b }));
            }
        }
    }
}

}  // namespace ygg::tests
