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

#include "yggdrasil/semantics/canonicalization.hpp"

#include "yggdrasil/core/config.hpp"
#include <urd/elements/flat.hpp>

#include <gtest/gtest.h>
#include <vector>

namespace ygg::tests
{
struct CanonicalizationTestTag;
using Tag = CanonicalizationTestTag;
}  // namespace ygg::tests

template<>
struct urd::data<ygg::tests::CanonicalizationTestTag>
{
    ygg::uint_t value {};

    data() = default;
    explicit data(ygg::uint_t value_) noexcept : value(value_) {}

    static constexpr auto cista_members() noexcept { return urd::Members<&data::value> {}; }
    static constexpr auto identifying_members() noexcept { return urd::Members<&data::value> {}; }
};

namespace ygg::tests
{

TEST(YggdrasilTests, CommonCanonicalizeIndexListSortsAndDeduplicates)
{
    auto list = urd::flat_vector<urd::index<Tag>> {};
    list.push_back(urd::index<Tag>(2));
    list.push_back(urd::index<Tag>(1));
    list.push_back(urd::index<Tag>(2));
    list.push_back(urd::index<Tag>(3));

    EXPECT_FALSE(ygg::is_canonical(list));
    EXPECT_FALSE(ygg::is_canonical<false>(list));

    ygg::canonicalize<false>(list);

    ASSERT_EQ(list.size(), 4);
    EXPECT_FALSE(ygg::is_canonical(list));
    EXPECT_TRUE(ygg::is_canonical<false>(list));
    EXPECT_EQ(list[0].value, 1);
    EXPECT_EQ(list[1].value, 2);
    EXPECT_EQ(list[2].value, 2);
    EXPECT_EQ(list[3].value, 3);

    ygg::canonicalize(list);

    ASSERT_EQ(list.size(), 3);
    EXPECT_TRUE(ygg::is_canonical(list));
    EXPECT_EQ(list[0].value, 1);
    EXPECT_EQ(list[1].value, 2);
    EXPECT_EQ(list[2].value, 3);
}

TEST(YggdrasilTests, CommonCanonicalizeRecordListSortsAndDeduplicates)
{
    auto list = urd::flat_vector<urd::data<Tag>> {};
    list.push_back(urd::data<Tag>(3));
    list.push_back(urd::data<Tag>(1));
    list.push_back(urd::data<Tag>(3));
    list.push_back(urd::data<Tag>(2));

    EXPECT_FALSE(ygg::is_canonical(list));
    EXPECT_FALSE(ygg::is_canonical<false>(list));

    ygg::canonicalize<false>(list);

    ASSERT_EQ(list.size(), 4);
    EXPECT_FALSE(ygg::is_canonical(list));
    EXPECT_TRUE(ygg::is_canonical<false>(list));
    EXPECT_EQ(list[0].value, 1);
    EXPECT_EQ(list[1].value, 2);
    EXPECT_EQ(list[2].value, 3);
    EXPECT_EQ(list[3].value, 3);

    ygg::canonicalize(list);

    ASSERT_EQ(list.size(), 3);
    EXPECT_TRUE(ygg::is_canonical(list));
    EXPECT_EQ(list[0].value, 1);
    EXPECT_EQ(list[1].value, 2);
    EXPECT_EQ(list[2].value, 3);
}

TEST(YggdrasilTests, CommonCanonicalizeOptionalIsNoOp)
{
    auto value = urd::flat_optional<int> { 4 };

    EXPECT_TRUE(ygg::is_canonical(value));
    ygg::canonicalize(value);

    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, 4);
}

}  // namespace ygg::tests
