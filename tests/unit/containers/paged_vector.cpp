/*
 * Copyright (C) 2026 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "yggdrasil/containers/paged_vector.hpp"

#include "gtest/gtest.h"
#include <barrier>
#include <cstddef>
#include <thread>
#include <vector>

namespace ygg::tests
{
namespace
{

TEST(YggdrasilTests, PagedVectorAllocatesTouchedPagesOnly)
{
    auto vector = PagedVector<int, 4> {};
    EXPECT_EQ(vector.find(1000), nullptr);

    auto& element = vector.get_or_create(1000);
    EXPECT_EQ(element, 0);
    element = 42;

    ASSERT_NE(vector.find(1000), nullptr);
    EXPECT_EQ(*vector.find(1000), 42);
    EXPECT_EQ(vector.find(1001) != nullptr, true);
    EXPECT_EQ(*vector.find(1001), 0);
    EXPECT_EQ(vector.find(0), nullptr);
    EXPECT_EQ(vector.find(size_t(1) << 31), nullptr);

    auto& far = vector.get_or_create((size_t(1) << 31) + 7);
    far = 7;
    EXPECT_EQ(&vector.get_or_create(1000), &element);
    EXPECT_EQ(*vector.find((size_t(1) << 31) + 7), 7);
}

TEST(YggdrasilTests, ConcurrentPagedVectorPublishesPagesOnce)
{
    constexpr size_t kThreads = 8;
    constexpr size_t kIndices = 1 << 12;
    auto vector = PagedVector<int, 6, true> {};
    auto addresses = std::vector<std::vector<int*>>(kThreads, std::vector<int*>(kIndices));
    auto start = std::barrier<>(static_cast<std::ptrdiff_t>(kThreads));

    {
        auto threads = std::vector<std::jthread> {};
        for (size_t thread = 0; thread < kThreads; ++thread)
            threads.emplace_back(
                [&, thread]
                {
                    start.arrive_and_wait();
                    for (size_t i = 0; i < kIndices; ++i)
                        addresses[thread][i] = &vector.get_or_create(i * 31);
                });
    }

    for (size_t thread = 1; thread < kThreads; ++thread)
        EXPECT_EQ(addresses[thread], addresses[0]);
}

}
}
