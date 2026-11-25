/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

// The generic key lookup: repository[urd::key<Ts...>] is the record of the key's last tag, resolved by the one part of
// the repository that resolves the key (chosen at compile time), which folds over the key's indices through its nested
// containers (urd::record_at: a family of stores first selects the store of the key's kind). An index (N = 1) is its
// pool's own lookup; a key of two parts (N = 2) is shown with a test-only repository (a KeyedRepository too, its part
// a two-part store) of members numbered in their group's own pool of records, over the parent layer's.

#include "yggdrasil/formalism/builder_impl.hpp"
#include "yggdrasil/formalism/copy.hpp"
#include "yggdrasil/formalism/free_relations.hpp"
#include "yggdrasil/formalism/repository_impl.hpp"

#include <boost/mp11.hpp>
#include <concepts>
#include <cstdint>
#include <gtest/gtest.h>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <urd/elements/flat.hpp>
#include <urd/elements/keyed.hpp>
#include <urd/repr/maps/index_map.hpp>
#include <urd/repr/wrappers/layer.hpp>
#include <urd/store.hpp>
#include <vector>

namespace
{
/// The Schema of these column types, inserted into `repository` as any record.
template<std::ranges::input_range Types>
urd::index<ygg::Schema> insert_schema(ygg::FreeRelationRepository& repository, const Types& types)
{
    auto schema = urd::data<ygg::Schema>();
    schema.column_types.set(std::ranges::begin(types), std::ranges::end(types));
    return repository.insert(schema).first.index();
}

struct Group
{
};

struct Member
{
};
}  // namespace

/// A member is numbered in its group's own container: its key is its group, then its number there.
template<>
struct urd::key_of<Member>
{
    using type = urd::key<Group, Member>;
};

/// A member of a group: its number in its group's container (the local part of its key), its group and its weight.
template<>
struct urd::data<Member>
{
    urd::key<Member> index;
    urd::index<Group> group;
    int weight = 0;

    data() = default;

    data(urd::index<Group> group_, int weight_) : index(), group(group_), weight(weight_) {}

    static constexpr bool copyable() noexcept
    {
        static_assert(std::is_trivially_copyable_v<data>, "a copyable record is trivially copyable");
        return true;
    }

    static constexpr auto fields() noexcept { return urd::Fields<urd::field<"group", &data::group>, urd::field<"weight", &data::weight>> {}; }

    static constexpr auto identifying_members() noexcept { return urd::members_of(fields()); }

    static constexpr auto cista_members() noexcept { return urd::Members<&data::index, &data::group, &data::weight> {}; }

    /// Its whole key: its group, then its number there.
    [[nodiscard]] urd::key<Group, Member> key() const noexcept { return urd::key<Group, Member>(group, index); }
};

template<typename V>
struct urd::view_mixin<Member, V>
{
private:
    [[nodiscard]] const V& self() const noexcept { return static_cast<const V&>(*this); }

public:
    [[nodiscard]] decltype(auto) get_group() const { return self().template member<&urd::record_of<V>::group>(); }

    [[nodiscard]] decltype(auto) get_weight() const { return self().template member<&urd::record_of<V>::weight>(); }
};

namespace
{
using ygg::formalism::HoldsKey;

// N = 1: the free relation repository resolves its symbols' indices through its symbol repository, a Schema's through
// its relation repository; nothing else.
using Free = ygg::FreeRelationRepository;
static_assert(HoldsKey<Free, urd::index<ygg::Columns>> && HoldsKey<Free, urd::index<ygg::Schema>>);
static_assert(!HoldsKey<Free, urd::index<Member>> && !HoldsKey<Free, urd::key<Group, Member>>);
using FreeParts = ygg::formalism::key_parts<Free, false>;
// Its stores are families: a key first selects the store of its kind (urd::SelectsStoreOf), then the ordinary fold.
static_assert(urd::SelectsStoreOf<Free::Impl::SymbolStore, ygg::Columns> && urd::SelectsStoreOf<Free::Impl::RelationStore, ygg::Schema>);
static_assert(ygg::formalism::part_holding<FreeParts, urd::index<ygg::Columns>, false>() == 0);  // its symbol repository
static_assert(ygg::formalism::part_holding<FreeParts, urd::index<ygg::Schema>, false>() == 1);   // its relation repository
static_assert(!ygg::formalism::OnePartHolds<FreeParts, urd::index<Member>, false>);
// One operator[] (and one at()): a symbol's index and a Schema's resolve through the same member, KeyedRepository's.
static_assert(std::same_as<decltype(&Free::operator[]<ygg::Columns>),
                           urd::stored_ref<ygg::Columns> (ygg::formalism::KeyedRepository<Free>::*)(urd::key<ygg::Columns>) const>);
static_assert(std::same_as<decltype(&Free::operator[]<ygg::Schema>),
                           urd::stored_ref<ygg::Schema> (ygg::formalism::KeyedRepository<Free>::*)(urd::key<ygg::Schema>) const>);
static_assert(
    std::same_as<decltype(&Free::at<ygg::Schema>), urd::stored_ref<ygg::Schema> (ygg::formalism::KeyedRepository<Free>::*)(urd::key<ygg::Schema>) const>);
static_assert(std::same_as<decltype(std::declval<const Free&>()[urd::index<ygg::Schema>()]), urd::stored_ref<ygg::Schema>>);

TEST(KeyLookup, AnIndexIsItsSubRepositorysOwnLookup)
{
    auto repository = Free();
    auto columns = urd::data<ygg::Columns>();
    const auto symbol = repository.insert(columns).first.index();  // a symbol: the symbol repository
    const auto types = std::vector<ygg::TypeId> { ygg::type_id<std::uint32_t>, ygg::type_id<bool> };
    const auto schema = insert_schema(repository, types);  // a Schema: the relation repository

    EXPECT_EQ(repository[symbol]->index, symbol);
    EXPECT_EQ(&*repository[symbol], &*repository[symbol]);     // the record in place, not a copy
    EXPECT_EQ(&*repository.at(symbol), &*repository[symbol]);  // checked and unchecked: the same record
    EXPECT_EQ(repository[schema]->column_types.size(), 2U);
    EXPECT_EQ(repository[schema]->column_types[1], ygg::type_id<bool>);
    EXPECT_THROW((void) repository.at(urd::index<ygg::Columns>(7)), std::out_of_range);
    EXPECT_THROW((void) repository.at(urd::index<ygg::Schema>(7)), std::out_of_range);

    // Over a parent: the parent's records, through the child.
    const auto child = Free(&repository);
    EXPECT_EQ(&*child[symbol], &*repository[symbol]);
    EXPECT_EQ(&*child[schema], &*repository[schema]);
}

/// A group's members: a numbered pool of records (urd::layer, no default constructor), layered.
using Members = urd::Store<urd::pool<urd::data<Member>, urd::index<Member>>, urd::flat, urd::layered>;
static_assert(!std::default_initializable<Members>);

/// Members by group, over a parent's: a group's index selects its members (a layered dense map: a group this layer
/// never wrote is read from the parent; one it writes to has its own pool, built in place over the parent's, numbering
/// on after it), a member's index numbers it there.
class Groups : public ygg::formalism::KeyedRepository<Groups>
{
    using ByGroup = urd::Store<urd::map<urd::index<Group>, Members>, urd::dense, urd::layered>;

    ByGroup m_groups;

    friend class ygg::formalism::KeyedRepository<Groups>;

public:
    using KeyTypes = boost::mp11::mp_list<urd::key<Group, Member>>;
    /// Its one part: the members by group, held in place (an ordinary part, as a repository's stores are), as read and
    /// as written into.
    using PartStores = boost::mp11::mp_list<ByGroup>;
    using PartKeys = boost::mp11::mp_list<KeyTypes>;
    template<bool Writable>
    using OwnParts = std::tuple<ygg::formalism::HeldPart<ygg::formalism::detail::access_t<Writable, Groups>, 0>>;

    explicit Groups(const Groups* parent = nullptr)
    {
        if (parent != nullptr)
            m_groups.layer_over(&parent->m_groups);
    }

    /// A member of `group`, numbered in this layer's own container of the group.
    urd::key<Group, Member> insert(urd::index<Group> group, int weight)
    {
        m_groups.try_emplace(group);
        auto member = urd::data<Member>(group, weight);
        urd::insert_at(*m_groups.own_value(group), urd::key<>(), member);  // writes its number into it
        return member.key();
    }

private:
    [[nodiscard]] OwnParts<false> own_parts() const { return OwnParts<false>(std::tuple_element_t<0, OwnParts<false>>(*this, m_groups)); }

    [[nodiscard]] OwnParts<true> own_parts() { return OwnParts<true>(std::tuple_element_t<0, OwnParts<true>>(*this, m_groups)); }
};

static_assert(HoldsKey<Groups, urd::key<Group, Member>> && !HoldsKey<Groups, urd::index<Member>>);
static_assert(std::same_as<decltype(std::declval<const Groups&>()[urd::key<Group, Member>()]), urd::stored_ref<Member>>);
// One lookup path for N = 1 and N = 2: KeyedRepository's operator[] over a HeldPart, folded by urd::record_at.
static_assert(std::same_as<decltype(&Groups::operator[]<Group, Member>),
                           urd::stored_ref<Member> (ygg::formalism::KeyedRepository<Groups>::*)(urd::key<Group, Member>) const>);

TEST(KeyLookup, AKeyOfTwoPartsFoldsThroughItsNestedContainers)
{
    auto groups = std::make_shared<Groups>();
    const auto first = groups->insert(urd::index<Group>(3), 5);
    const auto second = groups->insert(urd::index<Group>(3), 8);
    const auto other = groups->insert(urd::index<Group>(0), 13);
    EXPECT_EQ(urd::get<Member>(second), urd::index<Member>(1));  // numbered densely in its group
    EXPECT_EQ(urd::get<Member>(other), urd::index<Member>(0));

    EXPECT_EQ((*groups)[first]->weight, 5);
    EXPECT_EQ((*groups)[second]->weight, 8);
    EXPECT_EQ((*groups)[other]->weight, 13);
    EXPECT_EQ(&*groups->at(second), &*(*groups)[second]);                                       // checked and unchecked: the same record
    const auto missing = urd::key<Group, Member>(urd::index<Group>(1), urd::index<Member>(0));  // no group 1
    EXPECT_THROW((void) groups->at(missing), std::out_of_range);
    EXPECT_THROW((void) groups->at(urd::key<Group, Member>(urd::index<Group>(3), urd::index<Member>(2))), std::out_of_range);

    // Over a parent: a group the child never wrote is the parent's; one it writes to numbers on after the parent's.
    auto child = Groups(groups.get());
    EXPECT_EQ(&*child[second], &*(*groups)[second]);
    EXPECT_EQ(child.insert(urd::index<Group>(3), 8), second);  // the parent's member, found through the chain
    const auto third = child.insert(urd::index<Group>(3), 21);
    EXPECT_EQ(urd::get<Member>(third), urd::index<Member>(2));
    EXPECT_EQ(child[third]->weight, 21);
    EXPECT_EQ(&*child[first], &*(*groups)[first]);  // the parent's records, in place, through the child's own pool
    EXPECT_EQ(child.at(third)->weight, 21);
    EXPECT_THROW((void) groups->at(third), std::out_of_range);  // the parent knows nothing of it
}

/// N = 2, a row: a key of its relation and its id in the relation's pool. Read by the same operator[] and at() as any
/// key (its stored form handed out by value: PackedRow, the row packed in its slot); interned from a builder (its
/// relation the key's known part, its values the content: insert(known, content), urd::insert_at), found likewise.
using FreeRowKey = urd::key<ygg::Schema, ygg::Row<ygg::Schema>>;
using FreeRowBuilder = urd::builder<ygg::Row<ygg::Schema>, Free>;
static_assert(std::same_as<urd::stored_ref<ygg::Row<ygg::Schema>>, ygg::PackedRow<ygg::Schema>>);
static_assert(std::ranges::view<ygg::PackedRow<ygg::Schema>> && std::ranges::borrowed_range<ygg::PackedRow<ygg::Schema>>);  // a handle into its slot
static_assert(std::same_as<decltype(std::declval<const Free&>()[FreeRowKey()]), ygg::PackedRow<ygg::Schema>>);
static_assert(std::same_as<decltype(&Free::operator[]<ygg::Schema, ygg::Row<ygg::Schema>>),
                           ygg::PackedRow<ygg::Schema> (ygg::formalism::KeyedRepository<Free>::*)(FreeRowKey) const>);
static_assert(ygg::formalism::part_holding<FreeParts, FreeRowKey, false>() == 1);  // its relation repository

TEST(KeyLookup, ARowIsInternedFromItsBuilderAndReadByItsKey)
{
    auto storage = ygg::FreeRelationStorage();
    auto repository = Free();
    const auto types = std::vector<ygg::TypeId> { ygg::type_id<std::uint32_t>, ygg::type_id<bool> };
    const auto pool = insert_schema(repository, types);

    auto built = FreeRowBuilder();
    built.relation = pool;
    built.values = { ygg::encode(std::uint64_t { 7 }, ygg::type_id<std::uint32_t>), ygg::encode(true, ygg::type_id<bool>) };
    EXPECT_FALSE(repository.find(built, storage).has_value());
    const auto [stored, created] = repository.insert(built, storage);
    EXPECT_TRUE(created);
    EXPECT_EQ(repository.insert(built, storage), std::pair(stored, false));  // deduplicated in its slot
    const auto found = repository.find(built, storage);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found.value(), stored);

    // The stored form, by its key: its values packed in its slot.
    const auto row = repository[stored.index()];
    ASSERT_EQ(row.size(), 2U);
    EXPECT_EQ(row[0], ygg::encode(std::uint64_t { 7 }, ygg::type_id<std::uint32_t>));
    EXPECT_TRUE(std::ranges::equal(repository.at(stored.index()), row));
    EXPECT_THROW((void) repository.at(FreeRowKey(pool, urd::index<ygg::Row<ygg::Schema>>(1))), std::out_of_range);
    EXPECT_THROW((void) repository.at(FreeRowKey(urd::index<ygg::Schema>(9), urd::index<ygg::Row<ygg::Schema>>(0))), std::out_of_range);

    // A view of each form: stored and owned read typed (their interned relation's column types), built untyped only.
    EXPECT_EQ(stored.get<std::uint32_t>(0), 7U);
    EXPECT_TRUE(stored.get<bool>(1));
    EXPECT_EQ(stored.get_relation().index(), pool);
    EXPECT_EQ(repository.view(built).value(0), row[0]);
    auto content = urd::data<ygg::Row<ygg::Schema>>(pool, urd::flat_vector<ygg::Value>(row.begin(), row.end()));
    EXPECT_EQ(urd::resolve(content, repository).value(0), row[0]);
    EXPECT_EQ(urd::resolve(content, repository).get<std::uint32_t>(0), 7U);  // an owned row reads typed through its relation
    EXPECT_EQ(repository.find(content), stored);
    // Its content identifies it: hashed alike owned and packed, its relation selecting the slot, not compared.
    EXPECT_EQ(urd::Hash<urd::data<ygg::Row<ygg::Schema>>> {}(content), urd::Hash<ygg::PackedRow<ygg::Schema>> {}(row));
    EXPECT_EQ(content, urd::data<ygg::Row<ygg::Schema>>(urd::index<ygg::Schema>(9), urd::flat_vector<ygg::Value>(row.begin(), row.end())));

    // A held row (a record not interned yet, as a child holds one) is interned and found like a builder: under its relation.
    auto held = urd::data<ygg::Row<ygg::Schema>>(pool, urd::flat_vector<ygg::Value>(row.begin(), row.end()));
    EXPECT_EQ(ygg::formalism::detail::insert_child(repository, storage, held), std::pair(stored, false));
    held.values[1] = ygg::encode(false, ygg::type_id<bool>);
    EXPECT_FALSE(ygg::formalism::detail::find_child(repository, storage, held).has_value());
    const auto [held_view, held_created] = ygg::formalism::detail::insert_child(repository, storage, held);
    EXPECT_TRUE(held_created);
    EXPECT_FALSE(held_view.get<bool>(1));

    // An owned row is copied as any row form: its relation copied, then its values stored under it there.
    auto target = Free();
    const auto copied = urd::copy(urd::resolve(content, repository), target, storage);
    EXPECT_TRUE(std::ranges::equal(target[copied], row));

    // Over a parent: its rows found through the child, the child's own numbered after them in its own slot.
    auto child = Free(&repository);
    const auto found_in_child = child.find(content);
    ASSERT_TRUE(found_in_child.has_value());
    EXPECT_EQ(found_in_child.value().index(), stored.index());
    EXPECT_EQ(child.insert(built, storage).first.index(), stored.index());
    EXPECT_EQ(&child.insert(built, storage).first.context(), &repository);  // the parent's row, through the layer owning it
    built.values[0] = ygg::encode(std::uint64_t { 8 }, ygg::type_id<std::uint32_t>);
    const auto [own, own_created] = child.insert(built, storage);
    EXPECT_TRUE(own_created);
    EXPECT_EQ(urd::get<ygg::Row<ygg::Schema>>(own.index()), urd::index<ygg::Row<ygg::Schema>>(2));  // after the parent's two
    EXPECT_EQ(child[own.index()][0], ygg::encode(std::uint64_t { 8 }, ygg::type_id<std::uint32_t>));
    EXPECT_EQ(child[stored.index()][0], row[0]);
    EXPECT_FALSE(repository.find(built, storage).has_value());  // the parent knows nothing of it
}
}  // namespace

/// A layer that has made its own pool of a relation still finds the rows the layer below holds in its pool of it.
TEST(KeyLookup, ALayersOwnPoolFindsTheRowsOfThePoolBelow)
{
    auto parent = ygg::FreeRelationRepository();
    const auto relation = insert_schema(parent, std::array { ygg::type_id<std::uint32_t> });
    auto below = urd::data<ygg::Row<ygg::Schema>>(relation, urd::flat_vector<ygg::Value> { 1 });
    const auto [below_view, below_inserted] = parent.insert(below);
    ASSERT_TRUE(below_inserted);

    auto child = ygg::FreeRelationRepository(&parent);
    auto own = urd::data<ygg::Row<ygg::Schema>>(relation, urd::flat_vector<ygg::Value> { 2 });
    const auto [own_view, own_inserted] = child.insert(own);
    ASSERT_TRUE(own_inserted);

    auto again = urd::data<ygg::Row<ygg::Schema>>(relation, urd::flat_vector<ygg::Value> { 1 });
    const auto found_again = child.find(again);
    ASSERT_TRUE(found_again.has_value());
    EXPECT_EQ(found_again.value().index(), below_view.index());
    const auto [reinserted, reinserted_created] = child.insert(again);
    EXPECT_FALSE(reinserted_created);
    EXPECT_EQ(reinserted.index(), below_view.index());
    EXPECT_NE(own_view.index(), below_view.index());

    // A field of a stored row read through its packed form, by value (what Python's get_values reads).
    static_assert(std::same_as<decltype(below_view.template member<&urd::data<ygg::Row<ygg::Schema>>::values>()), ygg::PackedRow<ygg::Schema>>);
    EXPECT_EQ(below_view.template member<&urd::data<ygg::Row<ygg::Schema>>::values>()[0], 1U);
}
