/*
 * Copyright (C) 2026 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "yggdrasil/containers/sparse_bitset.hpp"

#include "gtest/gtest.h"
#include <cstddef>
#include <vector>

namespace ygg::tests
{
namespace
{

std::vector<size_t> elements(const SparseBitset<>& set) { return std::vector<size_t>(set.begin(), set.end()); }

TEST(YggdrasilTests, SparseBitsetIteratesTouchedWordsInFirstTouchOrder)
{
    auto set = SparseBitset<> {};
    EXPECT_TRUE(set.empty());
    EXPECT_EQ(set.begin(), set.end());

    EXPECT_TRUE(set.insert(size_t(1) << 31));
    EXPECT_TRUE(set.insert(70));
    EXPECT_TRUE(set.insert(65));
    EXPECT_FALSE(set.insert(70));
    EXPECT_TRUE(set.insert(3));

    EXPECT_TRUE(set.contains(65));
    EXPECT_FALSE(set.contains(66));
    EXPECT_FALSE(set.contains(1000));
    EXPECT_EQ(elements(set), (std::vector<size_t> { size_t(1) << 31, 65, 70, 3 }));
}

TEST(YggdrasilTests, SparseBitsetResetClearsOnlyTouchedWords)
{
    auto set = SparseBitset<> {};
    set.insert(5);
    set.insert(500);
    set.reset();

    EXPECT_TRUE(set.empty());
    EXPECT_FALSE(set.contains(5));
    EXPECT_FALSE(set.contains(500));
    EXPECT_EQ(elements(set), std::vector<size_t> {});

    EXPECT_TRUE(set.insert(500));
    EXPECT_EQ(elements(set), std::vector<size_t> { 500 });
}

}
}
