/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/containers/unordered_multi_map.hpp"

#include <algorithm>
#include <gtest/gtest.h>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <vector>

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

TEST(YggdrasilTests, UnorderedMultiMapSupportsMoveOnlyValuesAndMovesWithFreeSlots)
{
    UnorderedMultiMap<int, std::unique_ptr<int>> map;
    map.insert(1, std::make_unique<int>(42));
    map.insert(1, std::make_unique<int>(13));
    map.insert(2, std::make_unique<int>(17));
    ASSERT_TRUE(map.erase(2, *map.values(2).begin()));
    auto moved = std::move(map);
    EXPECT_TRUE(map.empty());
    map.insert(7, std::make_unique<int>(5));
    EXPECT_EQ(map.size(), 1);
    EXPECT_EQ(**map.values(7).begin(), 5);
    UnorderedMultiMap<int, std::unique_ptr<int>> assigned;
    assigned.insert(2, std::make_unique<int>(99));
    assigned = std::move(moved);
    EXPECT_TRUE(moved.empty());
    moved.insert(8, std::make_unique<int>(8));
    EXPECT_EQ(moved.size(), 1);
    EXPECT_EQ(**moved.values(8).begin(), 8);
    assigned.insert(1, std::make_unique<int>(7));  // Reuse the transferred free slot.
    int sum = 0;
    for (const auto& value : assigned.values(1))
        sum += *value;
    EXPECT_EQ(sum, 62);
    EXPECT_EQ(assigned.size(), 3);
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
    friend bool operator==(const ThrowingValue&, const ThrowingValue&) = default;
    ThrowingValue(ThrowingValue&& other) : value(other.value)
    {
        if (fail)
            throw std::runtime_error("value construction failed");
    }
};
}  // namespace

TEST(YggdrasilTests, UnorderedMultiMapFailedInsertionPreservesChainsAndFreeSlots)
{
    for (const bool reuse_slot : { false, true })
    {
        UnorderedMultiMap<int, ThrowingValue> map;
        map.reserve(4);
        map.insert(1, ThrowingValue(10));
        if (reuse_slot)
        {
            map.insert(2, ThrowingValue(30));
            ASSERT_TRUE(map.erase(2, ThrowingValue(30)));
        }
        const auto value = ThrowingValue(20);
        ThrowingValue::fail = true;
        EXPECT_THROW(map.insert(1, value), std::runtime_error);
        EXPECT_THROW(map.insert(2, value), std::runtime_error);
        ThrowingValue::fail = false;
        EXPECT_EQ(map.size(), 1);
        EXPECT_EQ((*map.values(1).begin()).value, 10);
        EXPECT_TRUE(map.values(2).empty());
        map.insert(2, value);
        map.insert(3, ThrowingValue(30));
        EXPECT_EQ(map.size(), 3);
        EXPECT_EQ((*map.values(1).begin()).value, 10);
        EXPECT_EQ((*map.values(2).begin()).value, 20);
        EXPECT_EQ((*map.values(3).begin()).value, 30);
    }
}

template<typename Map>
concept HasTemporaryMultimapValues = requires(Map&& map) { std::move(map).values(1); };

template<typename Map, typename Value>
concept CanReplaceMultimapValue = requires(Map& map, const Value& value) { map.replace(1, value, value); };

static_assert(!std::is_copy_constructible_v<UnorderedMultiMap<int, int>>);
static_assert(!std::is_copy_assignable_v<UnorderedMultiMap<int, int>>);
static_assert(std::is_move_constructible_v<UnorderedMultiMap<int, int>>);
static_assert(std::is_move_assignable_v<UnorderedMultiMap<int, int>>);
static_assert(!HasTemporaryMultimapValues<UnorderedMultiMap<int, int>>);

TEST(YggdrasilTests, UnorderedMultiMapReconstructsValuesWithoutRequiringAssignment)
{
    struct Value
    {
        const int value;
        bool operator==(const Value&) const = default;
    };
    using Map = UnorderedMultiMap<int, Value>;
    static_assert(!CanReplaceMultimapValue<Map, Value>);
    auto map = Map();
    map.insert(1, Value { 42 });
    map.insert(1, Value { 13 });
    ASSERT_TRUE(map.erase(1, Value { 42 }));
    map.insert(2, Value { 99 });
    EXPECT_EQ(map.size(), 2);
    EXPECT_EQ((*map.values(1).begin()).value, 13);
    EXPECT_EQ((*map.values(2).begin()).value, 99);
}

TEST(YggdrasilTests, UnorderedMultiMapMutatesDuplicatePairsWithHashCollisions)
{
    auto map = UnorderedMultiMap<MultimapKey, int>();
    static_assert(std::ranges::forward_range<decltype(map.values({ 0 }))>);
    static_assert(std::same_as<std::ranges::range_reference_t<decltype(map.values({ 0 }))>, const int&>);
    map.insert({ 1 }, 10);
    map.insert({ 18 }, 10);
    map.insert({ 35 }, 20);
    map.insert({ 2 }, 30);
    EXPECT_TRUE(map.erase({ 52 }, 10));
    EXPECT_EQ(map.size(), 3);
    EXPECT_TRUE(map.replace({ 18 }, 20, 25));
    EXPECT_FALSE(map.erase({ 2 }, 10));
    EXPECT_FALSE(map.replace({ 2 }, 99, 100));
    const auto values = map.values({ 1 });
    auto actual = std::vector<int>(values.begin(), values.end());
    std::ranges::sort(actual);
    EXPECT_EQ(actual, (std::vector<int> { 10, 25 }));
    EXPECT_EQ(*map.values({ 2 }).begin(), 30);
    EXPECT_TRUE(map.erase({ 1 }, 10));
    EXPECT_FALSE(map.erase({ 1 }, 10));
    EXPECT_EQ(map.size(), 2);
    map.clear();
    EXPECT_TRUE(map.empty());
    EXPECT_TRUE(map.values({ 1 }).empty());
}

TEST(YggdrasilTests, UnorderedMultiMapErasesHeadMiddleTailAndReusesSlotsAcrossKeys)
{
    UnorderedMultiMap<int, int> map;
    map.reserve(8);
    for (const auto value : { 10, 20, 30, 40 })
        map.insert(1, value);
    const auto values = [&](int key)
    {
        const auto range = map.values(key);
        auto result = std::vector<int>(range.begin(), range.end());
        std::ranges::sort(result);
        return result;
    };
    // Insertion prepends: the chain is 40 -> 30 -> 20 -> 10.
    EXPECT_TRUE(map.erase(1, 30));
    EXPECT_EQ(values(1), (std::vector<int> { 10, 20, 40 }));
    EXPECT_TRUE(map.erase(1, 10));
    EXPECT_EQ(values(1), (std::vector<int> { 20, 40 }));
    EXPECT_TRUE(map.erase(1, 40));
    EXPECT_EQ(values(1), (std::vector<int> { 20 }));
    map.insert(2, 55);
    map.insert(1, 60);
    EXPECT_EQ(values(1), (std::vector<int> { 20, 60 }));
    EXPECT_EQ(values(2), (std::vector<int> { 55 }));
    EXPECT_TRUE(map.erase(1, 20));
    EXPECT_TRUE(map.erase(1, 60));
    EXPECT_TRUE(map.values(1).empty());
    EXPECT_FALSE(map.erase(1, 60));
    map.insert(1, 70);
    EXPECT_EQ(values(1), (std::vector<int> { 70 }));
    EXPECT_EQ(values(2), (std::vector<int> { 55 }));
    EXPECT_EQ(map.size(), 2);
}

TEST(YggdrasilTests, UnorderedMultiMapReservesValuesIndependentlyOfKeys)
{
    UnorderedMultiMap<int, int> map;
    UnorderedMultiMap<int, int> distinct;
    map.reserve_values(1024);
    distinct.reserve(1024);
    for (int value = 0; value < 1024; ++value)
        map.insert(1, value);
    EXPECT_EQ(map.size(), 1024);
    EXPECT_EQ(std::ranges::distance(map.values(1)), 1024);
    EXPECT_LT(map.memory_usage(), distinct.memory_usage());
    const auto retained = map.memory_usage();
    map.reserve_values(0);
    map.reserve_values(1);
    EXPECT_EQ(map.memory_usage(), retained);
    map.clear();
    for (int value = 0; value < 1024; ++value)
        map.insert(2, value);
    EXPECT_EQ(map.memory_usage(), retained);
}

TEST(YggdrasilTests, UnorderedMultiMapErasureDoesNotGrowStorage)
{
    UnorderedMultiMap<int, int> map;
    map.reserve(1024);
    for (int value = 0; value < 1024; ++value)
        map.insert(value, value);
    const auto retained = map.memory_usage();
    for (int value = 0; value < 1024; ++value)
        ASSERT_TRUE(map.erase(value, value));
    EXPECT_TRUE(map.empty());
    EXPECT_EQ(map.memory_usage(), retained);
}

TEST(YggdrasilTests, UnorderedMultiMapReusesSlotsAcrossNovelKeys)
{
    auto map = UnorderedMultiMap<int, int>();
    constexpr int count = 64;
    // GTL needs headroom to clean tombstones without growing the key table.
    // A fixed warmup does not guarantee this across portable and SSE2 probing.
    map.reserve(2 * count);
    for (int value = 0; value < count; ++value)
        map.insert(value, value);
    const auto retained = map.memory_usage();
    for (int value = 0; value < 64 * count; ++value)
    {
        ASSERT_TRUE(map.erase(value, value));
        map.insert(value + count, value + count);
        EXPECT_EQ(map.size(), count);
    }
    EXPECT_EQ(map.memory_usage(), retained);
    map.clear();
    map.reserve(0);
    map.reserve(1);
    EXPECT_EQ(map.memory_usage(), retained);
    for (int value = 0; value < count; ++value)
        map.insert(value, value);
    EXPECT_EQ(map.memory_usage(), retained);
}

TEST(YggdrasilTests, UnorderedMultiMapErasureAndClearDestroyOwnedValuesImmediately)
{
    UnorderedMultiMap<int, std::shared_ptr<int>> map;
    auto value = std::make_shared<int>(42);
    map.insert(1, value);
    map.insert(1, value);
    EXPECT_EQ(value.use_count(), 3);
    EXPECT_TRUE(map.erase(1, value));
    EXPECT_EQ(value.use_count(), 2);
    EXPECT_TRUE(map.erase(1, value));
    EXPECT_EQ(value.use_count(), 1);
    map.insert(2, value);
    map.clear();
    EXPECT_EQ(value.use_count(), 1);
    map.insert(3, value);
    EXPECT_EQ(value.use_count(), 2);
    EXPECT_EQ(map.size(), 1);
}

}  // namespace ygg::tests
