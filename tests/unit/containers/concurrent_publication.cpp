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

#include <array>
#include <cstdint>
#include <functional>
#include <gtest/gtest.h>
#include <thread>
#include <utility>
#include <yggdrasil/containers/bit_packed_array_pool.hpp>
#include <yggdrasil/containers/block_array_pool.hpp>
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
}  // namespace ygg::tests
