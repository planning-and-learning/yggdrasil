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

#include <gtest/gtest.h>
#include <stdexcept>
#include <yggdrasil/containers/shared_object_pool.hpp>

namespace ygg::tests
{

struct SharedPoolValue
{
    int value = 0;

    void initialize(int next_value) { value = next_value; }
};

struct ThrowingSharedPoolValue
{
    int value = 0;

    void initialize(int next_value)
    {
        if (next_value < 0)
            throw std::runtime_error("negative value");
        value = next_value;
    }
};

template<bool ThreadSafe>
void expect_shared_pool_recovers_from_failed_initialization()
{
    auto pool = ygg::SharedObjectPool<ThrowingSharedPoolValue, ThreadSafe>();

    EXPECT_THROW((void) pool.get_or_allocate(-1), std::runtime_error);
    EXPECT_EQ(pool.size(), 1);
    EXPECT_EQ(pool.free_size(), 1);

    auto value = pool.get_or_allocate(3);
    EXPECT_EQ(value->value, 3);
    EXPECT_EQ(value.ref_count(), 1);
    EXPECT_EQ(pool.size(), 1);
    EXPECT_EQ(pool.free_size(), 0);
}

TEST(YggdrasilTests, CommonSharedObjectPoolReportsStorageSize)
{
    auto pool = ygg::SharedObjectPool<SharedPoolValue>();

    EXPECT_EQ(pool.size(), 0);
    EXPECT_TRUE(pool.empty());
    EXPECT_EQ(pool.get_size(), pool.size());
    EXPECT_EQ(pool.free_size(), 0);
    EXPECT_EQ(pool.get_num_free(), pool.free_size());

    {
        auto first = pool.get_or_allocate(1);
        EXPECT_EQ(first->value, 1);
        EXPECT_EQ(first.ref_count(), 1);
        EXPECT_EQ(pool.size(), 1);
        EXPECT_FALSE(pool.empty());
        EXPECT_EQ(pool.get_size(), pool.size());
        EXPECT_EQ(pool.free_size(), 0);
        EXPECT_EQ(pool.get_num_free(), pool.free_size());

        auto copied = first;
        EXPECT_EQ(copied.ref_count(), 2);
        EXPECT_EQ(pool.size(), 1);
        EXPECT_FALSE(pool.empty());
        EXPECT_EQ(pool.free_size(), 0);
        EXPECT_EQ(pool.get_num_free(), pool.free_size());
    }

    EXPECT_EQ(pool.size(), 1);
    EXPECT_FALSE(pool.empty());
    EXPECT_EQ(pool.get_size(), pool.size());
    EXPECT_EQ(pool.free_size(), 1);
    EXPECT_EQ(pool.get_num_free(), pool.free_size());

    auto reused = pool.get_or_allocate(2);
    EXPECT_EQ(reused->value, 2);
    EXPECT_EQ(pool.size(), 1);
    EXPECT_FALSE(pool.empty());
    EXPECT_EQ(pool.get_size(), pool.size());
    EXPECT_EQ(pool.free_size(), 0);
    EXPECT_EQ(pool.get_num_free(), pool.free_size());
}

TEST(YggdrasilTests, CommonThreadSafeSharedObjectPoolReportsStorageSize)
{
    auto pool = ygg::SharedObjectPool<SharedPoolValue, true>();

    auto value = pool.get_or_allocate(4);

    EXPECT_EQ(value->value, 4);
    EXPECT_EQ(value.ref_count(), 1);
    EXPECT_EQ(pool.size(), 1);
    EXPECT_FALSE(pool.empty());
    EXPECT_EQ(pool.get_size(), pool.size());
    EXPECT_EQ(pool.free_size(), 0);
    EXPECT_EQ(pool.get_num_free(), pool.free_size());
}

TEST(YggdrasilTests, CommonThreadSafeSharedObjectPoolReusesReleasedObjects)
{
    auto pool = ygg::SharedObjectPool<SharedPoolValue, true>();
    SharedPoolValue* first_address = nullptr;

    {
        auto first = pool.get_or_allocate(7);
        first_address = first.get();
        EXPECT_EQ(first->value, 7);
        EXPECT_EQ(first.ref_count(), 1);
        EXPECT_EQ(pool.free_size(), 0);
    }

    EXPECT_EQ(pool.size(), 1);
    EXPECT_EQ(pool.free_size(), 1);

    auto reused = pool.get_or_allocate(8);
    EXPECT_EQ(reused.get(), first_address);
    EXPECT_EQ(reused->value, 8);
    EXPECT_EQ(reused.ref_count(), 1);
    EXPECT_EQ(pool.size(), 1);
    EXPECT_EQ(pool.free_size(), 0);
}

TEST(YggdrasilTests, CommonSharedObjectPoolPtrMoveCopyAndCloneTrackReferences)
{
    auto pool = ygg::SharedObjectPool<SharedPoolValue>();

    auto original = pool.get_or_allocate(5);
    EXPECT_EQ(original.ref_count(), 1);

    auto copied = original;
    EXPECT_EQ(original.ref_count(), 2);
    EXPECT_EQ(copied.ref_count(), 2);

    auto moved = std::move(original);
    EXPECT_FALSE(original);
    EXPECT_EQ(moved.ref_count(), 2);

    auto cloned = moved.clone();
    ASSERT_TRUE(cloned);
    EXPECT_NE(cloned.get(), moved.get());
    EXPECT_EQ(cloned->value, moved->value);
    EXPECT_EQ(cloned.ref_count(), 1);
    EXPECT_EQ(pool.size(), 2);

    copied = {};
    EXPECT_EQ(moved.ref_count(), 1);
    moved = {};
    EXPECT_EQ(pool.free_size(), 1);
    cloned = {};
    EXPECT_EQ(pool.free_size(), 2);
}

TEST(YggdrasilTests, CommonSharedObjectPoolRecoversFromFailedInitialization) { expect_shared_pool_recovers_from_failed_initialization<false>(); }

TEST(YggdrasilTests, CommonThreadSafeSharedObjectPoolRecoversFromFailedInitialization) { expect_shared_pool_recovers_from_failed_initialization<true>(); }

}  // namespace ygg::tests
