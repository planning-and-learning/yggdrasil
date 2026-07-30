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

#include <array>
#include <gtest/gtest.h>
#include <stdexcept>
#include <vector>
#include <yggdrasil/containers/raw_array_set.hpp>

namespace ygg::tests
{

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

    EXPECT_EQ(std::vector<int>(set[0], set[0] + set.array_size()), first);
    EXPECT_EQ(std::vector<int>(set.front(), set.front() + set.array_size()), first);
    EXPECT_EQ(std::vector<int>(set.back(), set.back() + set.array_size()), std::vector<int>(third.begin(), third.end()));
    EXPECT_EQ(std::vector<int>(set[1], set[1] + set.array_size()), second);
    EXPECT_EQ(std::vector<int>(set.at(1), set.at(1) + set.array_size()), second);
    EXPECT_EQ(std::vector<int>(set[2], set[2] + set.array_size()), std::vector<int>(third.begin(), third.end()));
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
    EXPECT_EQ(set[0], nullptr);
    EXPECT_EQ(set.front(), nullptr);
    EXPECT_EQ(set.back(), nullptr);
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

}  // namespace ygg::tests
