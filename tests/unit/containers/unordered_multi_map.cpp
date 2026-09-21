/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <algorithm>
#include <gtest/gtest.h>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <unordered_map>
#include <vector>
#include <yggdrasil/containers/unordered_multi_map.hpp>

namespace ygg::tests
{
struct MultimapKey
{
    int value;
};
}  // namespace ygg::tests

namespace ygg
{
template<>
struct Hash<tests::MultimapKey>
{
    hash_t operator()(const tests::MultimapKey&) const noexcept { return 0; }
};

template<>
struct EqualTo<tests::MultimapKey>
{
    bool operator()(const tests::MultimapKey& lhs, const tests::MultimapKey& rhs) const noexcept { return lhs.value % 17 == rhs.value % 17; }
};
}  // namespace ygg

namespace ygg::tests
{

TEST(YggdrasilTests, UnorderedMultiMapMatchesReferenceWithDuplicatesAndHashCollisions)
{
    UnorderedMultiMap<MultimapKey, int> map;
    static_assert(std::ranges::forward_range<decltype(map.values({ 0 }))>);
    static_assert(std::same_as<std::ranges::range_reference_t<decltype(map.values({ 0 }))>, const int&>);
    EXPECT_TRUE(map.empty());
    EXPECT_TRUE(map.values({ 0 }).empty());

    for (int round = 0; round < 3; ++round)
    {
        std::unordered_multimap<int, int> reference;
        for (int i = 0; i < 512; ++i)
        {
            const auto key = (i * 13 + round) % 67;
            const auto value = i % 7;  // Include identical key/value pairs.
            map.insert({ key }, value);
            reference.emplace(key % 17, value);
        }
        EXPECT_EQ(map.size(), reference.size());
        for (int key = 0; key < 17; ++key)
        {
            const auto values = map.values({ key + 17 });
            auto actual = std::vector<int>(values.begin(), values.end());
            auto expected = std::vector<int>();
            const auto [first, last] = reference.equal_range(key);
            for (auto it = first; it != last; ++it)
                expected.push_back(it->second);
            std::ranges::sort(actual);
            std::ranges::sort(expected);
            EXPECT_EQ(actual, expected);
        }
        EXPECT_TRUE(map.values({ -1 }).empty());
        map.clear();
        EXPECT_EQ(map.size(), 0);
        for (int key = 0; key < 17; ++key)
            EXPECT_TRUE(map.values({ key }).empty());
    }
}

TEST(YggdrasilTests, UnorderedMultiMapSupportsMoveOnlyValuesAndContainerMoves)
{
    UnorderedMultiMap<int, std::unique_ptr<int>> map;
    map.insert(1, std::make_unique<int>(42));
    map.insert(1, std::make_unique<int>(13));
    auto moved = std::move(map);
    UnorderedMultiMap<int, std::unique_ptr<int>> assigned;
    assigned.insert(2, std::make_unique<int>(99));
    assigned = std::move(moved);
    int sum = 0;
    for (const auto& value : assigned.values(1))
        sum += *value;
    EXPECT_EQ(sum, 55);
    EXPECT_EQ(assigned.size(), 2);
    EXPECT_TRUE(assigned.values(2).empty());
}

namespace
{
struct ThrowingValue
{
    static inline bool fail = false;
    int value;
    explicit ThrowingValue(int value) : value(value) {}
    ThrowingValue(const ThrowingValue&) = default;
    ThrowingValue(ThrowingValue&& other) : value(other.value)
    {
        if (fail)
            throw std::runtime_error("value construction failed");
    }
};
}  // namespace

TEST(YggdrasilTests, UnorderedMultiMapFailedInsertionPreservesKeyChains)
{
    UnorderedMultiMap<int, ThrowingValue> map;
    map.insert(1, ThrowingValue(10));
    const auto value = ThrowingValue(20);
    ThrowingValue::fail = true;
    EXPECT_THROW(map.insert(1, value), std::runtime_error);
    EXPECT_THROW(map.insert(2, value), std::runtime_error);
    ThrowingValue::fail = false;
    EXPECT_EQ(map.size(), 1);
    EXPECT_EQ((*map.values(1).begin()).value, 10);
    EXPECT_TRUE(map.values(2).empty());
    map.insert(2, value);
    EXPECT_EQ(map.size(), 2);
    EXPECT_EQ((*map.values(2).begin()).value, 20);
}

}  // namespace ygg::tests
