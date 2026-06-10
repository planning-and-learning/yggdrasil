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
#include <yggdrasil/buffer/buffer.hpp>
#include <yggdrasil/ids/index_mixins.hpp>
#include <yggdrasil/semantics/canonicalization.hpp>

#include <optional>
#include <stdexcept>
#include <tuple>

namespace ygg::tests {
struct BufferIndexedHashSetTag;
}

namespace ygg {
template <>
struct Index<tests::BufferIndexedHashSetTag>
    : IndexMixin<Index<tests::BufferIndexedHashSetTag>> {
  using Base = IndexMixin<Index<tests::BufferIndexedHashSetTag>>;
  using Base::Base;
};

template <> struct Data<tests::BufferIndexedHashSetTag> {
  Index<tests::BufferIndexedHashSetTag> index;
  uint_t symbol = 0;
  uint_t arity = 0;

  auto identifying_members() const noexcept { return std::tie(symbol, arity); }
};

inline bool is_canonical(const Data<tests::BufferIndexedHashSetTag> &) {
  return true;
}

inline void canonicalize(Data<tests::BufferIndexedHashSetTag> &) {}
} // namespace ygg

namespace ygg::tests {

TEST(YggdrasilTests,
     BufferIndexedHashSetDefaultConstructionSupportsReadOnlyQueries) {
  auto repository = buffer::IndexedHashSet<BufferIndexedHashSetTag>();
  const auto element =
      Data<BufferIndexedHashSetTag>{Index<BufferIndexedHashSetTag>(0), 10, 2};

  EXPECT_TRUE(repository.empty());
  EXPECT_EQ(repository.size(), 0);
  EXPECT_FALSE(repository.contains(element));
  EXPECT_EQ(repository.find(element), std::nullopt);
  EXPECT_THROW(repository.front(), std::out_of_range);
  EXPECT_THROW(repository.insert(element), std::logic_error);
}

TEST(YggdrasilTests, BufferIndexedHashSetSupportsHashAwareInsertion) {
  using Set = buffer::IndexedHashSet<BufferIndexedHashSetTag>;
  auto arena = buffer::SegmentedBuffer();
  auto bytes = buffer::Buffer();
  auto repository = Set(bytes, arena);
  const auto first =
      Data<BufferIndexedHashSetTag>{Index<BufferIndexedHashSetTag>(0), 10, 2};
  const auto second =
      Data<BufferIndexedHashSetTag>{Index<BufferIndexedHashSetTag>(1), 11, 3};
  const auto first_hash = Set::hash(first);
  const auto second_hash = Set::hash(second);

  EXPECT_EQ(repository.find_with_hash(first, first_hash), std::nullopt);

  const auto [first_index, first_inserted] =
      repository.insert_with_hash(first_hash, first);
  EXPECT_TRUE(first_inserted);
  EXPECT_EQ(first_index, Index<BufferIndexedHashSetTag>(0));
  EXPECT_TRUE(repository.contains_with_hash(first, first_hash));
  EXPECT_EQ(repository.find_with_hash(first, first_hash), first_index);

  const auto [duplicate_index, duplicate_inserted] =
      repository.insert_with_hash(first_hash, first);
  EXPECT_FALSE(duplicate_inserted);
  EXPECT_EQ(duplicate_index, first_index);

  EXPECT_EQ(repository.insert_new_with_hash(second_hash, second),
            Index<BufferIndexedHashSetTag>(1));
  EXPECT_EQ(repository.find_with_hash(second, second_hash),
            Index<BufferIndexedHashSetTag>(1));
}

TEST(YggdrasilTests, BufferIndexedHashSetStoresSerializedCanonicalData) {
  auto arena = buffer::SegmentedBuffer();
  auto bytes = buffer::Buffer();
  auto repository =
      buffer::IndexedHashSet<BufferIndexedHashSetTag>(bytes, arena);
  auto builder = Data<BufferIndexedHashSetTag>();

  EXPECT_TRUE(repository.empty());
  EXPECT_EQ(repository.size(), 0);

  builder.index.value = 0;
  builder.symbol = 10;
  builder.arity = 2;

  canonicalize(builder);
  auto [first_index, first_inserted] = repository.insert(builder);
  const auto &first = repository[first_index];

  EXPECT_TRUE(first_inserted);
  EXPECT_FALSE(repository.empty());
  EXPECT_EQ(repository.size(), 1);
  EXPECT_EQ(first.index.value, 0);
  EXPECT_EQ(first.symbol, builder.symbol);
  EXPECT_EQ(first.arity, builder.arity);
  EXPECT_EQ(repository.at(first_index).symbol, builder.symbol);
  EXPECT_THROW(repository.at(Index<BufferIndexedHashSetTag>(1)),
               std::out_of_range);
  EXPECT_TRUE(repository.contains(builder));
  EXPECT_EQ(repository.find(builder), first_index);

  builder.index.value = 1;
  builder.symbol = 11;
  builder.arity = 3;

  canonicalize(builder);
  auto [second_index, second_inserted] = repository.insert(builder);
  const auto &second = repository[second_index];

  EXPECT_TRUE(second_inserted);
  EXPECT_EQ(repository.size(), 2);
  EXPECT_EQ(second.index.value, 1);
  EXPECT_EQ(second.symbol, builder.symbol);
  EXPECT_EQ(second.arity, builder.arity);
  EXPECT_EQ(repository.at(second_index).symbol, builder.symbol);
  EXPECT_THROW(repository.at(Index<BufferIndexedHashSetTag>(2)),
               std::out_of_range);

  builder.index.value = 1;
  builder.symbol = 11;
  builder.arity = 3;

  canonicalize(builder);
  auto [duplicate_index, duplicate_inserted] = repository.insert(builder);
  const auto &duplicate = repository[duplicate_index];

  EXPECT_FALSE(duplicate_inserted);
  EXPECT_EQ(duplicate_index, second_index);
  EXPECT_EQ(repository.size(), 2);
  EXPECT_EQ(duplicate.index.value, 1);
  EXPECT_EQ(duplicate.symbol, builder.symbol);
  EXPECT_EQ(duplicate.arity, builder.arity);
  EXPECT_TRUE(repository.contains(builder));
  EXPECT_EQ(repository.find(builder), second_index);
}

} // namespace ygg::tests
