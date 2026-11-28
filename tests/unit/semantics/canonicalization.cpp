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
#include "yggdrasil/formalism/declarations.hpp"

#include <gtest/gtest.h>
#include <type_traits>
#include <urd/elements/flat.hpp>
#include <urd/elements/view.hpp>
#include <urd/tags.hpp>
#include <utility>
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

    explicit data(ygg::uint_t value_) : value(value_) {}

    static constexpr bool copyable() noexcept
    {
        static_assert(std::is_trivially_copyable_v<data>, "a copyable record is trivially copyable");
        return true;
    }

    static constexpr auto fields() noexcept { return urd::Fields<urd::field<"value", &data::value>> {}; }

    static constexpr auto cista_members() noexcept { return urd::cista_members_of<data>(); }
};

namespace ygg::tests
{
struct CanonicalizationSetsTag;
}  // namespace ygg::tests

/// A record that holds one list as a set, one as a bag, and one in the order it was given.
template<>
struct urd::data<ygg::tests::CanonicalizationSetsTag>
{
    urd::flat_vector<ygg::uint_t> set;
    urd::flat_vector<ygg::uint_t> bag;
    urd::flat_vector<ygg::uint_t> order;

    data() = default;

    data(urd::flat_vector<ygg::uint_t> set_, urd::flat_vector<ygg::uint_t> bag_, urd::flat_vector<ygg::uint_t> order_) :
        set(std::move(set_)),
        bag(std::move(bag_)),
        order(std::move(order_))
    {
    }

    data(const data& other) = delete;
    data& operator=(const data& other) = delete;
    data(data&& other) = default;
    data& operator=(data&& other) = default;
    ~data() = default;

    static constexpr auto fields() noexcept
    {
        return urd::
            Fields<urd::field<"set", &data::set, urd::sorted, urd::unique>, urd::field<"bag", &data::bag, urd::sorted>, urd::field<"order", &data::order>> {};
    }

    static constexpr auto cista_members() noexcept { return urd::cista_members_of<data>(); }
};

template<typename V>
struct urd::view_mixin<ygg::tests::CanonicalizationTestTag, V>
{
private:
    [[nodiscard]] const V& self() const noexcept { return static_cast<const V&>(*this); }

public:
    [[nodiscard]] decltype(auto) value() const { return self().template member<&urd::record_of<V>::value>(); }
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

TEST(YggdrasilTests, ARecordIsCanonicalizedAsItsFieldsPromise)
{
    auto record = urd::data<CanonicalizationSetsTag> {};
    for (const auto value : { 3U, 1U, 3U, 2U })
    {
        record.set.push_back(value);
        record.bag.push_back(value);
        record.order.push_back(value);
    }
    EXPECT_FALSE(ygg::formalism::is_canonical(record));

    ygg::formalism::canonicalize(record);

    EXPECT_TRUE(ygg::formalism::is_canonical(record));
    EXPECT_EQ(std::vector<ygg::uint_t>(record.set.begin(), record.set.end()), (std::vector<ygg::uint_t> { 1, 2, 3 }));         // sorted, unique
    EXPECT_EQ(std::vector<ygg::uint_t>(record.bag.begin(), record.bag.end()), (std::vector<ygg::uint_t> { 1, 2, 3, 3 }));      // sorted
    EXPECT_EQ(std::vector<ygg::uint_t>(record.order.begin(), record.order.end()), (std::vector<ygg::uint_t> { 3, 1, 3, 2 }));  // as given
}

}  // namespace ygg::tests
