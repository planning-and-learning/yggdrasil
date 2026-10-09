/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/core/counting_memory_resource.hpp"

#include <cstdint>
#include <gtest/gtest.h>
#include <memory_resource>
#include <new>
#include <type_traits>

namespace ygg::tests
{
static_assert(!std::is_copy_constructible_v<CountingMemoryResource>);
static_assert(!std::is_move_constructible_v<CountingMemoryResource>);

TEST(YggdrasilTests, CountingMemoryResourceForwardsAndCountsAllocations)
{
    CountingMemoryResource upstream;
    std::pmr::memory_resource& upstream_resource = upstream;
    CountingMemoryResource memory(upstream_resource);
    auto* first = memory.allocate(64, 64);
    auto* second = memory.allocate(17, 1);

    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(first) % 64, 0U);
    EXPECT_EQ(memory.memory_usage(), 81U);
    EXPECT_EQ(upstream.memory_usage(), 81U);
    memory.deallocate(first, 64, 64);
    EXPECT_EQ(memory.memory_usage(), 17U);
    EXPECT_EQ(upstream.memory_usage(), 17U);
    memory.deallocate(second, 17, 1);
    EXPECT_EQ(memory.memory_usage(), 0U);
    EXPECT_EQ(upstream.memory_usage(), 0U);
}

TEST(YggdrasilTests, CountingMemoryResourceDoesNotCountFailedAllocations)
{
    CountingMemoryResource memory(*std::pmr::null_memory_resource());
    EXPECT_THROW((void) memory.allocate(1), std::bad_alloc);
    EXPECT_EQ(memory.memory_usage(), 0U);
}

TEST(YggdrasilTests, CountingMemoryResourceKeepsAllocationOwnershipSeparate)
{
    CountingMemoryResource first;
    CountingMemoryResource second;
    EXPECT_TRUE(first.is_equal(first));
    EXPECT_FALSE(first.is_equal(second));
    EXPECT_FALSE(first.is_equal(*std::pmr::new_delete_resource()));
}
}  // namespace ygg::tests
