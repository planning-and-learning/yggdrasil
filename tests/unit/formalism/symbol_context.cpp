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

// A repository on SymbolContext: its records interned, found and read through its interface, over a parent layer,
// its members instantiated by the library macro as a library instantiates its repositories.

#include "yggdrasil/formalism/symbol_context_instantiations.hpp"

#include <boost/mp11.hpp>
#include <gtest/gtest.h>
#include <type_traits>
#include <urd/elements/copy.hpp>
#include <urd/elements/flat.hpp>
#include <urd/elements/view.hpp>

namespace
{
struct Name
{
};
}  // namespace

template<>
struct urd::data<Name>
{
    urd::index<Name> index;
    int value = 0;

    data() = default;

    explicit data(int value_) : index(), value(value_) {}

    static constexpr bool copyable() noexcept
    {
        static_assert(std::is_trivially_copyable_v<data>, "a copyable record is trivially copyable");
        return true;
    }

    static constexpr auto fields() noexcept { return urd::Fields<urd::field<"value", &data::value>> {}; }

    static constexpr auto identifying_members() noexcept { return urd::members_of(fields()); }

    static constexpr auto cista_members() noexcept { return urd::Members<&data::index, &data::value> {}; }
};

template<typename V>
struct urd::view_mixin<Name, V>
{
private:
    [[nodiscard]] const V& self() const noexcept { return static_cast<const V&>(*this); }

public:
    [[nodiscard]] decltype(auto) get_value() const { return self().template member<&urd::record_of<V>::value>(); }
};

namespace
{
class Names : public ygg::formalism::SymbolContext<Names, boost::mp11::mp_list<Name>>
{
    using Base = ygg::formalism::SymbolContext<Names, boost::mp11::mp_list<Name>>;

public:
    Names() = default;

    explicit Names(const Names* parent) : Base(parent) {}

    [[nodiscard]] std::size_t layer() const noexcept { return 0; }
};
}  // namespace

YGG_FORMALISM_SYMBOL_CONTEXT(, Names, 1)

namespace
{
TEST(SymbolContext, InternsFindsAndReadsItsRecords)
{
    auto names = Names();
    auto record = urd::data<Name> {};
    record.value = 7;
    const auto [view, created] = names.insert(record);
    EXPECT_TRUE(created);
    EXPECT_EQ(view.index(), record.index);
    EXPECT_EQ(names[view.index()].value, 7);
    EXPECT_TRUE(names.contains(view.index()));
    EXPECT_EQ(names.size<Name>(), 1);
    const auto found = names.find(record);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found.value().index(), view.index());
    EXPECT_FALSE(names.insert(record).second);  // interned once
}

TEST(SymbolContext, LayersOverItsParent)
{
    auto parent = Names();
    auto record = urd::data<Name> {};
    record.value = 1;
    const auto in_parent = parent.insert(record).first.index();
    auto child = Names(&parent);
    EXPECT_TRUE(child.find(record).has_value());  // the parent's record, found from the child
    record.value = 2;
    EXPECT_TRUE(child.insert(record).second);
    EXPECT_EQ(child.size<Name>(), 2);
    EXPECT_EQ(parent.size<Name>(), 1);
    EXPECT_EQ(child[in_parent].value, 1);
    child.clear();  // the child's records go, the parent's stay
    EXPECT_EQ(child.size<Name>(), 1);
    EXPECT_GT(parent.memory_usage<Name>(), 0);
}

static_assert(urd::Sees<Names>);

TEST(SymbolContext, SeesItselfAndTheLayersItLiesOver)
{
    auto parent = Names();
    auto child = Names(&parent);
    auto sibling = Names(&parent);
    EXPECT_EQ(child.parent(), &parent);
    EXPECT_EQ(parent.parent(), nullptr);
    EXPECT_TRUE(child.sees(child));
    EXPECT_TRUE(child.sees(parent));
    EXPECT_FALSE(parent.sees(child));
    EXPECT_FALSE(child.sees(sibling));
}
}  // namespace
