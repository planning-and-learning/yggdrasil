/*
 * Copyright (C) 2026 Dominik Drexler
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
#include <atomic>
#include <cstdint>
#include <functional>
#include <gtest/gtest.h>
#include <thread>
#include <utility>
#include <vector>
#include <yggdrasil/containers/bit_packed_array_pool.hpp>
#include <yggdrasil/containers/block_array_pool.hpp>
#include <yggdrasil/containers/raw_array_pool.hpp>
#include <yggdrasil/containers/raw_array_set.hpp>
#include <yggdrasil/containers/raw_vector_pool.hpp>
#include <yggdrasil/containers/raw_vector_set.hpp>
#include <yggdrasil/containers/segmented_vector.hpp>

#ifndef NDEBUG
#error "Concurrent publication tests must be compiled with NDEBUG."
#endif

namespace ygg::tests
{
namespace
{
template<typename Publish, typename Size, typename Read>
auto publish_and_read(Publish&& publish, Size&& size, Read&& read)
{
    using Value = std::invoke_result_t<Read>;

    auto result = Value {};

    auto writer = std::jthread([&] { std::invoke(publish); });
    auto reader = std::jthread(
        [&]
        {
            while (std::invoke(size) == 0)
                std::this_thread::yield();
            result = std::invoke(read);
        });

    writer.join();
    reader.join();
    return result;
}

template<typename Pool>
void expect_concurrent_pool_appends(Pool& pool)
{
    constexpr auto thread_count = size_t { 8 };
    constexpr auto values_per_thread = size_t { 32 };
    constexpr auto value_count = thread_count * values_per_thread;

    auto indices = std::vector<uint_t>(value_count);
    auto threads = std::vector<std::jthread> {};
    for (size_t thread = 0; thread < thread_count; ++thread)
    {
        threads.emplace_back(
            [&, thread]
            {
                for (size_t offset = 0; offset < values_per_thread; ++offset)
                {
                    const auto value = thread * values_per_thread + offset;
                    indices[value] = pool.insert(std::array<int, 2> { static_cast<int>(value), static_cast<int>(value + 1) });
                }
            });
    }
    threads.clear();

    EXPECT_EQ(pool.size(), value_count);
    auto sorted_indices = indices;
    std::ranges::sort(sorted_indices);
    for (size_t value = 0; value < value_count; ++value)
    {
        EXPECT_EQ(sorted_indices[value], value);
        EXPECT_TRUE(std::ranges::equal(pool[indices[value]], std::array<int, 2> { static_cast<int>(value), static_cast<int>(value + 1) }));
    }
}

template<typename Set>
void expect_concurrent_set_inserts(Set& set)
{
    constexpr auto thread_count = size_t { 8 };
    constexpr auto value_count = size_t { 64 };

    auto indices = std::vector(thread_count, std::vector<uint_t>(value_count));
    auto lookup_ok = std::atomic_bool { true };
    auto threads = std::vector<std::jthread> {};
    for (size_t thread = 0; thread < thread_count; ++thread)
    {
        threads.emplace_back(
            [&, thread]
            {
                for (size_t value = 0; value < value_count; ++value)
                {
                    const auto key = std::array<int, 2> { static_cast<int>(value), static_cast<int>(value + 1) };
                    const auto index = set.insert(key);
                    indices[thread][value] = index;
                    if (!set.contains(key) || set.find(key) != index)
                        lookup_ok.store(false, std::memory_order_relaxed);
                }
            });
    }
    threads.clear();

    EXPECT_TRUE(lookup_ok.load(std::memory_order_relaxed));
    EXPECT_EQ(set.size(), value_count);
    for (size_t value = 0; value < value_count; ++value)
    {
        const auto expected = std::array<int, 2> { static_cast<int>(value), static_cast<int>(value + 1) };
        for (size_t thread = 1; thread < thread_count; ++thread)
            EXPECT_EQ(indices[thread][value], indices[0][value]);
        EXPECT_EQ(set.find(expected), indices[0][value]);
        EXPECT_TRUE(std::ranges::equal(set[indices[0][value]], expected));
    }
}
}  // namespace

TEST(CommonConcurrentPublicationTest, SegmentedVectorPublishesForMutableAndConstAccess)
{
    auto mutable_vector = SegmentedVector<uint32_t, 1, true> {};
    EXPECT_EQ(publish_and_read([&] { mutable_vector.push_back(17); }, [&] { return mutable_vector.size(); }, [&] { return mutable_vector[0]; }), 17);

    auto const_vector = SegmentedVector<uint32_t, 1, true> {};
    EXPECT_EQ(publish_and_read([&] { const_vector.push_back(23); }, [&] { return const_vector.size(); }, [&] { return std::as_const(const_vector)[0]; }), 23);
}

TEST(CommonConcurrentPublicationTest, BlockArrayPoolPublishesForMutableAndConstAccess)
{
    using Pool = BlockArrayPool<uint32_t, bit::ForwardingBlockCoder<uint32_t>, 1, true>;
    constexpr auto values = std::array<uint32_t, 2> { 17, 23 };

    auto mutable_pool = Pool(2);
    EXPECT_EQ(publish_and_read([&] { mutable_pool.push_back(values); },
                               [&] { return mutable_pool.size(); },
                               [&] { return static_cast<uint32_t>(mutable_pool[0][1]); }),
              23);

    auto const_pool = Pool(2);
    EXPECT_EQ(publish_and_read([&] { const_pool.push_back(values); }, [&] { return const_pool.size(); }, [&] { return std::as_const(const_pool)[0][1]; }), 23);
}

TEST(CommonConcurrentPublicationTest, BitPackedArrayPoolPublishesForMutableAndConstAccess)
{
    using Pool = BitPackedArrayPool<uint32_t, bit::ForwardingBlockCoder<uint32_t>, 1, true>;
    constexpr auto values = std::array<uint32_t, 2> { 17, 23 };

    auto mutable_pool = Pool(2, 5);
    EXPECT_EQ(publish_and_read([&] { mutable_pool.push_back(values); },
                               [&] { return mutable_pool.size(); },
                               [&] { return static_cast<uint32_t>(mutable_pool[0][1]); }),
              23);

    auto const_pool = Pool(2, 5);
    EXPECT_EQ(publish_and_read([&] { const_pool.push_back(values); }, [&] { return const_pool.size(); }, [&] { return std::as_const(const_pool)[0][1]; }), 23);
}

TEST(CommonConcurrentPublicationTest, RawVectorPoolPublishesCompleteValues)
{
    using Pool = RawVectorPool<uint8_t, int, 16, true>;
    constexpr auto values = std::array<int, 3> { 17, 23, 29 };

    auto pool = Pool {};
    EXPECT_EQ(publish_and_read([&] { pool.insert(values); }, [&] { return pool.size(); }, [&] { return pool[0][2]; }), 29);
}

TEST(CommonConcurrentPublicationTest, RawArrayPoolPublishesCompleteValues)
{
    using Pool = RawArrayPool<int, 1, true>;
    constexpr auto values = std::array<int, 3> { 17, 23, 29 };

    auto pool = Pool(3);
    EXPECT_EQ(publish_and_read([&] { pool.insert(values); }, [&] { return pool.size(); }, [&] { return pool[0][2]; }), 29);
}

TEST(CommonConcurrentPublicationTest, RawPoolsSerializeAppendsAndPublishUniqueIndices)
{
    auto vector_pool = RawVectorPool<uint8_t, int, 16, true> {};
    expect_concurrent_pool_appends(vector_pool);

    auto array_pool = RawArrayPool<int, 1, true>(2);
    expect_concurrent_pool_appends(array_pool);
}

TEST(CommonConcurrentPublicationTest, RawSetsCanonicalizeDuringConcurrentInsertionAndLookup)
{
    auto vector_set = RawVectorSet<uint8_t, int, 16, true> {};
    expect_concurrent_set_inserts(vector_set);

    auto array_set = RawArraySet<int, 1, true>(2);
    expect_concurrent_set_inserts(array_set);
}
}  // namespace ygg::tests
