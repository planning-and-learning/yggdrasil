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

#include "symbol_concepts.hpp"
#include "yggdrasil/core/config.hpp"
#include "yggdrasil/formalism/builder_impl.hpp"
#include "yggdrasil/formalism/canonicalization.hpp"
#include "yggdrasil/formalism/formatter.hpp"
#include "yggdrasil/formalism/free_relations.hpp"
#include "yggdrasil/formalism/interning.hpp"
#include "yggdrasil/formalism/membership.hpp"
#include "yggdrasil/formalism/relation_builder.hpp"
#include "yggdrasil/formalism/relation_repository.hpp"
#include "yggdrasil/formalism/repository_impl.hpp"
#include "yggdrasil/formalism/row_data.hpp"
#include "yggdrasil/formalism/row_view.hpp"
#include "yggdrasil/formalism/scratch_relation.hpp"
#include "yggdrasil/formalism/symbol_repository.hpp"
#include "yggdrasil/semantics/canonicalization.hpp"

#include "gtest/gtest.h"
#include <algorithm>
#include <array>
#include <boost/mp11.hpp>
#include <urd/repr/slots/slots.hpp>
#include <concepts>
#include <cstdint>
#include <forward_list>
#include <iterator>
#include <limits>
#include <list>
#include <memory>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <urd/config.hpp>
#include <urd/elements/equal_to.hpp>
#include <urd/elements/flat.hpp>
#include <urd/elements/hash.hpp>
#include <urd/elements/view.hpp>
#include <urd/repr/sequences/sequence.hpp>
#include <urd/store.hpp>
#include <vector>

namespace ygg::tests
{

/// Whether the context makes views of the handle.
template<typename H, typename C>
concept Viewable = requires(const C& context, H handle) { context.view(handle); };

struct RepositoryTypesSerializedElement;
struct RepositoryTypesBuilderOther;
/// The context the storage tests' builders name (nothing reads through it).
struct BuilderContext;

namespace
{
struct RepositoryTypesElement
{
};

struct RepositoryTypesRelation
{
};

/// A family whose rows are stored bit-packed (urd::bit).
struct RepositoryTypesPackedRelation
{
};
}
struct RepositoryTypesCheckout;

}  // namespace ygg::tests

namespace urd
{

template<>
struct data<ygg::tests::RepositoryTypesElement>
{
    urd::index<ygg::tests::RepositoryTypesElement> index;
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
struct view_mixin<ygg::tests::RepositoryTypesElement, V>
{
private:
    [[nodiscard]] const V& self() const noexcept { return static_cast<const V&>(*this); }

public:
    [[nodiscard]] decltype(auto) get_value() const { return self().template member<&urd::record_of<V>::value>(); }

    /// As a key of row pools, its rows' column types: `value` object columns.
    [[nodiscard]] auto get_column_types() const
    {
        return std::views::iota(0, get_value()) | std::views::transform([](int) { return ygg::type_id<urd::index<ygg::formalism::Object>>; });
    }

    static constexpr auto functions() noexcept { return urd::Functions<urd::function<"get_column_types", &view_mixin::get_column_types>> {}; }
};

template<>
struct data<ygg::tests::RepositoryTypesSerializedElement>
{
    urd::index<ygg::tests::RepositoryTypesSerializedElement> index;
    urd::flat_vector<urd::index<ygg::tests::RepositoryTypesElement>> values;

    data() = default;

    explicit data(urd::flat_vector<urd::index<ygg::tests::RepositoryTypesElement>> values_) : index(), values(std::move(values_)) {}

    data(const data& other) = delete;
    data& operator=(const data& other) = delete;
    data(data&& other) = default;
    data& operator=(data&& other) = default;
    ~data() = default;

    static constexpr auto fields() noexcept { return urd::Fields<urd::field<"values", &data::values>> {}; }

    static constexpr auto identifying_members() noexcept { return urd::members_of(fields()); }

    static constexpr auto cista_members() noexcept { return urd::Members<&data::index, &data::values> {}; }
};

template<typename V>
struct view_mixin<ygg::tests::RepositoryTypesSerializedElement, V>
{
private:
    [[nodiscard]] const V& self() const noexcept { return static_cast<const V&>(*this); }

public:
    [[nodiscard]] decltype(auto) get_values() const { return self().template member<&urd::record_of<V>::values>(); }
};

template<>
struct data<ygg::tests::RepositoryTypesRelation>
{
    urd::index<ygg::tests::RepositoryTypesRelation> index;
    ygg::uint_t value = 0;

    data() = default;

    explicit data(ygg::uint_t value_) : index(), value(value_) {}

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
struct view_mixin<ygg::tests::RepositoryTypesRelation, V>
{
private:
    [[nodiscard]] const V& self() const noexcept { return static_cast<const V&>(*this); }

public:
    [[nodiscard]] decltype(auto) get_value() const { return self().template member<&urd::record_of<V>::value>(); }

    /// A key of row pools gives its rows' column types (here: `value` object columns).
    [[nodiscard]] auto get_column_types() const
    {
        return std::views::iota(ygg::uint_t { 0 }, get_value())
               | std::views::transform([](ygg::uint_t) { return ygg::type_id<urd::index<ygg::formalism::Object>>; });
    }

    static constexpr auto functions() noexcept { return urd::Functions<urd::function<"get_column_types", &view_mixin::get_column_types>> {}; }
};

template<>
struct data<ygg::tests::RepositoryTypesPackedRelation>
{
    urd::index<ygg::tests::RepositoryTypesPackedRelation> index;
    ygg::uint_t value = 0;

    data() = default;

    explicit data(ygg::uint_t value_) : index(), value(value_) {}

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
struct view_mixin<ygg::tests::RepositoryTypesPackedRelation, V>
{
private:
    [[nodiscard]] const V& self() const noexcept { return static_cast<const V&>(*this); }

public:
    [[nodiscard]] decltype(auto) get_value() const { return self().template member<&urd::record_of<V>::value>(); }

    /// A key of row pools gives its rows' column types (here: `value` object columns of the packed tag).
    [[nodiscard]] auto get_column_types() const
    {
        return std::views::iota(ygg::uint_t { 0 }, get_value())
               | std::views::transform([](ygg::uint_t) { return ygg::type_id<urd::index<ygg::formalism::Object>>; });
    }

    static constexpr auto functions() noexcept { return urd::Functions<urd::function<"get_column_types", &view_mixin::get_column_types>> {}; }
};

template<>
struct data<ygg::formalism::Object>
{
    urd::index<ygg::formalism::Object> index;
    ygg::uint_t value = 0;

    data() = default;

    explicit data(ygg::uint_t value_) : index(), value(value_) {}

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
struct view_mixin<ygg::formalism::Object, V>
{
private:
    [[nodiscard]] const V& self() const noexcept { return static_cast<const V&>(*this); }

public:
    [[nodiscard]] decltype(auto) get_value() const { return self().template member<&urd::record_of<V>::value>(); }
};

/// The builders of the storage tests' records: their records' members (no children).
template<typename C>
struct builder<ygg::tests::RepositoryTypesElement, C>
{
    urd::index<ygg::tests::RepositoryTypesElement> index;
    int value = 0;

    static constexpr auto fields() noexcept { return urd::Fields<urd::field<"value", &builder::value>> {}; }
};

template<typename C>
struct builder<ygg::tests::RepositoryTypesBuilderOther, C>
{
    int value = 0;

    static constexpr auto fields() noexcept { return urd::Fields<urd::field<"value", &builder::value>> {}; }
};

template<typename C>
struct builder<ygg::tests::RepositoryTypesCheckout, C>
{
    int id = 0;
    std::vector<int> values;

    static constexpr auto fields() noexcept { return urd::Fields<urd::field<"id", &builder::id>, urd::field<"values", &builder::values>> {}; }
};

template<>
struct data<ygg::tests::RepositoryTypesBuilderOther>
{
    int value = 0;

    data() = default;

    explicit data(int value_) : value(value_) {}

    static constexpr bool copyable() noexcept
    {
        static_assert(std::is_trivially_copyable_v<data>, "a copyable record is trivially copyable");
        return true;
    }

    static constexpr auto fields() noexcept { return urd::Fields<urd::field<"value", &data::value>> {}; }

    static constexpr auto identifying_members() noexcept { return urd::members_of(fields()); }

    static constexpr auto cista_members() noexcept { return urd::Members<&data::value> {}; }
};

template<typename V>
struct view_mixin<ygg::tests::RepositoryTypesBuilderOther, V>
{
private:
    [[nodiscard]] const V& self() const noexcept { return static_cast<const V&>(*this); }

public:
    [[nodiscard]] decltype(auto) get_value() const { return self().template member<&urd::record_of<V>::value>(); }
};

template<>
struct data<ygg::tests::RepositoryTypesCheckout>
{
    int id = 0;
    urd::Store<urd::sequence<int>, urd::contiguous> values;

    data() = default;

    data(int id_, urd::Store<urd::sequence<int>, urd::contiguous> values_) : id(id_), values(std::move(values_)) {}

    data(const data& other) = delete;
    data& operator=(const data& other) = delete;
    data(data&& other) = default;
    data& operator=(data&& other) = default;
    ~data() = default;

    static constexpr auto fields() noexcept { return urd::Fields<urd::field<"id", &data::id>, urd::field<"values", &data::values>> {}; }

    static constexpr auto identifying_members() noexcept { return urd::members_of(fields()); }

    static constexpr auto cista_members() noexcept { return urd::Members<&data::id, &data::values> {}; }
};

template<typename V>
struct view_mixin<ygg::tests::RepositoryTypesCheckout, V>
{
private:
    [[nodiscard]] const V& self() const noexcept { return static_cast<const V&>(*this); }

public:
    [[nodiscard]] decltype(auto) get_id() const { return self().template member<&urd::record_of<V>::id>(); }

    [[nodiscard]] decltype(auto) get_values() const { return self().template member<&urd::record_of<V>::values>(); }
};

}  // namespace urd

namespace ygg::tests
{
namespace
{
/// The key of a pool of `arity` object columns: family F's record, interned in `repository`, which declares its pool.
template<typename F, typename Repository>
urd::index<F> declared(Repository& repository, ygg::uint_t arity)
{
    auto key = urd::data<F>(arity);
    return repository.insert(key).first.index();
}
}  // namespace

TEST(YggdrasilTests, CommonRepositoryTypesUmbrellaHeaderCompiles) { SUCCEED(); }

using FreeRepository = ygg::FreeRelationRepository;
static_assert(ygg::SchemaViewConcept<urd::index_view<ygg::Schema, FreeRepository>>);
static_assert(ygg::SchemaViewConcept<urd::borrowed_view<ygg::Schema, FreeRepository, urd::builder<ygg::Schema, FreeRepository>>>);
static_assert(ygg::SchemaViewConcept<urd::borrowed_view<ygg::Schema, FreeRepository>>);
static_assert(!ygg::SchemaViewConcept<urd::index_view<ygg::Columns, FreeRepository>>);
static_assert(ygg::ColumnsViewConcept<urd::index_view<ygg::Columns, FreeRepository>>);
static_assert(ygg::ColumnsViewConcept<urd::borrowed_view<ygg::Columns, FreeRepository, urd::builder<ygg::Columns, FreeRepository>>>);
static_assert(ygg::ColumnsViewConcept<urd::borrowed_view<ygg::Columns, FreeRepository>>);
static_assert(!ygg::ColumnsViewConcept<urd::index_view<ygg::Schema, FreeRepository>>);
// A builder's rows are searched by values in their own order (its set's lower_bound); a record's by position.
static_assert(urd::OrderedLookup<decltype(std::declval<const ygg::FreeRelationBuilderView&>().get_rows()), std::span<const ygg::Value>>);
static_assert(!urd::OrderedLookup<decltype(std::declval<const ygg::FreeRelationView&>().get_rows()), std::span<const ygg::Value>>);
static_assert(ygg::RelationViewConcept<ygg::FreeRelationView, ygg::Schema>);
static_assert(ygg::RelationViewConcept<ygg::FreeRelationBuilderView, ygg::Schema>);
static_assert(ygg::RelationViewConcept<const ygg::FreeRelationBuilderView&, ygg::Schema>);
static_assert(!ygg::RelationViewConcept<ygg::FreeRelationView, RepositoryTypesRelation>);
static_assert(!ygg::RelationViewConcept<ygg::RowView<ygg::Schema, ygg::FreeRelationRepository>, ygg::Schema>);

/// A relation builder keeps its rows in tuple order and unique, reads as its interned record does, and interns into a
/// record of the same rows; renaming relabels its columns, keeping its rows.
TEST(YggdrasilTests, RelationBuilderKeepsTupleOrderAndInterns)
{
    auto repository = ygg::FreeRelationRepository();
    auto records = urd::Store<urd::slots<urd::data<ygg::Row<ygg::Schema>>>>();
    const auto columns =
        std::array { ygg::ColumnLayout { urd::index<ygg::Column>(0), ygg::TypeId(0) }, ygg::ColumnLayout { urd::index<ygg::Column>(1), ygg::TypeId(0) } };
    auto relation = ygg::FreeRelationBuilder();
    relation.initialize(repository, columns);
    const auto row = [](ygg::Value a, ygg::Value b) { return std::array { a, b }; };
    EXPECT_EQ(relation.insert(repository, records, row(2, 1)), std::pair(std::size_t { 0 }, true));
    EXPECT_EQ(relation.insert(repository, records, row(1, 5)), std::pair(std::size_t { 0 }, true));
    EXPECT_EQ(relation.insert(repository, records, row(1, 7)), std::pair(std::size_t { 1 }, true));
    EXPECT_EQ(relation.insert(repository, records, row(1, 5)), std::pair(std::size_t { 0 }, false));

    const auto built = repository.view(relation);
    EXPECT_EQ(built.arity(), 2U);
    EXPECT_EQ(built.size(), 3U);
    EXPECT_TRUE(built.contains(row(1, 7)));
    EXPECT_FALSE(built.contains(row(7, 1)));
    EXPECT_EQ(built.find(row(2, 1)), std::optional<std::size_t>(2));

    const auto renamed =
        std::array { ygg::ColumnLayout { urd::index<ygg::Column>(2), ygg::TypeId(0) }, ygg::ColumnLayout { urd::index<ygg::Column>(3), ygg::TypeId(0) } };
    relation.rename(repository, renamed);
    EXPECT_EQ(repository.view(relation).columns()[1].label, urd::index<ygg::Column>(3));

    // The rows that start with a prefix, in a builder (its set searches itself) and once interned (searched from outside).
    const auto starting = [](const auto& view, ygg::Value first)
    {
        auto seconds = std::vector<ygg::Value>();
        for (const auto& held : view.with_prefix(std::array { first }))
            seconds.push_back(ygg::with_values(held, [](const auto& values) { return std::ranges::begin(values)[1]; }));
        return seconds;
    };
    EXPECT_EQ(starting(built, 1), (std::vector<ygg::Value> { 5, 7 }));
    EXPECT_EQ(starting(built, 2), (std::vector<ygg::Value> { 1 }));
    EXPECT_TRUE(starting(built, 3).empty());

    auto storage = ygg::FreeRelationStorage();
    const auto [interned, created] = repository.insert(relation, storage);
    EXPECT_TRUE(created);
    EXPECT_EQ(starting(interned, 1), (std::vector<ygg::Value> { 5, 7 }));

    // A record of the same rows, in another order: not canonical, until ygg::formalism::insert makes it so.
    auto reversed = urd::flat_vector<ygg::RowIndex<ygg::Schema>>();
    for (std::size_t i = interned.size(); i-- > 0;)
        reversed.push_back(interned.row(i).index());
    auto record = urd::data<ygg::Relation<ygg::Schema>>(std::get<0>(relation.pool).index(), std::get<0>(relation.columns).index(), std::move(reversed));
    EXPECT_FALSE(ygg::is_canonical(record, repository));
    const auto [same, same_created] = ygg::formalism::insert(repository, record);
    EXPECT_TRUE(ygg::is_canonical(record, repository));
    EXPECT_FALSE(same_created);
    EXPECT_EQ(same, interned);
    EXPECT_EQ(interned.size(), 3U);
    EXPECT_EQ(interned.find(row(1, 5)), std::optional<std::size_t>(0));
    EXPECT_EQ(interned.find(row(2, 1)), std::optional<std::size_t>(2));

    EXPECT_TRUE(relation.erase(repository, row(1, 7)));
    EXPECT_FALSE(relation.erase(repository, row(1, 7)));
    EXPECT_EQ(repository.view(relation).size(), 2U);
}

/// A repository interns a view of a record of its kinds as a child resolves to it, and finds it likewise: a builder's
/// view as that builder, an interned view as itself (another repository's: std::invalid_argument).
TEST(YggdrasilTests, RepositoryInternsAndFindsBuilderAndInternedViews)
{
    auto repository = ygg::FreeRelationRepository();
    auto storage = ygg::FreeRelationStorage();
    const auto layouts =
        std::array { ygg::ColumnLayout { urd::index<ygg::Column>(0), ygg::TypeId(0) }, ygg::ColumnLayout { urd::index<ygg::Column>(1), ygg::TypeId(0) } };
    auto columns = storage.checkout<ygg::Columns>();
    columns->values.assign(layouts.begin(), layouts.end());
    const auto built = urd::resolve(*columns, repository);

    EXPECT_FALSE(repository.find(built, storage).has_value());
    const auto [interned, created] = repository.insert(built, storage);
    EXPECT_TRUE(created);
    EXPECT_EQ(repository.find(built, storage), std::optional(interned));


    EXPECT_EQ(repository.insert(interned, storage), std::pair(interned, false));
    EXPECT_EQ(repository.find(interned, storage), std::optional(interned));
    auto other = ygg::FreeRelationRepository();
    (void) other.insert(built, storage);  // a record at the same key, of another repository
    EXPECT_THROW((void) other.insert(interned, storage), std::invalid_argument);
}

/// A scratch relation keeps its rows in its own repository; drawn again, it starts empty, with other columns if asked.
TEST(YggdrasilTests, ScratchRelationsDrawRelationsWithTheirRows)
{
    auto scratch = ygg::FreeScratchRelations();
    const auto columns =
        std::array { ygg::ColumnLayout { urd::index<ygg::Column>(0), ygg::TypeId(0) }, ygg::ColumnLayout { urd::index<ygg::Column>(1), ygg::TypeId(0) } };
    const ygg::FreeScratchRelation* first = nullptr;
    {
        auto relation = scratch.draw(columns);
        first = relation.get();
        relation->insert(std::array<ygg::Value, 2> { 3, 4 });
        relation->insert(std::array<ygg::Value, 2> { 1, 2 });
        const auto view = relation->view();
        EXPECT_EQ(view.size(), 2U);
        EXPECT_EQ(view.find(std::array<ygg::Value, 2> { 1, 2 }), std::optional<std::size_t>(0));
    }
    auto again = scratch.draw(columns);
    EXPECT_EQ(again.get(), first);  // its slot, reused for the same arity
    EXPECT_EQ(again->view().size(), 0U);
    auto narrower = scratch.draw(std::span(columns).first(1));
    EXPECT_NE(narrower.get(), first);  // another arity: a slot of its own
    EXPECT_EQ(narrower->view().arity(), 1U);
    narrower->insert(std::array<ygg::Value, 1> { 9 });
    EXPECT_TRUE(narrower->view().contains(std::array<ygg::Value, 1> { 9 }));
}

/// A row reads its column types from its relation in whatever form it holds it: a built Schema as an interned one.
TEST(YggdrasilTests, RowBuilderReadsItsTypesFromItsRelationInEachForm)
{
    auto repository = ygg::FreeRelationRepository();
    auto schemas = urd::Store<urd::slots<urd::builder<ygg::Schema, ygg::FreeRelationRepository>>>();
    auto row = urd::builder<ygg::Row<ygg::Schema>, ygg::FreeRelationRepository>();
    auto& schema = urd::builder_of(row.relation, schemas, repository);
    schema.types.push_back(ygg::type_id<bool>);
    schema.types.push_back(ygg::type_id<std::uint32_t>);
    row.values.push_back(ygg::encode(true, ygg::type_id<bool>));
    row.values.push_back(ygg::encode(std::uint64_t { 7 }, ygg::type_id<std::uint32_t>));

    const auto built = repository.view(row);
    EXPECT_EQ(built.type(0), ygg::type_id<bool>);
    EXPECT_EQ(built.get<std::uint32_t>(1), 7U);

    row.relation = repository.view(repository.schema(std::array { ygg::type_id<bool>, ygg::type_id<std::uint32_t> }));
    EXPECT_EQ(repository.view(row).type(1), ygg::type_id<std::uint32_t>);
    EXPECT_THROW(static_cast<void>(repository.view(row).type(2)), std::out_of_range);
}

using CommonBuilderStorage = ygg::formalism::BuilderStorage<BuilderContext, RepositoryTypesElement, RepositoryTypesBuilderOther>;

TEST(YggdrasilTests, CommonBuilderStorageReusesReleasedBuildersByType)
{
    auto storage = CommonBuilderStorage();

    auto element_builder = storage.checkout<RepositoryTypesElement>();
    element_builder->value = 7;
    const auto* first_element_address = element_builder.get();
    element_builder = {};

    auto reused_element_builder = storage.checkout<RepositoryTypesElement>();
    EXPECT_EQ(reused_element_builder.get(), first_element_address);
    EXPECT_EQ(reused_element_builder->value, 0);  // cleared

    auto other_builder = storage.checkout<RepositoryTypesBuilderOther>();
    other_builder->value = 3;
    EXPECT_NE(static_cast<const void*>(other_builder.get()), static_cast<const void*>(reused_element_builder.get()));

    auto scratch = storage.scratch<RepositoryTypesElement>();  // a record, from a pool of its own
    EXPECT_EQ(scratch->value, 0);
}

template<typename T, typename Storage>
concept CanCheckout = requires(Storage& storage) {
    storage.template checkout<T>();
    ygg::formalism::checkout<T>(storage);
};

using ContractBuilderStorage = ygg::formalism::BuilderStorage<BuilderContext, RepositoryTypesCheckout>;
static_assert(std::is_default_constructible_v<ContractBuilderStorage>);
static_assert(!std::is_copy_constructible_v<ContractBuilderStorage>);
static_assert(!std::is_copy_assignable_v<ContractBuilderStorage>);
static_assert(!std::is_move_constructible_v<ContractBuilderStorage>);
static_assert(!std::is_move_assignable_v<ContractBuilderStorage>);

static_assert(CanCheckout<RepositoryTypesCheckout, ContractBuilderStorage>);
static_assert(!CanCheckout<RepositoryTypesCheckout, ygg::formalism::BuilderStorage<BuilderContext, RepositoryTypesElement>>);

TEST(YggdrasilTests, CommonBuilderCheckoutClearsAndRetainsCapacity)
{
    auto storage = ContractBuilderStorage();
    auto built = storage.checkout<RepositoryTypesCheckout>();
    built->id = 7;
    built->values.assign(32, 42);
    const auto* address = built.get();
    const auto* buffer = built->values.data();
    const auto capacity = built->values.capacity();
    built = {};

    auto cleared = ygg::formalism::checkout<RepositoryTypesCheckout>(storage);
    EXPECT_EQ(cleared.get(), address);
    EXPECT_EQ(cleared->id, 0);
    EXPECT_TRUE(cleared->values.empty());
    EXPECT_EQ(cleared->values.data(), buffer);
    EXPECT_EQ(cleared->values.capacity(), capacity);
}

template<typename Repository, typename T>
concept CanUseAnySymbolOperation = requires(Repository& repository, urd::data<T>& data) { repository.insert(data); }
                                   || requires(const Repository& repository, const urd::data<T>& data) { repository.find(data); }
                                   || requires(const Repository& repository, urd::index<T> index) { repository[index]; }
                                   || requires(const Repository& repository, urd::index<T> index) { repository.owner(index); }
                                   || requires(const Repository& repository, urd::index<T> index) { repository.contains(index); }
                                   || requires(const Repository& repository) { repository.template size<T>(); }
                                   || requires(const Repository& repository) { repository.template memory_usage<T>(); };

template<typename Repository, typename T>
concept CanUseAnyLocalSymbolOperation =
    requires(Repository& repository) { repository.template get<T>(); } || requires(const Repository& repository) { repository.template get<T>(); };

using ContractSymbols = formalism::SymbolRepository<RepositoryTypesElement, RepositoryTypesRelation>;
using ContractConcurrentSymbols = formalism::ConcurrentSymbolRepository<RepositoryTypesElement, RepositoryTypesRelation>;
using ContractRepository = formalism::Repository<ContractSymbols, formalism::RelationRepository<urd::word, RepositoryTypesRelation>>;
using ContractBinding = Row<RepositoryTypesRelation>;
static_assert(std::same_as<ContractSymbols::SymbolTypes, boost::mp11::mp_list<RepositoryTypesElement, RepositoryTypesRelation>>);
static_assert(std::same_as<ContractConcurrentSymbols::SymbolTypes, ContractSymbols::SymbolTypes>);
static_assert(std::same_as<ContractRepository::SymbolTypes, ContractSymbols::SymbolTypes>);
static_assert(formalism::SupportsSymbol<const ContractSymbols&, RepositoryTypesElement>);
static_assert(formalism::SupportsSymbol<ContractConcurrentSymbols, RepositoryTypesElement>);
static_assert(formalism::SupportsSymbol<ContractRepository, RepositoryTypesElement>);
static_assert(!formalism::SupportsSymbol<ContractSymbols, RepositoryTypesSerializedElement>);
static_assert(!formalism::SupportsSymbol<ContractRepository, ContractBinding>);
static_assert(!formalism::SupportsSymbol<int, RepositoryTypesElement>);
static_assert(!CanUseAnySymbolOperation<ContractSymbols, RepositoryTypesSerializedElement>);
static_assert(!CanUseAnyLocalSymbolOperation<ContractSymbols, RepositoryTypesSerializedElement>);
static_assert(!CanUseAnySymbolOperation<ContractConcurrentSymbols, RepositoryTypesSerializedElement>);
static_assert(!CanUseAnyLocalSymbolOperation<ContractConcurrentSymbols, RepositoryTypesSerializedElement>);
static_assert(!CanUseAnySymbolOperation<ContractRepository, RepositoryTypesSerializedElement>);
static_assert(Viewable<urd::index<RepositoryTypesElement>, ContractSymbols>);
static_assert(!Viewable<urd::index<RepositoryTypesSerializedElement>, ContractSymbols>);
static_assert(!Viewable<urd::index<RepositoryTypesSerializedElement>, ContractRepository>);

using ContractRelations = formalism::RelationRepository<urd::word, RepositoryTypesRelation>;
using ContractConcurrentRelations = formalism::ConcurrentRelationRepository<urd::word, RepositoryTypesRelation>;
static_assert(std::same_as<ContractRelations::RelationTypes, boost::mp11::mp_list<RepositoryTypesRelation>>);
static_assert(std::same_as<ContractConcurrentRelations::RelationTypes, ContractRelations::RelationTypes>);
static_assert(std::same_as<ContractRepository::RelationTypes, ContractRelations::RelationTypes>);
static_assert(formalism::HasRows<ContractRelations, RepositoryTypesRelation>);
static_assert(formalism::HasRows<ContractRepository, RepositoryTypesRelation>);
static_assert(!formalism::HasRelations<ContractRepository, RepositoryTypesRelation>);
static_assert(!formalism::HasRows<ContractRelations, RepositoryTypesElement>);
static_assert(!formalism::HasRows<int, RepositoryTypesRelation>);
static_assert(!SymbolContextFor<ContractSymbols, RepositoryTypesElement>);
static_assert(SymbolContextFor<ContractRepository, RepositoryTypesElement>);
static_assert(!SymbolContextFor<ContractSymbols, RepositoryTypesSerializedElement>);
static_assert(!SymbolRepositoryFor<ContractRepository, ContractBinding>);
static_assert(std::same_as<formalism::BuilderStorage<BuilderContext, RepositoryTypesCheckout>::Types, boost::mp11::mp_list<RepositoryTypesCheckout>>);

template<typename Repository, typename T>
concept CanUseUnsupportedRelation = requires(Repository& repository, urd::data<Row<T>>& data) { repository.insert(data); }
                                    || requires(const Repository& repository, urd::data<Row<T>>& data) { repository.find(data); }
                                    || requires(const Repository& repository, RowIndex<T> index) { repository.values(index); }
                                    || requires(const Repository& repository, RowIndex<T> index) { repository.owner(index); }
                                    || requires(const Repository& repository, urd::index<T> index) { repository.size(index); }
                                    || requires(Repository& repository) { repository.template get<T>(); };
static_assert(!CanUseUnsupportedRelation<ContractRelations, RepositoryTypesElement>);
static_assert(!CanUseUnsupportedRelation<ContractConcurrentRelations, RepositoryTypesElement>);
static_assert(!CanUseUnsupportedRelation<ContractRepository, RepositoryTypesElement>);
static_assert(!Viewable<RowIndex<RepositoryTypesElement>, ContractRepository>);

TEST(YggdrasilTests, CommonIndexedViewPreservesLookupHandleAndOwner)
{
    ContractRepository repository;
    urd::data<RepositoryTypesElement> data;
    data.value = 31;
    const auto published = repository.insert(data).first;
    const auto view = urd::index_view<RepositoryTypesElement, ContractRepository>(published.index(), &repository);
    EXPECT_EQ(view.index(), published.index());
    EXPECT_EQ(&view.context(), &repository);
    EXPECT_EQ(&view.get_value(), &published.get_value());
    EXPECT_EQ(std::get<1>(urd::detail::identity(view)), repository.layer());
}

template<typename Relations>
static void expect_row_access_for_each_relation_type()
{
    using ObjectIndex = urd::index<formalism::Object>;
    using Repository = formalism::Repository<ContractConcurrentSymbols, Relations>;
    static_assert(Viewable<RowIndex<RepositoryTypesRelation>, Repository>);
    static_assert(Viewable<RowIndex<RepositoryTypesElement>, Repository>);

    Repository repository;
    const auto relation = declared<RepositoryTypesRelation>(repository, 1);
    const auto element = declared<RepositoryTypesElement>(repository, 2);
    urd::data<Row<RepositoryTypesRelation>> first_data;
    first_data.relation = relation;
    first_data.values.push_back(ygg::encode_value(ObjectIndex(3)));
    urd::data<Row<RepositoryTypesElement>> second_data;
    second_data.relation = element;
    second_data.values.push_back(ygg::encode_value(ObjectIndex(7)));
    second_data.values.push_back(ygg::encode_value(ObjectIndex(9)));
    const auto first = repository.insert(first_data).first;
    const auto second = repository.insert(second_data).first;
    EXPECT_EQ(first.get_values().size(), 1);
    EXPECT_EQ(first.get_values()[0], 3U);
    EXPECT_EQ(second.get_values().size(), 2);
    EXPECT_EQ(second.get_values()[0], 7U);
    EXPECT_EQ(second.get_values()[1], 9U);
    EXPECT_EQ(second.value(1), 9);
    EXPECT_EQ(second.type(1), ygg::type_id<ObjectIndex>);
    EXPECT_EQ(std::get<urd::index<formalism::Object>>(second.get(1)).value, 9U);
    EXPECT_EQ(second.template get<ObjectIndex>(1), ObjectIndex(9));
    EXPECT_EQ(second.template get<ObjectIndex>()[0], ObjectIndex(7));
    EXPECT_THROW((void) second.template get<double>(1), std::invalid_argument);
}

TEST(YggdrasilTests, CommonRelationAccessUsesDecodedObjectTypeForEveryRelation)
{
    expect_row_access_for_each_relation_type<formalism::RelationRepository<urd::word, RepositoryTypesRelation, RepositoryTypesElement>>();
    expect_row_access_for_each_relation_type<formalism::ConcurrentRelationRepository<urd::word, RepositoryTypesRelation, RepositoryTypesElement>>();
    expect_row_access_for_each_relation_type<formalism::RelationRepository<urd::bit, RepositoryTypesRelation, RepositoryTypesElement>>();
    expect_row_access_for_each_relation_type<formalism::ConcurrentRelationRepository<urd::bit, RepositoryTypesRelation, RepositoryTypesElement>>();
}

TEST(YggdrasilTests, CommonRowValuesTextWritesEachColumnAsItsTypeWithoutTheRelation)
{
    using Object = formalism::Object;
    using Repository =
        formalism::Repository<formalism::SymbolRepository<Object, RepositoryTypesRelation>, formalism::RelationRepository<urd::word, RepositoryTypesRelation>>;
    Repository repository;
    auto first = urd::data<Object>(5);
    auto second = urd::data<Object>(8);
    const auto a = repository.insert(first).first;
    const auto b = repository.insert(second).first;
    urd::data<Row<RepositoryTypesRelation>> row;
    row.relation = declared<RepositoryTypesRelation>(repository, 2);
    row.values.push_back(ygg::encode_value(b.index()));
    row.values.push_back(ygg::encode_value(a.index()));
    const auto stored = repository.insert(row).first;
    EXPECT_EQ(ygg::values_text<RepositoryTypesRelation>(stored), fmt::format("{} {}", b, a));
}

namespace
{
struct PreparedRepository
{
    ContractSymbols symbols;
    size_t preparation_count = 0;
    size_t raw_count = 0;

    std::pair<urd::index_view<RepositoryTypesElement, PreparedRepository>, bool> insert(urd::data<RepositoryTypesElement>& data)
    {
        ++raw_count;
        const auto [index, created] = formalism::intern(symbols.get<RepositoryTypesElement>(), data);
        return { urd::index_view<RepositoryTypesElement, PreparedRepository>(index, this), created };
    }

    const auto& operator[](urd::index<RepositoryTypesElement> index) const { return symbols[index]; }

    // What a context of views offers; nothing here evaluates it.
    [[maybe_unused]] friend const PreparedRepository& get_repository(const PreparedRepository& repository URD_LIFETIMEBOUND) noexcept { return repository; }
};

void prepare_for_insert(PreparedRepository& repository, urd::data<RepositoryTypesElement>& data)
{
    ++repository.preparation_count;
    data.value %= 10;
}
}  // namespace

namespace
{
struct MissingPreparationRepository
{
    std::pair<urd::index_view<RepositoryTypesElement, MissingPreparationRepository>, bool> insert(urd::data<RepositoryTypesElement>&);
};

}  // namespace

namespace
{
// Found by argument-dependent lookup: the repository's families are this namespace's.
[[maybe_unused]] void prepare_for_insert(ContractRepository&, urd::data<ContractBinding>&) noexcept {}
}  // namespace

template<typename Repository, typename T>
concept CanPrepareAndIntern = requires(Repository& repository, urd::data<T>& data) { formalism::insert(repository, data); };
static_assert(CanPrepareAndIntern<PreparedRepository, RepositoryTypesElement>);
static_assert(CanPrepareAndIntern<ContractRepository, ContractBinding>);
static_assert(!CanPrepareAndIntern<MissingPreparationRepository, RepositoryTypesElement>);

TEST(YggdrasilTests, CommonInterningPreparesExactlyOnceBeforeRawInterning)
{
    auto repository = PreparedRepository();
    auto data = urd::data<RepositoryTypesElement>();
    data.value = 17;
    const auto [first, created] = formalism::insert(repository, data);
    EXPECT_TRUE(created);
    EXPECT_EQ(first.get_value(), 7);
    EXPECT_EQ(data.index, first.index());
    EXPECT_EQ(repository.preparation_count, 1);
    EXPECT_EQ(repository.raw_count, 1);

    data.value = 27;
    data.index = urd::index<RepositoryTypesElement>(99);
    const auto [duplicate, duplicate_created] = formalism::insert(repository, data);
    EXPECT_FALSE(duplicate_created);
    EXPECT_EQ(duplicate.index(), first.index());
    EXPECT_EQ(data.index, first.index());
    EXPECT_EQ(repository.preparation_count, 2);
    EXPECT_EQ(repository.raw_count, 2);
}

TEST(YggdrasilTests, CommonInterningAcceptsRowsWithoutSymbolIdentity)
{
    auto repository = ContractRepository();
    const auto relation = declared<RepositoryTypesRelation>(repository, 1);
    using Object = formalism::Object;
    auto objects = urd::flat_vector<ygg::Value>();
    objects.push_back(ygg::encode_value(urd::index<Object>(3)));
    auto data = urd::data<ContractBinding>(relation, std::move(objects));
    const auto [first, created] = formalism::insert(repository, data);
    EXPECT_TRUE(created);
    const auto [duplicate, duplicate_created] = formalism::insert(repository, data);
    EXPECT_FALSE(duplicate_created);
    EXPECT_EQ(duplicate.index(), first.index());
    EXPECT_EQ(first.template get<urd::index<Object>>(0), urd::index<Object>(3));
    EXPECT_EQ(urd::stored<RowIndex<RepositoryTypesRelation>>(first), first.index());  // a row's view written as its key
}

TEST(YggdrasilTests, CommonRepositoryLayersHaveDistinctNumbersAndParents)
{
    using SymbolRepo = ygg::formalism::SymbolRepository<RepositoryTypesElement>;
    using RelationRepo = ygg::formalism::RelationRepository<urd::word, RepositoryTypesRelation>;
    using Repository = ygg::formalism::Repository<SymbolRepo, RelationRepo>;

    auto root = Repository();
    auto child = Repository(&root);
    auto shared = std::make_shared<Repository>(&child);

    EXPECT_LT(root.layer(), child.layer());
    EXPECT_LT(child.layer(), shared->layer());
    EXPECT_EQ(root.parent(), nullptr);
    EXPECT_EQ(child.parent(), &root);
    EXPECT_EQ(shared->parent(), &child);
}

TEST(YggdrasilTests, CommonRowViewIdentityUsesTheOwningLayer)
{
    using Object = ygg::formalism::Object;
    using Binding = Row<RepositoryTypesRelation>;
    using SymbolRepo = ygg::formalism::SymbolRepository<RepositoryTypesElement, RepositoryTypesRelation>;
    using RelationRepo = ygg::formalism::RelationRepository<urd::word, RepositoryTypesRelation>;
    using Repository = ygg::formalism::Repository<SymbolRepo, RelationRepo>;
    using StoredRow = RowView<RepositoryTypesRelation, Repository>;

    auto first_repository = Repository();
    auto second_repository = Repository();
    auto independent_repository = Repository();

    // Each repository interns the same key first: the same index in each.
    const auto relation = declared<RepositoryTypesRelation>(first_repository, 1);
    ASSERT_EQ(declared<RepositoryTypesRelation>(second_repository, 1), relation);
    ASSERT_EQ(declared<RepositoryTypesRelation>(independent_repository, 1), relation);
    auto objects = urd::flat_vector<ygg::Value> {};
    objects.push_back(ygg::encode_value(urd::index<Object>(0)));
    const auto data = urd::data<Binding>(relation, objects);

    const auto [first, first_created] = first_repository.insert(data);
    const auto [second, second_created] = second_repository.insert(data);
    const auto [independent, independent_created] = independent_repository.insert(data);

    ASSERT_TRUE(first_created);
    ASSERT_TRUE(second_created);
    ASSERT_TRUE(independent_created);
    EXPECT_EQ(first.index(), second.index());
    EXPECT_EQ(first.index(), independent.index());

    EXPECT_FALSE(urd::EqualTo<StoredRow> {}(first, second));
    EXPECT_NE(urd::Hash<StoredRow> {}(first), urd::Hash<StoredRow> {}(second));
    EXPECT_FALSE(urd::EqualTo<StoredRow> {}(first, independent));
    EXPECT_TRUE(urd::EqualTo<StoredRow> {}(first, StoredRow(first.index(), &first_repository)));
    EXPECT_TRUE(formalism::contains(first_repository, first));
    EXPECT_FALSE(formalism::contains(first_repository, second));
    EXPECT_FALSE(formalism::contains(first_repository, independent));
    EXPECT_NE(first_repository.owner(independent.index()), nullptr);
    // Their logical keys (relation, values) are equal: neither is less.
    const auto key = [](const auto& row) { return std::pair(row.get_relation().index(), row.get_values()); };
    EXPECT_FALSE(urd::Less<> {}(key(first), key(second)));
    EXPECT_FALSE(urd::Less<> {}(key(second), key(first)));

    objects[0] = ygg::encode_value(urd::index<Object>(1));
    const auto [larger, larger_created] = second_repository.insert(urd::data<Binding>(relation, objects));
    ASSERT_TRUE(larger_created);
    EXPECT_TRUE(urd::Less<> {}(key(first), key(larger)));
}

template<typename V>
concept HasPublishedRowIndex = requires(const V& view) { view.index(); };

using RowViewRepository = formalism::Repository<formalism::SymbolRepository<RepositoryTypesRelation, formalism::Object>, ContractRelations>;
using BorrowedContractRowView = urd::borrowed_view<ContractBinding, RowViewRepository>;
using PublishedContractRowView = RowView<RepositoryTypesRelation, RowViewRepository>;
static_assert(RowViewConcept<BorrowedContractRowView, RepositoryTypesRelation>);
static_assert(RowViewConcept<PublishedContractRowView, RepositoryTypesRelation>);
static_assert(RowViewConcept<const PublishedContractRowView&, RepositoryTypesRelation>);
static_assert(RowViewConcept<BorrowedContractRowView&&, RepositoryTypesRelation>);

namespace
{
struct MutableRowView : PublishedContractRowView
{
    urd::index<RepositoryTypesRelation> get_relation();  // not const: no row view
};
}  // namespace

static_assert(!RowViewConcept<MutableRowView&, RepositoryTypesRelation>);
static_assert(!RowViewConcept<BorrowedContractRowView, RepositoryTypesElement>);
static_assert(!RowViewConcept<int, RepositoryTypesRelation>);
static_assert(!HasPublishedRowIndex<BorrowedContractRowView>);
static_assert(HasPublishedRowIndex<PublishedContractRowView>);
static_assert(std::same_as<decltype(std::declval<const PublishedContractRowView&>().index()), RowIndex<RepositoryTypesRelation>>);
static_assert(std::same_as<urd::record_of<BorrowedContractRowView>, urd::data<ContractBinding>>);

TEST(YggdrasilTests, CommonBorrowedRowViewReadsWithoutPublishingAndMaterializesConstData)
{
    using Object = formalism::Object;
    auto repository = RowViewRepository();
    auto relation_data = urd::data<RepositoryTypesRelation>();
    relation_data.value = 2;  // its rows' arity: interning it declares its pool
    const auto relation = repository.insert(relation_data).first.index();
    auto object_data = urd::data<Object>();
    for (uint_t i = 0; i < 8; ++i)
    {
        object_data.value = i;
        repository.insert(object_data);
    }
    auto data = urd::data<ContractBinding>();
    data.relation = relation;
    data.values.push_back(ygg::encode_value(urd::index<Object>(2)));
    data.values.push_back(ygg::encode_value(urd::index<Object>(5)));
    const auto borrowed = urd::resolve(data, repository);
    EXPECT_EQ(&borrowed.get_values(), &data.values);
    EXPECT_EQ(&borrowed.context(), &repository);
    EXPECT_EQ(borrowed.get_relation().index(), relation);
    EXPECT_EQ(borrowed.type(0), ygg::type_id<urd::index<Object>>);  // declared with its key, before any row
    EXPECT_EQ(borrowed.value(1), 5U);
    EXPECT_EQ(borrowed.get_values().data(), data.values.data());
    EXPECT_FALSE(repository.find(data));

    const auto [published, created] = repository.insert(data);
    ASSERT_TRUE(created);
    EXPECT_EQ(&published.context(), &repository);
    EXPECT_TRUE(std::ranges::equal(published.get_values(), borrowed.get_values()));
    const auto [duplicate, duplicate_created] = repository.insert(data);
    EXPECT_FALSE(duplicate_created);
    EXPECT_EQ(duplicate.index(), published.index());

    // A returned object range borrows Data directly, independently of the wrapper; its objects resolve.
    EXPECT_EQ(borrowed.template get<urd::index<Object>>()[1], urd::index<Object>(5));
    const auto objects = urd::resolve(urd::resolve(data, repository).template get<urd::index<Object>>(), repository);
    data.values[0] = ygg::encode_value(urd::index<Object>(7));
    EXPECT_EQ(objects[0].index(), urd::index<Object>(7));
    EXPECT_EQ(urd::resolve(published.template get<urd::index<Object>>(), repository)[0].index(), urd::index<Object>(2));
}

TEST(YggdrasilTests, CommonCompositeRepositorySynchronizesLocalAndInheritedScratchIdentity)
{
    auto root = ContractRepository();
    auto data = urd::data<RepositoryTypesElement>();
    data.index = urd::index<RepositoryTypesElement>(99);
    data.value = 7;
    const auto [original, created] = root.insert(data);
    EXPECT_TRUE(created);
    EXPECT_EQ(data.index, original.index());

    data.index = urd::index<RepositoryTypesElement>(99);
    const auto [duplicate, duplicate_created] = root.insert(data);
    EXPECT_FALSE(duplicate_created);
    EXPECT_EQ(duplicate.index(), original.index());
    EXPECT_EQ(data.index, original.index());

    auto child = ContractRepository(&root);
    data.index = urd::index<RepositoryTypesElement>(99);
    const auto [inherited, inherited_created] = child.insert(data);
    EXPECT_FALSE(inherited_created);
    EXPECT_EQ(data.index, original.index());
    EXPECT_EQ(&inherited.context(), &root);

    data.index = urd::index<RepositoryTypesElement>(99);
    data.value = 11;
    const auto [local, local_created] = child.insert(data);
    EXPECT_TRUE(local_created);
    EXPECT_EQ(data.index, local.index());
    EXPECT_NE(local.index(), inherited.index());
    EXPECT_EQ(&local.context(), &child);
    EXPECT_EQ(root.size<RepositoryTypesElement>(), 1);
    EXPECT_EQ(child.size<RepositoryTypesElement>(), 2);
}

TEST(YggdrasilTests, CommonRepositoryRejectsWrongArityWithoutMutation)
{
    using Object = ygg::formalism::Object;
    using Binding = Row<RepositoryTypesRelation>;

    auto repository = ContractRepository();
    const auto relation = declared<RepositoryTypesRelation>(repository, 2);
    auto objects = urd::flat_vector<ygg::Value> {};
    objects.push_back(ygg::encode_value(urd::index<Object>(0)));
    objects.push_back(ygg::encode_value(urd::index<Object>(1)));
    const auto data = urd::data<Binding>(relation, std::move(objects));
    const auto [view, created] = repository.insert(data);
    ASSERT_TRUE(created);

    auto wrong_objects = urd::flat_vector<ygg::Value> {};
    wrong_objects.push_back(ygg::encode_value(urd::index<Object>(0)));
    const auto wrong = urd::data<Binding>(relation, std::move(wrong_objects));
    EXPECT_FALSE(repository.find(wrong));
    EXPECT_THROW(repository.insert(wrong), std::invalid_argument);
    EXPECT_EQ(repository.size(relation), 1);
    EXPECT_EQ(view.get_values().size(), 2);
}

TEST(YggdrasilTests, CommonRepositoryReportsOwnedMemory)
{
    using Object = ygg::formalism::Object;
    using Binding = Row<RepositoryTypesRelation>;
    using SymbolRepository = ygg::formalism::SymbolRepository<RepositoryTypesElement, RepositoryTypesRelation>;
    using RelationRepository = ygg::formalism::RelationRepository<urd::word, RepositoryTypesRelation>;
    using Repository = ygg::formalism::Repository<SymbolRepository, RelationRepository>;

    auto repository = Repository();
    EXPECT_EQ(repository.memory_usage<RepositoryTypesElement>(), 0);
    EXPECT_EQ(repository.row_memory_usage<RepositoryTypesRelation>(), 0);

    auto element = urd::data<RepositoryTypesElement> {};
    element.value = 7;
    repository.insert(element);

    auto objects = urd::flat_vector<ygg::Value> {};
    objects.push_back(ygg::encode_value(urd::index<Object>(0)));
    objects.push_back(ygg::encode_value(urd::index<Object>(1)));
    const auto data = urd::data<Binding>(declared<RepositoryTypesRelation>(repository, 2), objects);
    repository.insert(data);

    const auto symbol_memory_usage = repository.memory_usage<RepositoryTypesElement>();
    const auto relation_memory_usage = repository.row_memory_usage<RepositoryTypesRelation>();
    EXPECT_GT(symbol_memory_usage, 0);
    EXPECT_GT(relation_memory_usage, 0);

    repository.clear_all();
    EXPECT_EQ(repository.memory_usage<RepositoryTypesElement>(), symbol_memory_usage);
    EXPECT_LE(repository.row_memory_usage<RepositoryTypesRelation>(), relation_memory_usage);

    auto child = Repository(&repository);
    EXPECT_EQ(child.memory_usage<RepositoryTypesElement>(), 0);
    EXPECT_EQ(child.row_memory_usage<RepositoryTypesRelation>(), 0);
}

TEST(YggdrasilTests, CommonRelationRepositorySupportsBitPackedStorageTrait)
{
    using DefaultRelationRepository = ygg::formalism::RelationRepository<urd::word, RepositoryTypesRelation>;
    using Object = ygg::formalism::Object;
    using Binding = Row<RepositoryTypesPackedRelation>;
    using SymbolRepository = ygg::formalism::SymbolRepository<RepositoryTypesElement, RepositoryTypesPackedRelation>;
    using RelationRepository = ygg::formalism::RelationRepository<urd::bit, RepositoryTypesPackedRelation>;
    using Repository = ygg::formalism::Repository<SymbolRepository, RelationRepository>;

    // Both store rows as object columns (urd::column_widths): whole (32 bits) by default, in the layer's object width packed.
    static_assert(std::constructible_from<typename DefaultRelationRepository::template Rows<RepositoryTypesRelation>, const urd::column_widths&>);
    static_assert(std::constructible_from<typename RelationRepository::template Rows<RepositoryTypesPackedRelation>, const urd::column_widths&>);

    auto root = Repository(4, nullptr);
    const auto relation = declared<RepositoryTypesPackedRelation>(root, 2);
    auto objects = urd::flat_vector<ygg::Value> {};
    objects.push_back(ygg::encode_value(urd::index<Object>(0)));
    objects.push_back(ygg::encode_value(urd::index<Object>(3)));
    const auto data = urd::data<Binding>(relation, objects);

    EXPECT_EQ(root.row_memory_usage<RepositoryTypesPackedRelation>(), 0);

    const auto [root_view, root_created] = root.insert(data);
    const auto [duplicate_view, duplicate_created] = root.insert(data);

    EXPECT_TRUE(root_created);
    EXPECT_FALSE(duplicate_created);
    EXPECT_GT(root.row_memory_usage<RepositoryTypesPackedRelation>(), 0);
    EXPECT_EQ(duplicate_view.index().row, root_view.index().row);
    EXPECT_EQ(root_view.template get<urd::index<Object>>(0), urd::index<Object>(0));
    EXPECT_EQ(root_view.template get<urd::index<Object>>(1), urd::index<Object>(3));

    objects[0] = ygg::encode_value(urd::index<Object>(4));
    objects[1] = ygg::encode_value(urd::index<Object>(0));
    const auto wider_data = urd::data<Binding>(relation, objects);

    EXPECT_THROW(root.insert(wider_data), std::out_of_range);
    EXPECT_EQ(root.size(relation), 1);

    const auto [root_duplicate_after_failure, created_after_failure] = root.insert(data);
    EXPECT_FALSE(created_after_failure);
    EXPECT_EQ(root_duplicate_after_failure.index().row, root_view.index().row);

    auto inherited_width_child = Repository(2, &root);
    objects[0] = ygg::encode_value(urd::index<Object>(3));
    const auto inherited_width_data = urd::data<Binding>(relation, objects);
    const auto [inherited_width_view, inherited_width_created] = inherited_width_child.insert(inherited_width_data);
    EXPECT_TRUE(inherited_width_created);
    EXPECT_EQ(inherited_width_view.template get<urd::index<Object>>(0), urd::index<Object>(3));

    auto child = std::make_shared<Repository>(5, &root);
    const auto [inherited_view, inherited_created] = child->insert(data);
    const auto [child_view, child_created] = child->insert(wider_data);

    EXPECT_FALSE(inherited_created);
    EXPECT_EQ(inherited_view.index().row, root_view.index().row);
    EXPECT_EQ(&inherited_view.context(), &root);
    EXPECT_TRUE(child_created);
    EXPECT_EQ(child_view.index().row, urd::index<Row<RepositoryTypesPackedRelation>>(1));
    EXPECT_EQ(child_view.template get<urd::index<Object>>(0), urd::index<Object>(4));
    EXPECT_EQ(child_view.template get<urd::index<Object>>(1), urd::index<Object>(0));
}

TEST(YggdrasilTests, CommonRelationRepositoryValidatesObjectIndexWidth)
{
    using Repository = ygg::formalism::RelationRepository<urd::bit, RepositoryTypesRelation>;
    using SymbolRepository = ygg::formalism::SymbolRepository<RepositoryTypesElement>;
    using Layer = ygg::formalism::Repository<SymbolRepository, Repository>;
    constexpr auto full_width = ygg::formalism::default_object_index_width.bits;

    EXPECT_EQ(Repository().get_object_index_width(), full_width);
    EXPECT_THROW((Repository(nullptr, urd::bit_width { 0 })), std::invalid_argument);
    EXPECT_THROW((Repository(nullptr, urd::bit_width { static_cast<std::uint8_t>(full_width + 1) })), std::invalid_argument);

    const auto parent = Repository(nullptr, urd::bit_width { 2 });
    EXPECT_THROW((Repository(&parent, urd::bit_width { 1 })), std::invalid_argument);

    EXPECT_NO_THROW(Layer(size_t { 0 }, nullptr));
    EXPECT_NO_THROW(std::make_shared<Layer>(1, nullptr));
    const auto full_width_parent = Layer();
    EXPECT_NO_THROW(Layer(size_t { 2 }, &full_width_parent));
    if constexpr (std::numeric_limits<size_t>::digits > std::numeric_limits<ygg::uint_t>::digits)
    {
        const auto oversized_domain = static_cast<size_t>(std::numeric_limits<ygg::uint_t>::max()) + size_t { 1 };
        EXPECT_THROW(Layer(oversized_domain, nullptr), std::invalid_argument);
    }
}

TEST(YggdrasilTests, CommonRepositoryThrowsForMissingSymbolIndices)
{
    using SymbolRepo = ygg::formalism::SymbolRepository<RepositoryTypesElement>;
    using RelationRepo = ygg::formalism::RelationRepository<urd::word, RepositoryTypesRelation>;
    using Repository = ygg::formalism::Repository<SymbolRepo, RelationRepo>;

    auto repository = Repository();

    EXPECT_THROW(repository[urd::index<RepositoryTypesElement>(0)], std::out_of_range);
    EXPECT_EQ(repository.owner(urd::index<RepositoryTypesElement>(0)), nullptr);
    EXPECT_THROW(static_cast<void>(repository.view(urd::index<RepositoryTypesElement>(0))), std::out_of_range);

    auto data = urd::data<RepositoryTypesElement> {};
    data.value = 7;
    const auto [view, created] = repository.insert(data);

    EXPECT_TRUE(created);
    EXPECT_EQ(repository[view.index()].value, 7);
    EXPECT_EQ(repository.owner(view.index()), &repository);
    EXPECT_THROW(repository[urd::index<RepositoryTypesElement>(1)], std::out_of_range);
    EXPECT_THROW(static_cast<void>(repository.view(urd::index<RepositoryTypesElement>(1))), std::out_of_range);
}

TEST(YggdrasilTests, CommonRepositoryThrowsForMissingRowIndices)
{
    using Binding = Row<RepositoryTypesRelation>;
    using Object = ygg::formalism::Object;
    using SymbolRepo = ygg::formalism::SymbolRepository<RepositoryTypesElement, RepositoryTypesRelation>;
    using RelationRepo = ygg::formalism::RelationRepository<urd::word, RepositoryTypesRelation>;
    using Repository = ygg::formalism::Repository<SymbolRepo, RelationRepo>;

    auto repository = Repository();
    const auto relation = declared<RepositoryTypesRelation>(repository, 2);
    const auto missing = RowIndex<RepositoryTypesRelation> { relation, urd::index<Row<RepositoryTypesRelation>>(0) };

    EXPECT_THROW(static_cast<void>(repository.values(missing)), std::out_of_range);
    EXPECT_EQ(repository.owner(missing), nullptr);
    EXPECT_THROW(static_cast<void>(urd::resolve(missing, repository)), std::out_of_range);
    const auto rows = std::array { urd::index<Row<RepositoryTypesRelation>>(0) };
    const auto bindings = rows_view(relation, rows, repository);
    EXPECT_THROW((void) bindings[0], std::out_of_range);

    auto objects = urd::flat_vector<ygg::Value> {};
    objects.push_back(ygg::encode_value(urd::index<Object>(0)));
    objects.push_back(ygg::encode_value(urd::index<Object>(1)));
    const auto data = urd::data<Binding>(relation, objects);
    const auto [view, created] = repository.insert(data);

    EXPECT_TRUE(created);
    EXPECT_EQ(view.get_values().size(), 2);
    EXPECT_EQ(repository.owner(view.index()), &repository);

    const auto missing_row = RowIndex<RepositoryTypesRelation> { relation, urd::index<Row<RepositoryTypesRelation>>(1) };
    const auto missing_relation = RowIndex<RepositoryTypesRelation> { urd::index<RepositoryTypesRelation>(1), urd::index<Row<RepositoryTypesRelation>>(0) };
    EXPECT_THROW(static_cast<void>(repository.values(missing_row)), std::out_of_range);
    EXPECT_THROW(static_cast<void>(urd::resolve(missing_row, repository)), std::out_of_range);
    EXPECT_THROW(static_cast<void>(repository.values(missing_relation)), std::out_of_range);
    EXPECT_THROW(static_cast<void>(urd::resolve(missing_relation, repository)), std::out_of_range);
}

/// Whether the repository holds an index or binding key (some layer owns it), or the record a view denotes.
template<typename Repository, typename X>
static bool has(const Repository& repository, const X& x)
{
    if constexpr (requires { formalism::contains(repository, x); })
        return formalism::contains(repository, x);
    else
        return repository.owner(x) != nullptr;
}

template<typename Repository>
static void check_symbol_membership()
{
    using Handle = urd::index<RepositoryTypesElement>;
    using ElementView = urd::index_view<RepositoryTypesElement, Repository>;
    auto root = Repository();
    auto data = urd::data<RepositoryTypesElement> {};
    data.value = 7;
    const auto root_view = root.insert(data).first;
    auto child = Repository(&root);
    data.value = 11;
    const auto child_view = child.insert(data).first;
    auto grandchild = Repository(&child);
    data.value = 13;
    const auto local_view = grandchild.insert(data).first;

    EXPECT_TRUE(has(grandchild, root_view));
    EXPECT_TRUE(formalism::contains(grandchild, root_view));
    EXPECT_TRUE(has(grandchild, child_view));
    EXPECT_TRUE(has(grandchild, local_view));
    EXPECT_FALSE(has(root, child_view));
    EXPECT_FALSE(has(child, local_view));
    EXPECT_TRUE(has(root, ElementView(root_view.index(), &grandchild)));
    EXPECT_FALSE(has(grandchild, ElementView(local_view.index(), &root)));

    const auto indices = std::array { root_view.index(), child_view.index(), local_view.index() };
    EXPECT_TRUE(std::ranges::all_of(indices, [&](const auto& value) { return has(grandchild, value); }));
    EXPECT_FALSE(std::ranges::all_of(indices, [&](const auto& value) { return has(child, value); }));
    EXPECT_TRUE(std::ranges::all_of(urd::resolve(std::span<const Handle>(indices), grandchild), [&](const auto& value) { return has(grandchild, value); }));
    EXPECT_TRUE(std::ranges::all_of(std::span<const Handle> {}, [&](const auto& value) { return has(root, value); }));
    EXPECT_TRUE(std::ranges::all_of(std::span<const ElementView> {}, [&](const auto& value) { return has(root, value); }));
    EXPECT_FALSE(std::ranges::all_of(std::array { root_view.index(), Handle() }, [&](const auto& value) { return has(grandchild, value); }));
    EXPECT_FALSE(has(grandchild, Handle(3)));
    EXPECT_FALSE(has(grandchild, ElementView(Handle(), &grandchild)));

    auto unrelated = Repository();
    EXPECT_FALSE(has(grandchild, ElementView(root_view.index(), &unrelated)));
    data.value = 7;
    const auto unrelated_view = unrelated.insert(data).first;
    ASSERT_EQ(unrelated_view.index(), root_view.index());
    EXPECT_TRUE(has(grandchild, unrelated_view.index()));
    EXPECT_FALSE(has(grandchild, unrelated_view));
    EXPECT_FALSE(formalism::contains(grandchild, unrelated_view));
    EXPECT_FALSE(std::ranges::all_of(std::array { root_view, unrelated_view }, [&](const auto& value) { return has(grandchild, value); }));
}

TEST(YggdrasilTests, CommonSymbolMembershipChecksCanonicalOwnersAcrossLayers)
{
    check_symbol_membership<ContractRepository>();
    check_symbol_membership<formalism::Repository<ContractConcurrentSymbols, ContractConcurrentRelations>>();
}

template<typename Repository>
static void check_relation_membership()
{
    using Handle = RowIndex<RepositoryTypesRelation>;
    using StoredRow = RowView<RepositoryTypesRelation, Repository>;
    using Object = formalism::Object;
    auto root = Repository();
    const auto relation = declared<RepositoryTypesRelation>(root, 1);
    auto objects = urd::flat_vector<ygg::Value> {};
    objects.push_back(ygg::encode_value(urd::index<Object>(0)));
    const auto root_view = root.insert(urd::data<ContractBinding>(relation, objects)).first;
    auto child = Repository(&root);
    objects[0] = ygg::encode_value(urd::index<Object>(1));
    const auto child_view = child.insert(urd::data<ContractBinding>(relation, objects)).first;
    auto grandchild = Repository(&child);
    objects[0] = ygg::encode_value(urd::index<Object>(2));
    const auto local_view = grandchild.insert(urd::data<ContractBinding>(relation, objects)).first;

    EXPECT_TRUE(has(grandchild, root_view));
    EXPECT_TRUE(formalism::contains(grandchild, root_view));
    EXPECT_TRUE(has(grandchild, child_view));
    EXPECT_TRUE(has(grandchild, local_view));
    EXPECT_FALSE(has(root, child_view));
    EXPECT_FALSE(has(child, local_view));
    EXPECT_TRUE(has(root, StoredRow(root_view.index(), &grandchild)));
    EXPECT_FALSE(has(grandchild, StoredRow(local_view.index(), &root)));
    EXPECT_TRUE(std::ranges::all_of(std::array { root_view, child_view, local_view }, [&](const auto& value) { return has(grandchild, value); }));
    EXPECT_TRUE(std::ranges::all_of(std::array { root_view.index(), child_view.index(), local_view.index() },
                                    [&](const auto& value) { return has(grandchild, value); }));
    EXPECT_TRUE(std::ranges::all_of(std::span<const Handle> {}, [&](const auto& value) { return has(root, value); }));
    EXPECT_TRUE(std::ranges::all_of(std::span<const StoredRow> {}, [&](const auto& value) { return has(root, value); }));

    const auto missing_row = Handle { relation, urd::index<Row<RepositoryTypesRelation>>(3) };
    const auto missing_relation = Handle { urd::index<RepositoryTypesRelation>(1), urd::index<Row<RepositoryTypesRelation>>(0) };
    const auto max_row = Handle { relation, urd::index<Row<RepositoryTypesRelation>>() };
    const auto max_relation = Handle { urd::index<RepositoryTypesRelation>(), urd::index<Row<RepositoryTypesRelation>>(0) };
    for (const auto index : { missing_row, missing_relation, max_row, max_relation })
    {
        EXPECT_FALSE(has(grandchild, index));
        EXPECT_FALSE(has(grandchild, StoredRow(index, &grandchild)));
    }
    EXPECT_FALSE(std::ranges::all_of(std::array { root_view.index(), max_row }, [&](const auto& value) { return has(grandchild, value); }));

    auto unrelated = Repository();
    ASSERT_EQ(declared<RepositoryTypesRelation>(unrelated, 1), relation);  // the same key, interned first there too
    EXPECT_FALSE(has(grandchild, StoredRow(root_view.index(), &unrelated)));
    objects[0] = ygg::encode_value(urd::index<Object>(0));
    const auto unrelated_view = unrelated.insert(urd::data<ContractBinding>(relation, objects)).first;
    EXPECT_TRUE(has(grandchild, unrelated_view.index()));
    EXPECT_FALSE(has(grandchild, unrelated_view));
    EXPECT_FALSE(formalism::contains(grandchild, unrelated_view));
}

TEST(YggdrasilTests, CommonRelationMembershipChecksCanonicalOwnersAcrossLayers)
{
    check_relation_membership<ContractRepository>();
    check_relation_membership<formalism::Repository<ContractConcurrentSymbols, ContractConcurrentRelations>>();
}

}  // namespace ygg::tests
