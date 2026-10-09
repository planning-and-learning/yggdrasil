/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/containers/fibonacci_heap.hpp"

#include "yggdrasil/core/counting_memory_resource.hpp"

#include <functional>
#include <gtest/gtest.h>
#include <memory_resource>

namespace ygg::tests
{
TEST(YggdrasilTests, FibonacciHeapsSharePoolAndRetainMutableHandles)
{
    CountingMemoryResource memory;
    std::pmr::unsynchronized_pool_resource pool(&memory);
    {
        FibonacciHeap<int> maximum { FibonacciHeap<int>::allocator_type { &pool } };
        FibonacciHeap<int, std::greater<int>> minimum { FibonacciHeap<int, std::greater<int>>::allocator_type { &pool } };
        maximum.push(2);
        maximum.push(7);
        EXPECT_EQ(maximum.top(), 7);

        const auto handle = minimum.push(7);
        minimum.push(2);
        const auto erased = minimum.push(4);
        EXPECT_EQ(minimum.top(), 2);
        minimum.update(handle, 1);
        EXPECT_EQ(minimum.top(), 1);
        minimum.update(handle, 8);
        EXPECT_EQ(minimum.top(), 2);
        minimum.erase(erased);
        minimum.pop();
        EXPECT_EQ(minimum.top(), 8);
        minimum.pop();
        EXPECT_TRUE(minimum.empty());
        EXPECT_GT(memory.memory_usage(), 0U);
    }
    EXPECT_GT(memory.memory_usage(), 0U);
    pool.release();
    EXPECT_EQ(memory.memory_usage(), 0U);
}
}  // namespace ygg::tests
