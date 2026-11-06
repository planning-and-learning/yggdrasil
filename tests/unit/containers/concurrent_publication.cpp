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
#include <urd/repr/sequences/segmented.hpp>
#include <urd/repr/sequences/sequence.hpp>
#include <urd/store.hpp>
#include <utility>
#include <vector>

#ifndef NDEBUG
#error "Concurrent publication tests must be compiled with NDEBUG."
#endif

namespace ygg::tests
{
namespace
{
template<typename Publish, typename Size, typename Read>
auto publish_and_read(Publish publish, Size size, Read read)
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

struct BlockingValue
{
    int value;
    std::atomic_size_t* copies;
    std::atomic_bool* started;
    std::atomic_bool* release;

    BlockingValue(int value_, std::atomic_size_t* copies_ = nullptr, std::atomic_bool* started_ = nullptr, std::atomic_bool* release_ = nullptr) noexcept :
        value(value_),
        copies(copies_),
        started(started_),
        release(release_)
    {
    }

    // Constructing from another value, by copy or move, is counted and may block.
    BlockingValue(const BlockingValue& other) noexcept : value(other.value), copies(other.copies), started(other.started), release(other.release)
    { on_transfer(); }
    BlockingValue(BlockingValue&& other) noexcept : value(other.value), copies(other.copies), started(other.started), release(other.release) { on_transfer(); }
    BlockingValue& operator=(const BlockingValue&) = default;
    BlockingValue& operator=(BlockingValue&&) = default;
    ~BlockingValue() = default;

private:
    void on_transfer() const noexcept
    {
        if (!copies || copies->fetch_add(1, std::memory_order_relaxed) != 1)
            return;
        started->store(true, std::memory_order_release);
        while (!release->load(std::memory_order_acquire))
            std::this_thread::yield();
    }
};

}  // namespace

TEST(CommonConcurrentPublicationTest, SegmentedVectorPublishesForMutableAndConstAccess)
{
    auto mutable_vector = urd::Store<urd::sequence<uint32_t>, urd::stable_refs, urd::concurrent> {};
    EXPECT_EQ(publish_and_read([&] { mutable_vector.push_back(17); }, [&] { return mutable_vector.size(); }, [&] { return mutable_vector[0]; }), 17);

    auto const_vector = urd::Store<urd::sequence<uint32_t>, urd::stable_refs, urd::concurrent> {};
    EXPECT_EQ(publish_and_read([&] { const_vector.push_back(23); }, [&] { return const_vector.size(); }, [&] { return std::as_const(const_vector)[0]; }), 23);
}

}  // namespace ygg::tests
