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

#include <fmt/format.h>
#include <gtest/gtest.h>
#include <string>
#include <tuple>
#include <vector>
#include <yggdrasil/ids/index_mixins.hpp>
#include <yggdrasil/semantics/canonicalization.hpp>

namespace ygg::tests
{
struct CanonicalizationTestTag;

struct CanonicalizationContext
{
    const std::vector<ygg::Data<CanonicalizationTestTag>>* data = nullptr;
    std::vector<std::string> labels;

    const ygg::Data<CanonicalizationTestTag>& operator[](ygg::Index<CanonicalizationTestTag> index) const;
};
}  // namespace ygg::tests

namespace ygg
{
template<>
struct Data<ygg::tests::CanonicalizationTestTag>
{
    ygg::uint_t value {};

    auto identifying_members() const noexcept { return std::tie(value); }
};

template<>
struct Index<ygg::tests::CanonicalizationTestTag> : IndexMixin<Index<ygg::tests::CanonicalizationTestTag>>
{
    using Base = IndexMixin<Index<ygg::tests::CanonicalizationTestTag>>;
    using Base::Base;
};
}  // namespace ygg

namespace fmt
{
template<>
struct formatter<ygg::View<ygg::Data<ygg::tests::CanonicalizationTestTag>, ygg::tests::CanonicalizationContext>, char>
{
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(const ygg::View<ygg::Data<ygg::tests::CanonicalizationTestTag>, ygg::tests::CanonicalizationContext>& value, FormatContext& ctx) const
    {
        return fmt::format_to(ctx.out(), "{}", value.get_context().labels.at(value.get_data().value));
    }
};
}  // namespace fmt

namespace ygg::tests
{
const ygg::Data<CanonicalizationTestTag>& CanonicalizationContext::operator[](ygg::Index<CanonicalizationTestTag> index) const
{
    return data->at(index.get_value());
}

namespace
{
using Tag = CanonicalizationTestTag;
}

TEST(YggdrasilTests, CommonCanonicalizeIndexListSortsAndDeduplicates)
{
    auto list = ygg::IndexList<Tag> {};
    list.push_back(ygg::Index<Tag>(2));
    list.push_back(ygg::Index<Tag>(1));
    list.push_back(ygg::Index<Tag>(2));
    list.push_back(ygg::Index<Tag>(3));

    EXPECT_FALSE(ygg::is_canonical(list));

    ygg::canonicalize(list);

    ASSERT_EQ(list.size(), 3);
    EXPECT_TRUE(ygg::is_canonical(list));
    EXPECT_EQ(list[0].get_value(), 1);
    EXPECT_EQ(list[1].get_value(), 2);
    EXPECT_EQ(list[2].get_value(), 3);
}

TEST(YggdrasilTests, CommonCanonicalizeDataListSortsAndDeduplicates)
{
    auto list = ygg::DataList<Tag> {};
    list.push_back(ygg::Data<Tag> { .value = 3 });
    list.push_back(ygg::Data<Tag> { .value = 1 });
    list.push_back(ygg::Data<Tag> { .value = 3 });
    list.push_back(ygg::Data<Tag> { .value = 2 });

    EXPECT_FALSE(ygg::is_canonical(list));

    ygg::canonicalize(list);

    ASSERT_EQ(list.size(), 3);
    EXPECT_TRUE(ygg::is_canonical(list));
    EXPECT_EQ(list[0].value, 1);
    EXPECT_EQ(list[1].value, 2);
    EXPECT_EQ(list[2].value, 3);
}

TEST(YggdrasilTests, CommonCanonicalizeContextAwareIndexListSortsAndDeduplicatesByFormattedData)
{
    const auto data = std::vector<ygg::Data<Tag>> { { .value = 2 }, { .value = 0 }, { .value = 0 } };
    const auto context = CanonicalizationContext { .data = &data, .labels = { "a", "b", "c" } };
    auto list = ygg::IndexList<Tag> {};
    list.push_back(ygg::Index<Tag>(0));
    list.push_back(ygg::Index<Tag>(1));
    list.push_back(ygg::Index<Tag>(2));

    EXPECT_FALSE(ygg::is_canonical(context, list));

    ygg::canonicalize(context, list);

    ASSERT_EQ(list.size(), 2);
    EXPECT_TRUE(ygg::is_canonical(context, list));
    EXPECT_EQ(list[0].get_value(), 1);
    EXPECT_EQ(list[1].get_value(), 0);
}

TEST(YggdrasilTests, CommonCanonicalizeContextAwareDataListSortsAndDeduplicatesByFormattedData)
{
    const auto context = CanonicalizationContext { .data = nullptr, .labels = { "a", "b", "c" } };
    auto list = ygg::DataList<Tag> {};
    list.push_back(ygg::Data<Tag> { .value = 2 });
    list.push_back(ygg::Data<Tag> { .value = 0 });
    list.push_back(ygg::Data<Tag> { .value = 2 });

    EXPECT_FALSE(ygg::is_canonical(context, list));

    ygg::canonicalize(context, list);

    ASSERT_EQ(list.size(), 2);
    EXPECT_TRUE(ygg::is_canonical(context, list));
    EXPECT_EQ(list[0].value, 0);
    EXPECT_EQ(list[1].value, 2);
}

TEST(YggdrasilTests, CommonCanonicalizeOptionalIsNoOp)
{
    auto value = ::cista::optional<int> { 4 };

    EXPECT_TRUE(ygg::is_canonical(value));
    ygg::canonicalize(value);

    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, 4);
}

}  // namespace ygg::tests
