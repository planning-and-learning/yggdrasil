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
#include <yggdrasil/containers/raw_vector_set.hpp>

#include <array>
#include <stdexcept>
#include <vector>

namespace ygg::tests {

TEST(YggdrasilTests, CommonRawVectorSetDeduplicatesEmptyVectors) {
  auto set = ygg::RawVectorSet<uint8_t, int, 32>();
  const auto empty = std::vector<int>{};

  EXPECT_EQ(set.insert(empty), 0);
  EXPECT_EQ(set.insert(empty), 0);
  EXPECT_EQ(set.size(), 1);
  EXPECT_TRUE(set.contains(empty));
  EXPECT_EQ(set.find(empty), 0);
  EXPECT_TRUE(set[0].empty());
}

TEST(YggdrasilTests, CommonRawVectorSetEmptyAccessThrows) {
  auto set = ygg::RawVectorSet<uint8_t, int, 32>();
  const auto &const_set = set;

  EXPECT_THROW(set.front(), std::out_of_range);
  EXPECT_THROW(const_set.front(), std::out_of_range);
  EXPECT_THROW(set.back(), std::out_of_range);
  EXPECT_THROW(const_set.back(), std::out_of_range);
}

TEST(YggdrasilTests, CommonRawVectorSetStoresVariableLengthVectors) {
  auto set = ygg::RawVectorSet<uint8_t, int, 32>();

  const auto first = std::vector<int>{1, 2};
  const auto second = std::vector<int>{1, 2, 3};
  const auto third = std::array<int, 4>{1, 2, 3, 4};

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
  EXPECT_FALSE(set.contains(std::array<int, 1>{9}));
  EXPECT_EQ(set.find(first), 0);
  EXPECT_EQ(set.find(second), 1);
  EXPECT_EQ(set.find(third), 2);
  EXPECT_EQ(set.find(std::array<int, 1>{9}), std::nullopt);

  const auto first_view = set[0];
  const auto second_view = set[1];
  const auto third_view = set[2];
  EXPECT_TRUE(first_view.valid());
  EXPECT_TRUE(first_view);
  EXPECT_EQ(std::vector<int>(first_view.begin(), first_view.end()), first);
  EXPECT_EQ(first_view.front(), 1);
  EXPECT_TRUE(set.front().valid());
  EXPECT_EQ(set.front().front(), 1);
  EXPECT_EQ(first_view.back(), 2);
  EXPECT_TRUE(set.back().valid());
  EXPECT_EQ(set.back().back(), 4);
  EXPECT_EQ(std::vector<int>(second_view.begin(), second_view.end()), second);
  const auto checked_second_view = set.at(1);
  EXPECT_TRUE(checked_second_view.valid());
  EXPECT_EQ(
      std::vector<int>(checked_second_view.begin(), checked_second_view.end()),
      second);
  EXPECT_EQ(std::vector<int>(third_view.begin(), third_view.end()),
            std::vector<int>(third.begin(), third.end()));

  const auto &const_set = set;
  EXPECT_TRUE(const_set[0].valid());
  EXPECT_TRUE(const_set.at(1).valid());
  EXPECT_TRUE(const_set.front().valid());
  EXPECT_TRUE(const_set.back().valid());

  EXPECT_THROW(set.at(3), std::out_of_range);
  EXPECT_THROW(const_set.at(3), std::out_of_range);
}

TEST(YggdrasilTests, CommonRawVectorSetClearKeepsContainerReusable) {
  auto set = ygg::RawVectorSet<uint8_t, int, 32>();

  EXPECT_EQ(set.insert(std::vector<int>({1, 2})), 0);
  set.clear();
  EXPECT_TRUE(set.empty());

  const auto value = std::vector<int>{3, 4, 5};
  EXPECT_EQ(set.insert(value), 0);
  EXPECT_EQ(set.size(), 1);
  EXPECT_TRUE(set.contains(value));
  EXPECT_EQ(set.find(value), 0);
}

} // namespace ygg::tests
