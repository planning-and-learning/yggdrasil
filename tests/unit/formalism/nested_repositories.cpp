/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

// Nested repositories (Nests): three repositories, each over the next (Items over Tags over Names), a record's children
// resolved through the repository owning their kind at any depth, the nested repositories reached by type and by kind,
// records interned through a mutable nesting only, and a kind reached along two paths resolved through neither.

#include "yggdrasil/formalism/nested.hpp"
#include "yggdrasil/formalism/symbol_context_instantiations.hpp"

#include <boost/mp11.hpp>
#include <concepts>
#include <gtest/gtest.h>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <urd/elements/copy.hpp>
#include <urd/elements/flat.hpp>
#include <urd/elements/view.hpp>
#include <utility>

namespace
{
struct Name
{
};

struct Tag
{
};

struct Item
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

template<>
struct urd::data<Tag>
{
    urd::index<Tag> index;
    urd::index<Name> name;

    data() = default;

    explicit data(urd::index<Name> name_) : index(), name(name_) {}

    static constexpr bool copyable() noexcept
    {
        static_assert(std::is_trivially_copyable_v<data>, "a copyable record is trivially copyable");
        return true;
    }

    static constexpr auto fields() noexcept { return urd::Fields<urd::field<"name", &data::name>> {}; }

    static constexpr auto identifying_members() noexcept { return urd::members_of(fields()); }

    static constexpr auto cista_members() noexcept { return urd::Members<&data::index, &data::name> {}; }
};

template<>
struct urd::data<Item>
{
    urd::index<Item> index;
    urd::index<Tag> tag;

    data() = default;

    explicit data(urd::index<Tag> tag_) : index(), tag(tag_) {}

    static constexpr bool copyable() noexcept
    {
        static_assert(std::is_trivially_copyable_v<data>, "a copyable record is trivially copyable");
        return true;
    }

    static constexpr auto fields() noexcept { return urd::Fields<urd::field<"tag", &data::tag>> {}; }

    static constexpr auto identifying_members() noexcept { return urd::members_of(fields()); }

    static constexpr auto cista_members() noexcept { return urd::Members<&data::index, &data::tag> {}; }
};

template<typename V>
struct urd::view_mixin<Name, V>
{
private:
    [[nodiscard]] const V& self() const noexcept { return static_cast<const V&>(*this); }

public:
    [[nodiscard]] decltype(auto) get_value() const { return self().template member<&urd::record_of<V>::value>(); }
};

template<typename V>
struct urd::view_mixin<Tag, V>
{
private:
    [[nodiscard]] const V& self() const noexcept { return static_cast<const V&>(*this); }

public:
    [[nodiscard]] decltype(auto) get_name() const { return self().template member<&urd::record_of<V>::name>(); }
};

template<typename V>
struct urd::view_mixin<Item, V>
{
private:
    [[nodiscard]] const V& self() const noexcept { return static_cast<const V&>(*this); }

public:
    [[nodiscard]] decltype(auto) get_tag() const { return self().template member<&urd::record_of<V>::tag>(); }
};

namespace
{
/// Names: no nested repository.
class Names : public ygg::formalism::SymbolContext<Names, boost::mp11::mp_list<Name>>
{
public:
    [[nodiscard]] std::size_t layer() const noexcept { return 1; }
};

/// Tags, over names (read only).
class Tags : public ygg::formalism::SymbolContext<Tags, boost::mp11::mp_list<Tag>>, public ygg::formalism::Nests<Tags, ygg::formalism::Nested<const Names>>
{
    using Base = ygg::formalism::SymbolContext<Tags, boost::mp11::mp_list<Tag>>;
    using NestsBase = ygg::formalism::Nests<Tags, ygg::formalism::Nested<const Names>>;

public:
    explicit Tags(std::shared_ptr<const Names> names) : Base(), NestsBase(std::move(names)) {}

    [[nodiscard]] std::size_t layer() const noexcept { return 2; }

    using Base::find;
    using Base::insert;
    using Base::view;
    using NestsBase::find;
    using NestsBase::get;
    using NestsBase::insert;
    using NestsBase::repository_for;
    using NestsBase::view;
};

/// Items, over tags (interned into).
class Items : public ygg::formalism::SymbolContext<Items, boost::mp11::mp_list<Item>>, public ygg::formalism::Nests<Items, ygg::formalism::Nested<Tags>>
{
    using Base = ygg::formalism::SymbolContext<Items, boost::mp11::mp_list<Item>>;
    using NestsBase = ygg::formalism::Nests<Items, ygg::formalism::Nested<Tags>>;

public:
    explicit Items(std::shared_ptr<Tags> tags) : Base(), NestsBase(std::move(tags)) {}

    [[nodiscard]] std::size_t layer() const noexcept { return 3; }

    using Base::find;
    using Base::insert;
    using Base::view;
    using NestsBase::find;
    using NestsBase::get;
    using NestsBase::insert;
    using NestsBase::repository_for;
    using NestsBase::view;
};

/// Names reached along two paths: through tags and directly.
class Twice :
    public ygg::formalism::SymbolContext<Twice, boost::mp11::mp_list<Item>>,
    public ygg::formalism::Nests<Twice, ygg::formalism::Nested<const Tags, const Names>>
{
    using Base = ygg::formalism::SymbolContext<Twice, boost::mp11::mp_list<Item>>;
    using NestsBase = ygg::formalism::Nests<Twice, ygg::formalism::Nested<const Tags, const Names>>;

public:
    Twice(std::shared_ptr<const Tags> tags, std::shared_ptr<const Names> names) : Base(), NestsBase(std::move(tags), std::move(names)) {}

    [[nodiscard]] std::size_t layer() const noexcept { return 4; }

    using Base::view;
    using NestsBase::view;
};
}  // namespace

YGG_FORMALISM_SYMBOL_CONTEXT(, Names, 1)
YGG_FORMALISM_SYMBOL_CONTEXT(, Tags, 1)
YGG_FORMALISM_SYMBOL_CONTEXT(, Items, 1)
YGG_FORMALISM_SYMBOL_CONTEXT(, Twice, 1)

namespace
{
static_assert(ygg::formalism::reaches<Items, Name> && ygg::formalism::reaches<Items, Tag> && ygg::formalism::reaches<Items, Item>);
static_assert(!ygg::formalism::reaches<Names, Tag>);
static_assert(ygg::formalism::nests<Items, Names> && !ygg::formalism::nests<Names, Items>);

/// Whether repository R views an index of T, and interns a record of T.
template<typename R, typename T>
concept ViewsIndex = requires(const R& repository, urd::index<T> index) { repository.view(index); };
template<typename R, typename T>
concept InternsRecord = requires(R& repository, urd::data<T>& record) { repository.insert(record); };

// A kind reached along two paths resolves through neither; one reached along one path does.
static_assert(!ViewsIndex<Twice, Name>);
static_assert(ViewsIndex<Twice, Tag>);

// Into a nesting declared read only, nothing is interned.
static_assert(!InternsRecord<Tags, Name>);
static_assert(InternsRecord<Items, Tag>);

struct Fixture
{
    std::shared_ptr<Names> names = std::make_shared<Names>();
    std::shared_ptr<Tags> tags = std::make_shared<Tags>(names);
    Items items = Items(tags);
};

TEST(Nests, ResolvesEachKindThroughTheRepositoryOwningIt)
{
    auto fixture = Fixture {};
    auto name = urd::data<Name>(7);
    const auto name_index = fixture.names->insert(name).first.index();
    auto tag = urd::data<Tag>(name_index);
    const auto tag_index = fixture.items.insert(tag).first.index();  // through Items, into Tags
    auto item = urd::data<Item>(tag_index);
    const auto item_view = fixture.items.insert(item).first;

    const auto tag_view = item_view.get_tag();
    static_assert(std::same_as<std::remove_cvref_t<decltype(tag_view.context())>, Tags>);
    EXPECT_EQ(&tag_view.context(), fixture.tags.get());
    const auto name_view = tag_view.get_name();
    static_assert(std::same_as<std::remove_cvref_t<decltype(name_view.context())>, Names>);
    EXPECT_EQ(&name_view.context(), fixture.names.get());
    EXPECT_EQ(name_view.get_value(), 7);

    // An index of a kind two levels down, through Items.
    EXPECT_EQ(fixture.items.view(name_index).get_value(), 7);
    EXPECT_EQ(&fixture.items.view(name_index).context(), fixture.names.get());

    // A record held in place, of a kind held below: its view through its owner.
    const auto held = fixture.items.view(tag);
    static_assert(std::same_as<std::remove_cvref_t<decltype(held.context())>, Tags>);
    EXPECT_EQ(held.get_name().get_value(), 7);

    EXPECT_TRUE(fixture.items.find(tag).has_value());
}

TEST(Nests, ReachesItsNestedRepositoriesByTypeAndByKind)
{
    auto fixture = Fixture {};
    EXPECT_EQ(&fixture.items.get<Tags>(), fixture.tags.get());
    EXPECT_EQ(&fixture.tags->get<Names>(), fixture.names.get());
    static_assert(std::is_const_v<std::remove_reference_t<decltype(fixture.tags->get<Names>())>>);
    EXPECT_EQ(&fixture.items.repository_for<Tag>(), fixture.tags.get());
    EXPECT_EQ(&fixture.items.repository_for<Name>(), fixture.names.get());
    EXPECT_EQ(fixture.items.get_ptr<Tags>(), fixture.tags);
}

// Seeing is a query of layers alone. A repository may hold copies of another's records only where each repository it
// nests sees the other's (its records name theirs by plain values too): check_compatible throws otherwise.
static_assert(urd::ChecksCompatibility<Tags> && !urd::ChecksCompatibility<Names>);

TEST(Nests, ChecksASourceOverRepositoriesItDoesNotSee)
{
    const auto names = std::make_shared<Names>();
    const auto first = Tags(names);
    const auto second = Tags(names);
    EXPECT_TRUE(first.sees(first));
    EXPECT_FALSE(first.sees(second));
    EXPECT_NO_THROW(first.check_compatible(second));
    const auto elsewhere = Tags(std::make_shared<Names>());
    EXPECT_FALSE(first.sees(elsewhere));
    EXPECT_THROW(first.check_compatible(elsewhere), std::invalid_argument);
}

// A record of a kind a repository reaches through one it nests is not copied (urd::copy): its key where that one sees
// the record's repository, else std::invalid_argument.
TEST(Nests, CopiesNoRecordOfANestedKind)
{
    auto fixture = Fixture {};
    auto name = urd::data<Name>(3);
    auto tag = urd::data<Tag>(fixture.names->insert(name).first.index());
    const auto tag_view = fixture.tags->insert(tag).first;
    auto unused_storage = 0;
    static_assert(urd::Reaches<Items, Tag, Tags>);
    EXPECT_EQ(urd::copy(tag_view, fixture.items, unused_storage), tag_view.index());
    auto elsewhere = Tags(fixture.names);
    const auto other_view = elsewhere.insert(tag).first;
    EXPECT_THROW((void) urd::copy(other_view, fixture.items, unused_storage), std::invalid_argument);
}
}  // namespace
