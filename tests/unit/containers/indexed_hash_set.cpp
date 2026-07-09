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

#include <cstddef>
#include <gtest/gtest.h>
#include <stdexcept>
#include <yggdrasil/containers/indexed_hash_set.hpp>

namespace ygg::tests
{

struct IndexedHashSetTestTag
{
};

struct IndexedHashSetCustomHash
{
    ygg::hash_t operator()(const ygg::Data<IndexedHashSetTestTag>& value) const noexcept;
};

struct IndexedHashSetCustomEqualTo
{
    bool operator()(const ygg::Data<IndexedHashSetTestTag>& lhs, const ygg::Data<IndexedHashSetTestTag>& rhs) const noexcept;
};

}  // namespace ygg::tests

namespace ygg
{

template<>
struct Data<ygg::tests::IndexedHashSetTestTag>
{
    int value {};

    auto identifying_members() const noexcept { return std::tie(value); }
};

inline bool is_canonical(const Data<ygg::tests::IndexedHashSetTestTag>&) { return true; }

inline void canonicalize(Data<ygg::tests::IndexedHashSetTestTag>&) {}

template<>
struct Index<ygg::tests::IndexedHashSetTestTag> : IndexMixin<Index<ygg::tests::IndexedHashSetTestTag>>
{
    using Base = IndexMixin<Index<ygg::tests::IndexedHashSetTestTag>>;
    using Base::Base;
};

}  // namespace ygg

namespace ygg::tests
{

inline ygg::hash_t IndexedHashSetCustomHash::operator()(const ygg::Data<IndexedHashSetTestTag>& value) const noexcept
{
    return static_cast<ygg::hash_t>(value.value);
}

inline bool IndexedHashSetCustomEqualTo::operator()(const ygg::Data<IndexedHashSetTestTag>& lhs, const ygg::Data<IndexedHashSetTestTag>& rhs) const noexcept
{
    return lhs.value == rhs.value;
}

}  // namespace ygg::tests

namespace ygg
{

static_assert(ygg::HashFor<ygg::tests::IndexedHashSetCustomHash, ygg::Data<ygg::tests::IndexedHashSetTestTag>>);
static_assert(ygg::EqualToFor<ygg::tests::IndexedHashSetCustomEqualTo, ygg::Data<ygg::tests::IndexedHashSetTestTag>>);

}  // namespace ygg

namespace ygg::tests
{

TEST(YggdrasilTests, CommonIndexedHashSetSupportsHashAwareInsertion)
{
    using Set = ygg::IndexedHashSet<IndexedHashSetTestTag, IndexedHashSetCustomHash, IndexedHashSetCustomEqualTo>;
    auto set = Set();
    const auto first = ygg::Data<IndexedHashSetTestTag> { 1 };
    const auto second = ygg::Data<IndexedHashSetTestTag> { 2 };
    const auto first_hash = Set::hash(first);
    const auto second_hash = Set::hash(second);

    EXPECT_EQ(set.find_with_hash(first, first_hash), std::nullopt);

    const auto [first_index, first_inserted] = set.insert_with_hash(first_hash, first);
    EXPECT_TRUE(first_inserted);
    EXPECT_EQ(first_index, ygg::Index<IndexedHashSetTestTag>(0));
    EXPECT_TRUE(set.contains_with_hash(first, first_hash));
    EXPECT_EQ(set.find_with_hash(first, first_hash), first_index);

    const auto [duplicate_index, duplicate_inserted] = set.insert_with_hash(first_hash, first);
    EXPECT_FALSE(duplicate_inserted);
    EXPECT_EQ(duplicate_index, first_index);

    EXPECT_EQ(set.insert_new_with_hash(second_hash, second), ygg::Index<IndexedHashSetTestTag>(1));
    EXPECT_EQ(set.find_with_hash(second, second_hash), ygg::Index<IndexedHashSetTestTag>(1));
}

TEST(YggdrasilTests, CommonIndexedHashSetFindsAndContainsInsertedValues)
{
    auto set = ygg::IndexedHashSet<IndexedHashSetTestTag, IndexedHashSetCustomHash, IndexedHashSetCustomEqualTo>();
    const auto first = ygg::Data<IndexedHashSetTestTag> { 1 };
    const auto second = ygg::Data<IndexedHashSetTestTag> { 2 };
    const auto missing = ygg::Data<IndexedHashSetTestTag> { 3 };

    EXPECT_TRUE(set.empty());
    EXPECT_EQ(set.size(), 0);
    EXPECT_FALSE(set.contains(first));

    auto [first_index, inserted_first] = set.insert(first);
    EXPECT_TRUE(inserted_first);
    EXPECT_EQ(first_index.get_value(), ygg::uint_t { 0 });
    EXPECT_TRUE(set.contains(first));
    EXPECT_EQ(set.find(first), first_index);
    EXPECT_EQ(set[first_index].value, 1);
    EXPECT_EQ(set.at(first_index).value, 1);
    EXPECT_THROW(set.at(ygg::Index<IndexedHashSetTestTag>(1)), std::out_of_range);

    auto [second_index, inserted_second] = set.insert(second);
    EXPECT_TRUE(inserted_second);
    EXPECT_EQ(second_index.get_value(), ygg::uint_t { 1 });
    EXPECT_TRUE(set.contains(second));
    EXPECT_EQ(set.at(second_index).value, 2);
    EXPECT_THROW(set.at(ygg::Index<IndexedHashSetTestTag>(2)), std::out_of_range);

    auto [duplicate_index, inserted_duplicate] = set.insert(first);
    EXPECT_FALSE(inserted_duplicate);
    EXPECT_EQ(duplicate_index, first_index);
    EXPECT_EQ(set.size(), 2);
    EXPECT_FALSE(set.empty());
    EXPECT_FALSE(set.contains(missing));

    set.clear();
    EXPECT_TRUE(set.empty());
    EXPECT_EQ(set.size(), 0);
    EXPECT_FALSE(set.contains(first));
}

}  // namespace ygg::tests
