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
#include <yggdrasil.hpp>

#include <array>
#include <cstdint>
#include <stdexcept>
#include <type_traits>

namespace ygg::tests {

struct AdaptersIndexTag;
}

namespace ygg {
template <>
struct Index<tests::AdaptersIndexTag>
    : IndexMixin<Index<tests::AdaptersIndexTag>> {
  using Base = IndexMixin<Index<tests::AdaptersIndexTag>>;
  using Base::Base;
};
} // namespace ygg

namespace ygg::tests {

struct AdaptersUint : ygg::FixedUintMixin<AdaptersUint> {
  using Base = ygg::FixedUintMixin<AdaptersUint>;
  using Base::Base;
};

TEST(YggdrasilTests, CommonAdaptersUmbrellaHeaderExposesPublicSurfaces) {
  static_assert(ygg::IndexConcept<ygg::Index<AdaptersIndexTag>>);
  static_assert(
      std::is_same_v<ygg::Index<AdaptersIndexTag>::value_type, ygg::uint_t>);
  static_assert(std::is_same_v<AdaptersUint::value_type, ygg::uint_t>);

  const auto default_index = ygg::Index<AdaptersIndexTag>();
  const auto max_index = ygg::Index<AdaptersIndexTag>::max();
  const auto index = ygg::Index<AdaptersIndexTag>(3);

  EXPECT_TRUE(default_index.is_max());
  EXPECT_TRUE(max_index.is_max());
  EXPECT_FALSE(index.is_max());
  EXPECT_EQ(index.get_value(), 3);

  const auto default_uint = AdaptersUint();
  const auto max_uint = AdaptersUint::max();
  const auto uint_value = AdaptersUint(4);

  EXPECT_TRUE(default_uint.is_max());
  EXPECT_TRUE(max_uint.is_max());
  EXPECT_FALSE(uint_value.is_max());
  EXPECT_EQ(uint_value.get_value(), 4);

  auto arena = ygg::buffer::SegmentedBuffer();
  const auto value = std::array<uint8_t, 1>{1};

  EXPECT_NE(arena.write(value.data(), value.size()), nullptr);
  EXPECT_EQ(arena.size(), 1);
}

TEST(YggdrasilTests, CommonExecutionContextExposesThreadLimit) {
  const auto max_num_threads = ygg::ExecutionContext::get_max_num_threads();

  EXPECT_GE(max_num_threads, 1);
  EXPECT_EQ(ygg::ExecutionContext(max_num_threads).get_num_threads(),
            max_num_threads);
}

TEST(YggdrasilTests, CommonExecutionContextRejectsInvalidThreadCounts) {
  const auto max_num_threads = ygg::ExecutionContext::get_max_num_threads();

  EXPECT_THROW(ygg::ExecutionContext(0), std::invalid_argument);
  EXPECT_THROW(ygg::ExecutionContext(max_num_threads + 1),
               std::invalid_argument);
}

} // namespace ygg::tests
