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
#include <yggdrasil/containers/unique_object_pool.hpp>

namespace ygg::tests
{

struct UniquePoolValue
{
    int value = 0;

    void initialize(int next_value) { value = next_value; }
};

TEST(YggdrasilTests, CommonUniqueObjectPoolReportsStorageSize)
{
    auto pool = ygg::UniqueObjectPool<UniquePoolValue>();

    EXPECT_EQ(pool.size(), 0);
    EXPECT_TRUE(pool.empty());
    EXPECT_EQ(pool.get_size(), pool.size());
    EXPECT_EQ(pool.free_size(), 0);
    EXPECT_EQ(pool.get_num_free(), pool.free_size());

    {
        auto first = pool.get_or_allocate(1);
        EXPECT_EQ(first->value, 1);
        EXPECT_EQ(pool.size(), 1);
    EXPECT_FALSE(pool.empty());
        EXPECT_EQ(pool.get_size(), pool.size());
        EXPECT_EQ(pool.free_size(), 0);
        EXPECT_EQ(pool.get_num_free(), pool.free_size());

        auto second = pool.get_or_allocate(2);
        EXPECT_EQ(second->value, 2);
        EXPECT_EQ(pool.size(), 2);
    EXPECT_FALSE(pool.empty());
        EXPECT_EQ(pool.get_size(), pool.size());
        EXPECT_EQ(pool.free_size(), 0);
        EXPECT_EQ(pool.get_num_free(), pool.free_size());
    }

    EXPECT_EQ(pool.size(), 2);
    EXPECT_FALSE(pool.empty());
    EXPECT_EQ(pool.get_size(), pool.size());
    EXPECT_EQ(pool.free_size(), 2);
    EXPECT_EQ(pool.get_num_free(), pool.free_size());

    auto reused = pool.get_or_allocate(3);
    EXPECT_EQ(reused->value, 3);
    EXPECT_EQ(pool.size(), 2);
    EXPECT_FALSE(pool.empty());
    EXPECT_EQ(pool.get_size(), pool.size());
    EXPECT_EQ(pool.free_size(), 1);
    EXPECT_EQ(pool.get_num_free(), pool.free_size());
}

TEST(YggdrasilTests, CommonThreadSafeUniqueObjectPoolReportsStorageSize)
{
    auto pool = ygg::UniqueObjectPool<UniquePoolValue, true>();

    auto value = pool.get_or_allocate(4);

    EXPECT_EQ(value->value, 4);
    EXPECT_EQ(pool.size(), 1);
    EXPECT_FALSE(pool.empty());
    EXPECT_EQ(pool.get_size(), pool.size());
    EXPECT_EQ(pool.free_size(), 0);
    EXPECT_EQ(pool.get_num_free(), pool.free_size());
}

}
