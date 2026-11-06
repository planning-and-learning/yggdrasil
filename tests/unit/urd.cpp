/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

// urd ships in the native prefix (src/urd): its Stores resolve and work with the prefix's headers.

#include <urd/store.hpp>
#include <urd/repr/queues/addressable_heap.hpp>

#include <gtest/gtest.h>
#include <utility>

namespace ygg::tests
{
TEST(YggdrasilTests, UrdStoresResolveFromTheNativePrefix)
{
    using Sorted = urd::Store<urd::set<int>, urd::sorted>;
    using Entry = std::pair<unsigned, unsigned>;
    // by_priority: the least by the order leaves first (a min-heap under urd::Less).
    using Heap = urd::Store<urd::queue<Entry>, urd::by_priority, urd::handles>;

    auto sorted = Sorted {};
    sorted.insert(3);
    sorted.insert(1);
    EXPECT_EQ(*sorted.begin(), 1);

    auto heap = Heap {};
    const auto handle = heap.push({ 5, 0 });
    heap.push({ 3, 1 });
    heap.update(handle, { 1, 0 });
    EXPECT_EQ(heap.top(), (Entry { 1, 0 }));
}
}  // namespace ygg::tests
