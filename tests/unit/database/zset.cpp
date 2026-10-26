/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/semantics/zset.hpp"

#include <array>
#include <gtest/gtest.h>
#include <map>
#include <random>
#include <tuple>
#include <vector>

namespace ygg::tests
{
namespace
{
namespace db = database;
using Label = Index<db::Column>;

std::vector<std::byte> row_of(uint_t a, uint_t b)
{
    auto bytes = std::vector<std::byte>(2 * ByteCodec<uint_t>::size);
    ByteCodec<uint_t>::encode(a, std::span(bytes).first(ByteCodec<uint_t>::size));
    ByteCodec<uint_t>::encode(b, std::span(bytes).last(ByteCodec<uint_t>::size));
    return bytes;
}

constexpr std::array labels = { Label(0), Label(1) };
}  // namespace

TEST(DatabaseZSet, ConsolidatesAndErasesAtZero)
{
    auto zset = db::ZSet(labels);
    EXPECT_EQ(zset.add(row_of(1, 2), 2), 2);
    EXPECT_EQ(zset.add(row_of(3, 4), -1), -1);
    EXPECT_EQ(zset.add(row_of(1, 2), 1), 3);
    EXPECT_EQ(zset.size(), 2);
    EXPECT_EQ(zset.weight(row_of(1, 2)), 3);
    EXPECT_EQ(zset.weight(row_of(3, 4)), -1);
    EXPECT_EQ(zset.weight(row_of(5, 6)), 0);
    EXPECT_EQ(zset.add(row_of(1, 2), -3), 0);
    EXPECT_EQ(zset.size(), 1);
    EXPECT_FALSE(zset.rows().contains(row_of(1, 2)));
    EXPECT_EQ(zset.weight(size_t { 0 }), -1);
    EXPECT_EQ(zset.add(row_of(3, 4), 0), -1);
}

TEST(DatabaseZSet, AddsScaledZSetsAndRelations)
{
    auto lhs = db::ZSet(labels);
    lhs.add(row_of(1, 1), 1);
    lhs.add(row_of(2, 2), 2);
    auto rhs = db::ZSet(labels);
    rhs.add(row_of(2, 2), 1);
    rhs.add(row_of(3, 3), 4);
    lhs.add(rhs, -2);
    EXPECT_EQ(lhs.weight(row_of(1, 1)), 1);
    EXPECT_EQ(lhs.weight(row_of(2, 2)), 0);
    EXPECT_EQ(lhs.weight(row_of(3, 3)), -8);
    EXPECT_EQ(lhs.size(), 2);
    lhs.add(rhs.rows(), 2);
    EXPECT_EQ(lhs.weight(row_of(2, 2)), 2);
    EXPECT_EQ(lhs.weight(row_of(3, 3)), -6);
    lhs.clear();
    EXPECT_TRUE(lhs.empty());
}

TEST(DatabaseZSet, AgreesWithAMapUnderRandomUpdates)
{
    auto rng = std::mt19937(5);
    auto zset = db::ZSet(labels);
    auto expected = std::map<std::pair<uint_t, uint_t>, db::Weight>();
    for (int step = 0; step < 2000; ++step)
    {
        const auto a = static_cast<uint_t>(rng() % 8), b = static_cast<uint_t>(rng() % 8);
        const auto weight = static_cast<db::Weight>(rng() % 5) - 2;
        const auto result = zset.add(row_of(a, b), weight);
        auto& value = expected[{ a, b }];
        value += weight;
        EXPECT_EQ(result, value);
        if (value == 0)
            expected.erase({ a, b });
    }
    EXPECT_EQ(zset.size(), expected.size());
    for (const auto& [key, weight] : expected)
        EXPECT_EQ(zset.weight(row_of(key.first, key.second)), weight);
}
}  // namespace ygg::tests
