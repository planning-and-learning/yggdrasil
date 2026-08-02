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

#include <algorithm>
#include <array>
#include <concepts>
#include <gtest/gtest.h>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>
#include <yggdrasil/containers/raw_array_set.hpp>

namespace ygg::tests
{

using DefaultRawArraySet = RawArraySet<int, 2>;
using ConcurrentRawArraySet = RawArraySet<int, 2, true>;

static_assert(!DefaultRawArraySet::thread_safe);
static_assert(ConcurrentRawArraySet::thread_safe);
static_assert(std::same_as<decltype(std::declval<DefaultRawArraySet&>()[0]), std::span<const int>>);
static_assert(std::same_as<decltype(std::declval<ConcurrentRawArraySet&>()[0]), std::span<const int>>);
static_assert(!std::is_copy_constructible_v<DefaultRawArraySet>);
static_assert(std::is_move_constructible_v<ConcurrentRawArraySet>);
static_assert(std::is_move_assignable_v<ConcurrentRawArraySet>);

struct RawArraySetCountingElement
{
    int value;
    static inline size_t hash_calls = 0;

    friend bool operator==(const RawArraySetCountingElement&, const RawArraySetCountingElement&) = default;
};

}  // namespace ygg::tests

namespace ygg
{

template<>
struct Hash<tests::RawArraySetCountingElement>
{
    hash_t operator()(const tests::RawArraySetCountingElement& value) const noexcept
    {
        ++tests::RawArraySetCountingElement::hash_calls;
        return static_cast<hash_t>(value.value);
    }
};

}  // namespace ygg

namespace ygg::tests
{

TEST(YggdrasilTests, CommonRawArraySetRejectsWrongLengthInputs)
{
    auto set = ygg::RawArraySet<int, 2>(3);
    const auto too_short = std::array<int, 2> { 1, 2 };
    const auto too_long = std::array<int, 4> { 1, 2, 3, 4 };

    EXPECT_THROW(set.insert(too_short), std::invalid_argument);
    EXPECT_THROW(set.find(too_short), std::invalid_argument);
    EXPECT_THROW(set.contains(too_long), std::invalid_argument);
}

TEST(YggdrasilTests, CommonRawArraySetStoresFixedLengthArrays)
{
    auto set = ygg::RawArraySet<int, 2>(3);

    const auto first = std::vector<int> { 1, 2, 3 };
    const auto second = std::vector<int> { 1, 2, 4 };
    const auto third = std::array<int, 3> { 1, 2, 5 };

    EXPECT_TRUE(set.empty());

    EXPECT_EQ(set.insert(first), 0);
    EXPECT_EQ(set.insert(second), 1);
    EXPECT_EQ(set.insert(third), 2);
    EXPECT_EQ(set.insert(first), 0);

    EXPECT_EQ(set.size(), 3);
    EXPECT_FALSE(set.empty());
    EXPECT_TRUE(set.contains(first));
    EXPECT_TRUE(set.contains(second));
    EXPECT_TRUE(set.contains(third));
    EXPECT_FALSE(set.contains(std::array<int, 3> { 9, 9, 9 }));
    EXPECT_EQ(set.find(first), 0);
    EXPECT_EQ(set.find(second), 1);
    EXPECT_EQ(set.find(third), 2);
    EXPECT_EQ(set.find(std::array<int, 3> { 9, 9, 9 }), std::nullopt);

    EXPECT_TRUE(std::ranges::equal(set[0], first));
    EXPECT_TRUE(std::ranges::equal(set.front(), first));
    EXPECT_TRUE(std::ranges::equal(set.back(), third));
    EXPECT_TRUE(std::ranges::equal(set[1], second));
    EXPECT_TRUE(std::ranges::equal(set.at(1), second));
    EXPECT_TRUE(std::ranges::equal(set[2], third));
    EXPECT_THROW(set.at(3), std::out_of_range);
}

TEST(YggdrasilTests, CommonRawArraySetEmptyAccessThrows)
{
    auto set = ygg::RawArraySet<int, 2>(3);
    const auto& const_set = set;

    EXPECT_THROW(set.front(), std::out_of_range);
    EXPECT_THROW(const_set.front(), std::out_of_range);
    EXPECT_THROW(set.back(), std::out_of_range);
    EXPECT_THROW(const_set.back(), std::out_of_range);
}

TEST(YggdrasilTests, CommonRawArraySetStoresSingleZeroLengthArray)
{
    auto set = ygg::RawArraySet<int, 2>(0);
    const auto value = std::array<int, 0> {};

    EXPECT_TRUE(set.empty());
    EXPECT_EQ(set.array_size(), 0);

    EXPECT_EQ(set.insert(value), 0);
    EXPECT_EQ(set.insert(value), 0);

    EXPECT_EQ(set.size(), 1);
    EXPECT_FALSE(set.empty());
    EXPECT_TRUE(set.contains(value));
    EXPECT_EQ(set.find(value), 0);
    EXPECT_TRUE(set[0].empty());
    EXPECT_EQ(set[0].data(), nullptr);
    EXPECT_TRUE(set.front().empty());
    EXPECT_TRUE(set.back().empty());
}

TEST(YggdrasilTests, CommonRawArraySetClearKeepsContainerReusable)
{
    auto set = ygg::RawArraySet<int, 1>(2);

    EXPECT_EQ(set.insert(std::vector<int>({ 1, 2 })), 0);
    set.clear();
    EXPECT_TRUE(set.empty());

    const auto value = std::vector<int> { 3, 4 };
    EXPECT_EQ(set.insert(value), 0);
    EXPECT_EQ(set.size(), 1);
    EXPECT_TRUE(set.contains(value));
    EXPECT_EQ(set.find(value), 0);
}

TEST(YggdrasilTests, CommonRawArraySetInsertHashesEachElementOnce)
{
    auto set = ygg::RawArraySet<RawArraySetCountingElement, 2>(2);
    const auto value = std::array<RawArraySetCountingElement, 2> { RawArraySetCountingElement { 1 }, RawArraySetCountingElement { 2 } };

    RawArraySetCountingElement::hash_calls = 0;
    EXPECT_EQ(set.insert(value), 0);
    EXPECT_EQ(RawArraySetCountingElement::hash_calls, value.size());

    RawArraySetCountingElement::hash_calls = 0;
    EXPECT_EQ(set.insert(value), 0);
    EXPECT_EQ(RawArraySetCountingElement::hash_calls, value.size());
}

TEST(YggdrasilTests, CommonRawArraySetMoveKeepsHashFunctorsBoundToStorage)
{
    const auto value = std::array<int, 2> { 1, 2 };
    auto source = ygg::RawArraySet<int, 2>(2);
    EXPECT_EQ(source.insert(value), 0);

    auto moved = std::move(source);
    EXPECT_TRUE(moved.contains(value));

    auto assigned = ygg::RawArraySet<int, 2>(2);
    assigned = std::move(moved);
    EXPECT_EQ(assigned.find(value), 0);
    EXPECT_TRUE(std::ranges::equal(assigned[0], value));
}

}  // namespace ygg::tests
