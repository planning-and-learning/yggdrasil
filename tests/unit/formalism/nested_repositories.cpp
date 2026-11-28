/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

// Nested repositories (Nests): three repositories, each over the next (Items over Tags over Names), a record's children
// resolved through the repository owning their kind at any depth, the nested repositories reached by type and by kind,
// records interned through a mutable nesting only, and a kind reached along two paths resolved through neither.

#include "yggdrasil/formalism/nested.hpp"

#include <boost/mp11.hpp>
#include <concepts>
#include <cstdlib>
#include <gtest/gtest.h>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <urd/elements/copy.hpp>
#include <urd/elements/flat.hpp>
#include <urd/elements/view.hpp>
#include <utility>
#include <yggdrasil/formalism/symbol_context_impl.hpp>

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
    urd::key<Name> index;
    int value = 0;

    data() = default;

    explicit data(int value_) : index(), value(value_) {}

    static constexpr bool copyable() noexcept
    {
        static_assert(std::is_trivially_copyable_v<data>, "a copyable record is trivially copyable");
        return true;
    }

    static constexpr auto fields() noexcept { return urd::Fields<urd::field<"value", &data::value>> {}; }

    static constexpr auto cista_members() noexcept { return urd::cista_members_of<data>(); }

    [[nodiscard]] urd::key<Name> key() const noexcept { return urd::key<Name>(index); }
};

template<>
struct urd::data<Tag>
{
    urd::key<Tag> index;
    urd::index<Name> name;

    data() = default;

    explicit data(urd::index<Name> name_) : index(), name(name_) {}

    static constexpr bool copyable() noexcept
    {
        static_assert(std::is_trivially_copyable_v<data>, "a copyable record is trivially copyable");
        return true;
    }

    static constexpr auto fields() noexcept { return urd::Fields<urd::field<"name", &data::name>> {}; }

    static constexpr auto cista_members() noexcept { return urd::cista_members_of<data>(); }

    [[nodiscard]] urd::key<Tag> key() const noexcept { return urd::key<Tag>(index); }
};

template<>
struct urd::data<Item>
{
    urd::key<Item> index;
    urd::index<Tag> tag;

    data() = default;

    explicit data(urd::index<Tag> tag_) : index(), tag(tag_) {}

    static constexpr bool copyable() noexcept
    {
        static_assert(std::is_trivially_copyable_v<data>, "a copyable record is trivially copyable");
        return true;
    }

    static constexpr auto fields() noexcept { return urd::Fields<urd::field<"tag", &data::tag>> {}; }

    static constexpr auto cista_members() noexcept { return urd::cista_members_of<data>(); }

    [[nodiscard]] urd::key<Item> key() const noexcept { return urd::key<Item>(index); }
};

template<typename V>
struct urd::view_mixin<Name, V>
{
private:
    [[nodiscard]] const V& self() const noexcept { return static_cast<const V&>(*this); }

public:
    [[nodiscard]] decltype(auto) value() const { return self().template member<&urd::record_of<V>::value>(); }
};

template<typename V>
struct urd::view_mixin<Tag, V>
{
private:
    [[nodiscard]] const V& self() const noexcept { return static_cast<const V&>(*this); }

public:
    [[nodiscard]] decltype(auto) name() const { return self().template member<&urd::record_of<V>::name>(); }
};

template<typename V>
struct urd::view_mixin<Item, V>
{
private:
    [[nodiscard]] const V& self() const noexcept { return static_cast<const V&>(*this); }

public:
    [[nodiscard]] decltype(auto) tag() const { return self().template member<&urd::record_of<V>::tag>(); }
};

namespace
{
/// Names: no nested repository.
class Names : public ygg::formalism::SymbolContext<Names, boost::mp11::mp_list<Name>>
{
public:
    [[nodiscard]] std::size_t layer() const noexcept { return 1; }
};

/// The names' domain prepares a name: its value made non-negative (found by ADL from the owning repository's find and
/// insert).
void prepare_for_insert(const Names& /*repository*/, urd::data<Name>& data) { data.value = std::abs(data.value); }

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
    using NestsBase::get;
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
    using NestsBase::get;
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
};
}  // namespace

namespace
{
static_assert(ygg::formalism::resolves<Items, urd::index<Name>> && ygg::formalism::resolves<Items, urd::index<Tag>>
              && ygg::formalism::resolves<Items, urd::index<Item>>);
static_assert(!ygg::formalism::resolves<Names, urd::index<Tag>>);

/// Whether repository R views an index of T, and interns a record of T.
template<typename R, typename T>
concept ViewsIndex = requires(const R& repository, urd::index<T> index) { repository.view(index); };
template<typename R, typename T>
concept InternsRecord = requires(R& repository, urd::data<T>& record) { repository.insert(record); };

// A kind reached along two paths resolves through neither; one reached along one path does.
static_assert(!ViewsIndex<Twice, Name>);
static_assert(ViewsIndex<Twice, Tag>);

/// Whether repository R reads the record of a key K (R[key]): through the one part, its own or a nested repository,
/// that resolves it.
template<typename R, typename K>
concept ReadsKey = requires(const R& repository, K key) { repository[key]; };

static_assert(ReadsKey<Items, urd::index<Item>> && ReadsKey<Items, urd::index<Tag>> && ReadsKey<Items, urd::index<Name>>);
static_assert(!ReadsKey<Names, urd::index<Tag>>);
static_assert(!ReadsKey<Twice, urd::index<Name>>);  // resolved along two paths: by neither
static_assert(ReadsKey<Twice, urd::index<Tag>>);

// One operator[]: an own key and a nested key (two levels down) resolve through the same member, KeyedRepository's.
static_assert(std::same_as<decltype(&Items::operator[]<Item>), urd::stored_ref<Item> (ygg::formalism::KeyedRepository<Items>::*)(urd::key<Item>) const>);
static_assert(std::same_as<decltype(&Items::operator[]<Name>), urd::stored_ref<Name> (ygg::formalism::KeyedRepository<Items>::*)(urd::key<Name>) const>);

/// A repository that names no keys writes no record.
struct Unkeyed
{
};

static_assert(!ygg::formalism::WritesKey<Unkeyed&, urd::index<Item>>);
static_assert(ygg::formalism::WritesKey<Items&, urd::index<Item>> && ygg::formalism::WritesKey<Items&, urd::index<Tag>>);

// Into a nesting declared read only, nothing is interned.
static_assert(!InternsRecord<Tags, Name>);
static_assert(InternsRecord<Items, Tag>);

// Writing goes through the same part as reading: a Tag through Items into Tags (nested non-const), its view through Tags,
// the repository owning it; a Name, held only by Names (nested const in Tags), has no insert through Items or Tags.
static_assert(std::same_as<decltype(std::declval<Items&>().insert(std::declval<urd::data<Tag>&>())), std::pair<urd::index_view<Tag, Tags>, bool>>);
static_assert(!InternsRecord<Items, Name>);
static_assert(std::same_as<decltype(std::declval<const Items&>().find(std::declval<urd::data<Name>&>())), std::optional<urd::index_view<Name, Names>>>);

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

    const auto tag_view = item_view.tag();
    static_assert(std::same_as<std::remove_cvref_t<decltype(tag_view.context())>, Tags>);
    EXPECT_EQ(&tag_view.context(), fixture.tags.get());
    const auto name_view = tag_view.name();
    static_assert(std::same_as<std::remove_cvref_t<decltype(name_view.context())>, Names>);
    EXPECT_EQ(&name_view.context(), fixture.names.get());
    EXPECT_EQ(name_view.value(), 7);

    // A record of a kind two levels down found through Items (read only there), as its view in the repository owning it.
    auto name_again = urd::data<Name>(7);
    const auto found_name = fixture.items.find(name_again);
    ASSERT_TRUE(found_name.has_value());
    EXPECT_EQ(found_name.value().index(), name_index);
    EXPECT_EQ(&found_name.value().context(), fixture.names.get());

    // A key of a kind two levels down, through Items: the record in place in the repository owning it.
    EXPECT_EQ(&*fixture.items[name_index], &*(*fixture.names)[name_index]);
    EXPECT_EQ(fixture.items[tag_index]->name, name_index);
    EXPECT_EQ(fixture.items[item_view.index()]->tag, tag_index);

    // An index of a kind two levels down, through Items.
    EXPECT_EQ(fixture.items.view(name_index).value(), 7);
    EXPECT_EQ(&fixture.items.view(name_index).context(), fixture.names.get());

    // A record held in place, of a kind held below: its view through its owner.
    const auto held = fixture.items.view(tag);
    static_assert(std::same_as<std::remove_cvref_t<decltype(held.context())>, Tags>);
    EXPECT_EQ(held.name().value(), 7);

    EXPECT_TRUE(fixture.items.find(tag).has_value());
}

TEST(Nests, FindPreparesARecordInTheRepositoryOwningIt)
{
    auto fixture = Fixture {};
    auto name = urd::data<Name>(-7);
    const auto inserted = fixture.names->insert(name).first;
    EXPECT_EQ(inserted.value(), 7);

    // Found through Items, two levels down and nested read only: prepared by the names' domain as its insert is.
    auto unprepared = urd::data<Name>(-7);
    const auto found = fixture.items.find(unprepared);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found.value().index(), inserted.index());
    EXPECT_EQ(unprepared.value, 7);
}

TEST(Nests, ReachesItsNestedRepositoriesByTypeAndByKind)
{
    auto fixture = Fixture {};
    EXPECT_EQ(&fixture.items.get<Tags>(), fixture.tags.get());
    EXPECT_EQ(&fixture.tags->get<Names>(), fixture.names.get());
    static_assert(std::is_const_v<std::remove_reference_t<decltype(fixture.tags->get<Names>())>>);
    EXPECT_EQ(&fixture.items.resolving<urd::index<Tag>>(), fixture.tags.get());
    EXPECT_EQ(&fixture.items.resolving<urd::index<Name>>(), fixture.names.get());
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
    static_assert(urd::ResolvesThrough<Items, urd::index<Tag>, Tags>);
    EXPECT_EQ(urd::copy(tag_view, fixture.items, unused_storage), tag_view.index());
    auto elsewhere = Tags(fixture.names);
    const auto other_view = elsewhere.insert(tag).first;
    EXPECT_THROW((void) urd::copy(other_view, fixture.items, unused_storage), std::invalid_argument);
}
}  // namespace
