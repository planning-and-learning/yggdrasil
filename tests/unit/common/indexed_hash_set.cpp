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
#include <yggdrasil/containers/indexed_hash_set.hpp>

#include <cstddef>

namespace ygg::tests
{

struct IndexedHashSetTestTag
{
};

struct IndexedHashSetCustomHash
{
    std::size_t operator()(const ygg::Data<IndexedHashSetTestTag>& value) const noexcept;
};

struct IndexedHashSetCustomEqualTo
{
    bool operator()(const ygg::Data<IndexedHashSetTestTag>& lhs, const ygg::Data<IndexedHashSetTestTag>& rhs) const noexcept;
};

}

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

}

namespace ygg::tests
{

inline std::size_t IndexedHashSetCustomHash::operator()(const ygg::Data<IndexedHashSetTestTag>& value) const noexcept
{
    return static_cast<std::size_t>(value.value);
}

inline bool IndexedHashSetCustomEqualTo::operator()(const ygg::Data<IndexedHashSetTestTag>& lhs,
                                                    const ygg::Data<IndexedHashSetTestTag>& rhs) const noexcept
{
    return lhs.value == rhs.value;
}

}

namespace ygg
{

static_assert(ygg::HashFor<ygg::tests::IndexedHashSetCustomHash, ygg::Data<ygg::tests::IndexedHashSetTestTag>>);
static_assert(ygg::EqualToFor<ygg::tests::IndexedHashSetCustomEqualTo, ygg::Data<ygg::tests::IndexedHashSetTestTag>>);

}

namespace ygg::tests
{

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

    auto [second_index, inserted_second] = set.insert(second);
    EXPECT_TRUE(inserted_second);
    EXPECT_EQ(second_index.get_value(), ygg::uint_t { 1 });
    EXPECT_TRUE(set.contains(second));

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

}
