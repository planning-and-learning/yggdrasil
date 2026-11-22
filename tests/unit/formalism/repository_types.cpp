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
#include "yggdrasil/formalism/copy.hpp"
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
#include <urd/elements/keyed.hpp>
#include <urd/elements/view.hpp>
#include <urd/repr/sequences/sequence.hpp>
#include <urd/repr/slots/slots.hpp>
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

namespace ygg::tests
{
namespace
{
/// An element as a key of row pools: its value object columns, stored in its record (as every key of row pools stores its column types).
template<typename Repository>
void prepare_for_insert(Repository& /*repository*/, urd::data<ygg::tests::RepositoryTypesElement>& record)
{
    record.column_types.clear();
    for (auto column = decltype(record.value) { 0 }; column < record.value; ++column)
        record.column_types.push_back(ygg::type_id<urd::index<ygg::formalism::Object>>);
}

/// A relation key: its value object columns, stored in its record (as every key of row pools stores its column types).
template<typename Repository>
void prepare_for_insert(Repository& /*repository*/, urd::data<ygg::tests::RepositoryTypesRelation>& record)
{
    record.column_types.clear();
    for (auto column = decltype(record.value) { 0 }; column < record.value; ++column)
        record.column_types.push_back(ygg::type_id<urd::index<ygg::formalism::Object>>);
}

/// A packed relation key: its value object columns, stored in its record (as every key of row pools stores its column types).
template<typename Repository>
void prepare_for_insert(Repository& /*repository*/, urd::data<ygg::tests::RepositoryTypesPackedRelation>& record)
{
    record.column_types.clear();
    for (auto column = decltype(record.value) { 0 }; column < record.value; ++column)
        record.column_types.push_back(ygg::type_id<urd::index<ygg::formalism::Object>>);
}
}  // namespace
}  // namespace ygg::tests

namespace urd
{

template<>
struct data<ygg::tests::RepositoryTypesElement>
{
    urd::index<ygg::tests::RepositoryTypesElement> index;
    int value = 0;
    /// Its rows' column types, as a key of row pools: filled by its domain's preparation (prepare_for_insert), stored here.
    urd::flat_vector<ygg::TypeId> column_types;

    data() = default;

    explicit data(int value_) : index(), value(value_) {}


    static constexpr auto fields() noexcept { return urd::Fields<urd::field<"value", &data::value>> {}; }

    static constexpr auto identifying_members() noexcept { return urd::members_of(fields()); }

    static constexpr auto cista_members() noexcept { return urd::Members<&data::index, &data::value, &data::column_types> {}; }
};

template<typename V>
struct view_mixin<ygg::tests::RepositoryTypesElement, V>
{
private:
    [[nodiscard]] const V& self() const noexcept { return static_cast<const V&>(*this); }

public:
    [[nodiscard]] decltype(auto) get_value() const { return self().template member<&urd::record_of<V>::value>(); }

    /// As a key of row pools, its rows' column types: those its record stores.
    [[nodiscard]] decltype(auto) get_column_types() const { return self().template member<&urd::record_of<V>::column_types>(); }

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
    /// Its rows' column types, as a key of row pools: filled by its domain's preparation (prepare_for_insert), stored here.
    urd::flat_vector<ygg::TypeId> column_types;

    data() = default;

    explicit data(ygg::uint_t value_) : index(), value(value_) {}


    static constexpr auto fields() noexcept { return urd::Fields<urd::field<"value", &data::value>> {}; }

    static constexpr auto identifying_members() noexcept { return urd::members_of(fields()); }

    static constexpr auto cista_members() noexcept { return urd::Members<&data::index, &data::value, &data::column_types> {}; }
};

template<typename V>
struct view_mixin<ygg::tests::RepositoryTypesRelation, V>
{
private:
    [[nodiscard]] const V& self() const noexcept { return static_cast<const V&>(*this); }

public:
    [[nodiscard]] decltype(auto) get_value() const { return self().template member<&urd::record_of<V>::value>(); }

    /// As a key of row pools, its rows' column types: those its record stores.
    [[nodiscard]] decltype(auto) get_column_types() const { return self().template member<&urd::record_of<V>::column_types>(); }

    static constexpr auto functions() noexcept { return urd::Functions<urd::function<"get_column_types", &view_mixin::get_column_types>> {}; }
};

template<>
struct data<ygg::tests::RepositoryTypesPackedRelation>
{
    urd::index<ygg::tests::RepositoryTypesPackedRelation> index;
    ygg::uint_t value = 0;
    /// Its rows' column types, as a key of row pools: filled by its domain's preparation (prepare_for_insert), stored here.
    urd::flat_vector<ygg::TypeId> column_types;

    data() = default;

    explicit data(ygg::uint_t value_) : index(), value(value_) {}


    static constexpr auto fields() noexcept { return urd::Fields<urd::field<"value", &data::value>> {}; }

    static constexpr auto identifying_members() noexcept { return urd::members_of(fields()); }

    static constexpr auto cista_members() noexcept { return urd::Members<&data::index, &data::value, &data::column_types> {}; }
};

template<typename V>
struct view_mixin<ygg::tests::RepositoryTypesPackedRelation, V>
{
private:
    [[nodiscard]] const V& self() const noexcept { return static_cast<const V&>(*this); }

public:
    [[nodiscard]] decltype(auto) get_value() const { return self().template member<&urd::record_of<V>::value>(); }

    /// As a key of row pools, its rows' column types: those its record stores.
    [[nodiscard]] decltype(auto) get_column_types() const { return self().template member<&urd::record_of<V>::column_types>(); }

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

/// The builders of the storage tests' records: their records' members, each in its built form (no children).
template<typename C>
struct builder<ygg::tests::RepositoryTypesElement, C>
{
    urd::index<ygg::tests::RepositoryTypesElement> index;
    urd::built_t<int, C> value = 0;

    static constexpr auto fields() noexcept { return urd::Fields<urd::field<"value", &builder::value>> {}; }

    static constexpr auto identifying_members() noexcept { return urd::members_of(fields()); }
};

/// The builders of an object and of a pool key (what urd::copy draws where the target does not see them).
template<typename C>
struct builder<ygg::formalism::Object, C>
{
    urd::index<ygg::formalism::Object> index;
    urd::built_t<ygg::uint_t, C> value = 0;

    static constexpr auto fields() noexcept { return urd::Fields<urd::field<"value", &builder::value>> {}; }

    static constexpr auto identifying_members() noexcept { return urd::members_of(fields()); }
};

template<typename C>
struct builder<ygg::tests::RepositoryTypesRelation, C>
{
    urd::index<ygg::tests::RepositoryTypesRelation> index;
    urd::built_t<ygg::uint_t, C> value = 0;

    static constexpr auto fields() noexcept { return urd::Fields<urd::field<"value", &builder::value>> {}; }

    static constexpr auto identifying_members() noexcept { return urd::members_of(fields()); }
};

template<typename C>
struct builder<ygg::tests::RepositoryTypesBuilderOther, C>
{
    urd::built_t<int, C> value = 0;

    static constexpr auto fields() noexcept { return urd::Fields<urd::field<"value", &builder::value>> {}; }

    static constexpr auto identifying_members() noexcept { return urd::members_of(fields()); }
};

template<typename C>
struct builder<ygg::tests::RepositoryTypesCheckout, C>
{
    urd::built_t<int, C> id = 0;
    urd::built_t<urd::Store<urd::sequence<int>, urd::contiguous>, C> values;

    static constexpr auto fields() noexcept { return urd::Fields<urd::field<"id", &builder::id>, urd::field<"values", &builder::values>> {}; }

    static constexpr auto identifying_members() noexcept { return urd::members_of(fields()); }
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

/// A row to store: its relation (its key's known part) and its values (its content).
template<typename F>
struct Bound
{
    urd::index<F> relation;
    urd::flat_vector<ygg::Value> values;
};

template<typename F>
Bound<F> make_bound(urd::index<F> relation, const urd::flat_vector<ygg::Value>& values)
{ return { relation, urd::flat_vector<ygg::Value>(values.begin(), values.end()) }; }

/// The row of `bound` stored where no layer holds it (the repository's one insert: under its relation): its view, and
/// whether it was stored now.
template<typename F, typename Repository>
std::pair<RowView<F, Repository>, bool> insert_bound(Repository& repository, const Bound<F>& bound)
{
    auto content = urd::data<Row<F>>(bound.relation, urd::flat_vector<ygg::Value>(bound.values.begin(), bound.values.end()));
    return repository.insert(content);
}

/// Whether some layer holds the row of `bound` (the repository's one find).
template<typename F, typename Repository>
bool holds_bound(const Repository& repository, const Bound<F>& bound)
{
    const auto content = urd::data<Row<F>>(bound.relation, urd::flat_vector<ygg::Value>(bound.values.begin(), bound.values.end()));
    return repository.find(content).has_value();
}

/// The row of `values` in the pool `relation`, stored where no layer holds it (the repository's one insert: under its
/// relation, urd::insert_at): its view, and whether it was stored now.
template<typename F, typename Repository>
std::pair<RowView<F, Repository>, bool> insert_values(Repository& repository, urd::index<F> relation, std::initializer_list<ygg::Value> values)
{
    auto content = urd::data<Row<F>>(relation, urd::flat_vector<ygg::Value>(values.begin(), values.end()));
    return repository.insert(content);
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
    auto records = ygg::RowRecords<ygg::Schema, ygg::FreeRelationRepository>();
    const auto columns = std::array { urd::data<ygg::ColumnLayout> { urd::index<ygg::Column>(0), ygg::TypeId(0) },
                                      urd::data<ygg::ColumnLayout> { urd::index<ygg::Column>(1), ygg::TypeId(0) } };
    auto relation = ygg::FreeRelationBuilder();
    relation.initialize(repository, repository.schema(columns), columns);
    const auto row = [](ygg::Value a, ygg::Value b) { return std::array { a, b }; };
    EXPECT_EQ(relation.insert(repository, records, row(2, 1)), std::pair(std::size_t { 0 }, true));
    EXPECT_EQ(relation.insert(repository, records, row(1, 5)), std::pair(std::size_t { 0 }, true));
    EXPECT_EQ(relation.insert(repository, records, row(1, 7)), std::pair(std::size_t { 1 }, true));
    EXPECT_EQ(relation.insert(repository, records, row(1, 5)), std::pair(std::size_t { 0 }, false));

    const auto built = repository.view(relation);
    EXPECT_EQ(built.arity(), 2U);
    EXPECT_EQ(built.get_rows().size(), 3U);
    EXPECT_TRUE(built.contains(row(1, 7)));
    EXPECT_FALSE(built.contains(row(7, 1)));
    EXPECT_EQ(built.find(row(2, 1)), std::optional<std::size_t>(2));

    const auto renamed = std::array { urd::data<ygg::ColumnLayout> { urd::index<ygg::Column>(2), ygg::TypeId(0) },
                                      urd::data<ygg::ColumnLayout> { urd::index<ygg::Column>(3), ygg::TypeId(0) } };
    relation.rename(repository, renamed);
    EXPECT_EQ(repository.view(relation).columns()[1].label, urd::index<ygg::Column>(3));

    // The rows that start with a prefix, in a builder (its set searches itself) and once interned (searched from outside).
    const auto starting = [](const auto& view, ygg::Value first)
    {
        auto seconds = std::vector<ygg::Value>();
        for (const auto& held : view.with_prefix(std::array { first }))
            seconds.push_back(ygg::with_values(held, [](const auto& values) { return *std::ranges::next(std::ranges::begin(values)); }));
        return seconds;
    };
    EXPECT_EQ(starting(built, 1), (std::vector<ygg::Value> { 5, 7 }));
    EXPECT_EQ(starting(built, 2), (std::vector<ygg::Value> { 1 }));
    EXPECT_TRUE(starting(built, 3).empty());

    auto storage = ygg::FreeRelationStorage();
    const auto [interned, created] = repository.insert(relation, storage);
    EXPECT_TRUE(created);
    EXPECT_EQ(starting(interned, 1), (std::vector<ygg::Value> { 5, 7 }));

    // A record of the same rows, in another order: not canonical, until the repository's one insert makes it so.
    auto reversed = urd::flat_vector<urd::key<ygg::Schema, ygg::Row<ygg::Schema>>>();
    for (std::size_t i = interned.get_rows().size(); i-- > 0;)
        reversed.push_back(interned.row(i).index());
    auto record = urd::data<ygg::Relation<ygg::Schema>>(std::get<0>(relation.pool), std::get<0>(relation.columns), std::move(reversed));
    EXPECT_FALSE(ygg::is_canonical(record, repository));
    const auto [same, same_created] = repository.insert(record);
    EXPECT_TRUE(ygg::is_canonical(record, repository));
    EXPECT_FALSE(same_created);
    EXPECT_EQ(same, interned);
    EXPECT_EQ(interned.get_rows().size(), 3U);
    EXPECT_EQ(interned.find(row(1, 5)), std::optional<std::size_t>(0));
    EXPECT_EQ(interned.find(row(2, 1)), std::optional<std::size_t>(2));

    EXPECT_TRUE(relation.erase(repository, row(1, 7)));
    EXPECT_FALSE(relation.erase(repository, row(1, 7)));
    EXPECT_EQ(repository.view(relation).get_rows().size(), 2U);
}

/// A repository interns a view of a record of its kinds as a child resolves to it, and finds it likewise: a builder's
/// view as that builder, an interned view as itself (another repository's: std::invalid_argument).
TEST(YggdrasilTests, RepositoryInternsAndFindsBuilderAndInternedViews)
{
    auto repository = ygg::FreeRelationRepository();
    auto storage = ygg::FreeRelationStorage();
    const auto layouts = std::array { urd::data<ygg::ColumnLayout> { urd::index<ygg::Column>(0), ygg::TypeId(0) },
                                      urd::data<ygg::ColumnLayout> { urd::index<ygg::Column>(1), ygg::TypeId(0) } };
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
    const auto columns = std::array { urd::data<ygg::ColumnLayout> { urd::index<ygg::Column>(0), ygg::TypeId(0) },
                                      urd::data<ygg::ColumnLayout> { urd::index<ygg::Column>(1), ygg::TypeId(0) } };
    const ygg::FreeScratchRelation* first = nullptr;
    {
        auto relation = scratch.draw(columns);
        first = relation.get();
        relation->insert(std::array<ygg::Value, 2> { 3, 4 });
        relation->insert(std::array<ygg::Value, 2> { 1, 2 });
        const auto view = relation->view();
        EXPECT_EQ(view.get_rows().size(), 2U);
        EXPECT_EQ(view.find(std::array<ygg::Value, 2> { 1, 2 }), std::optional<std::size_t>(0));
    }
    auto again = scratch.draw(columns);
    EXPECT_EQ(again.get(), first);  // its slot, reused for the same arity
    EXPECT_EQ(again->view().get_rows().size(), 0U);
    auto narrower = scratch.draw(std::span(columns).first(1));
    EXPECT_NE(narrower.get(), first);  // another arity: a slot of its own
    EXPECT_EQ(narrower->view().arity(), 1U);
    narrower->insert(std::array<ygg::Value, 1> { 9 });
    EXPECT_TRUE(narrower->view().contains(std::array<ygg::Value, 1> { 9 }));
}

static_assert(urd::Sees<ygg::FreeRelationRepository>);

/// A layer sees itself and the layers it lies over: their rows are its own (urd::copy gives their keys); a sibling's row is
/// stored in it, its Schema (the parent's) as it is.
TEST(YggdrasilTests, CopyKeepsWhatItSeesAndStoresASiblingsRow)
{
    auto storage = ygg::FreeRelationStorage();
    auto parent = ygg::FreeRelationRepository();
    const auto pool = parent.schema(std::array { ygg::type_id<std::uint32_t> });
    auto source = ygg::FreeRelationRepository(&parent);
    auto target = ygg::FreeRelationRepository(&parent);
    EXPECT_TRUE(source.sees(parent));
    EXPECT_TRUE(source.sees(source));
    EXPECT_FALSE(parent.sees(source));
    EXPECT_FALSE(target.sees(source));

    const auto in_parent = insert_values(parent, pool, { ygg::encode(std::uint64_t { 5 }, ygg::type_id<std::uint32_t>) }).first;
    EXPECT_EQ(urd::copy(in_parent, source, storage), in_parent.index());  // seen: its key, nothing stored
    EXPECT_EQ(source.size(pool), 1U);

    const auto in_source = insert_values(source, pool, { ygg::encode(std::uint64_t { 6 }, ygg::type_id<std::uint32_t>) }).first;
    const auto copied = urd::copy(in_source, target, storage);
    EXPECT_EQ(copied.get<ygg::Schema>(), pool);
    EXPECT_EQ(target.view(copied).get<std::uint32_t>(0), 6U);
    EXPECT_EQ(&target.view(copied).context(), &target);
    EXPECT_EQ(urd::copy(target.view(copied), target, storage), copied);
}

/// A relation is copied, a builder's or an interned one, into another repository: its Schema, columns and rows interned
/// there; copied again from there, it is its own.
TEST(YggdrasilTests, CopyWritesARelationIntoAnotherRepository)
{
    auto storage = ygg::FreeRelationStorage();
    auto scratch = ygg::FreeScratchRelations();
    const auto columns = std::array { urd::data<ygg::ColumnLayout> { urd::index<ygg::Column>(0), ygg::TypeId(0) },
                                      urd::data<ygg::ColumnLayout> { urd::index<ygg::Column>(1), ygg::TypeId(0) } };
    auto relation = scratch.draw(columns);
    relation->insert(std::array<ygg::Value, 2> { 3, 4 });
    relation->insert(std::array<ygg::Value, 2> { 1, 2 });

    auto target = ygg::FreeRelationRepository();
    const auto copied = target.view(urd::copy(relation->view(), target, storage));
    EXPECT_EQ(copied.get_rows().size(), 2U);
    EXPECT_TRUE(copied.contains(std::array<ygg::Value, 2> { 1, 2 }));
    EXPECT_TRUE(copied.contains(std::array<ygg::Value, 2> { 3, 4 }));
    EXPECT_EQ(copied.columns()[1].label, urd::index<ygg::Column>(1));
    EXPECT_EQ(urd::copy(copied, target, storage), copied.index());

    auto other = ygg::FreeRelationRepository();
    const auto again = other.view(urd::copy(copied, other, storage));
    EXPECT_EQ(again.get_rows().size(), 2U);
    EXPECT_TRUE(again.contains(std::array<ygg::Value, 2> { 3, 4 }));
    EXPECT_EQ(&again.context(), &other);
}

using SchemaRowBuilderView = decltype(std::declval<const ygg::FreeRelationRepository&>().view(
    std::declval<const urd::builder<ygg::Row<ygg::Schema>, ygg::FreeRelationRepository>&>()));
using SchemaOwnedRowView =
    decltype(urd::resolve(std::declval<const urd::data<ygg::Row<ygg::Schema>>&>(), std::declval<const ygg::FreeRelationRepository&>()));
using SchemaStoredRowView = decltype(std::declval<ygg::FreeRelationRepository&>().insert(std::declval<urd::data<ygg::Row<ygg::Schema>>&>()).first);
static_assert(!ygg::TypedRowViewConcept<SchemaRowBuilderView, ygg::Schema>, "a builder's relation may not be interned: it reads untyped only");
static_assert(ygg::TypedRowViewConcept<SchemaOwnedRowView, ygg::Schema>);
static_assert(ygg::TypedRowViewConcept<SchemaStoredRowView, ygg::Schema>);
static_assert(ygg::RowViewConcept<SchemaRowBuilderView, ygg::Schema>);  // read untyped

/// A row reads typed only over an interned relation (an owned row's, a stored row's), from the column types its record
/// stores; a builder, its relation built or interned, reads untyped only.
TEST(YggdrasilTests, RowBuilderReadsItsTypesFromItsRelationInEachForm)
{
    auto repository = ygg::FreeRelationRepository();
    auto schemas = urd::Store<urd::slots<urd::builder<ygg::Schema, ygg::FreeRelationRepository>>>();
    auto row = urd::builder<ygg::Row<ygg::Schema>, ygg::FreeRelationRepository>();
    auto& schema = urd::builder_of(row.relation, schemas);
    schema.column_types.push_back(ygg::type_id<bool>);
    schema.column_types.push_back(ygg::type_id<std::uint32_t>);
    row.values.push_back(ygg::encode(true, ygg::type_id<bool>));
    row.values.push_back(ygg::encode(std::uint64_t { 7 }, ygg::type_id<std::uint32_t>));

    const auto built = repository.view(row);
    EXPECT_EQ(built.value(1), 7U);
    EXPECT_EQ(std::ranges::size(built.get_values()), 2U);
    EXPECT_THROW(static_cast<void>(built.value(2)), std::out_of_range);

    const auto relation = repository.schema(std::array { ygg::type_id<bool>, ygg::type_id<std::uint32_t> });
    row.relation = relation;
    EXPECT_EQ(repository.view(row).value(0), 1U);

    auto owned = urd::data<ygg::Row<ygg::Schema>>(relation, urd::flat_vector<ygg::Value>(row.values.begin(), row.values.end()));
    const auto held = urd::resolve(owned, repository);
    EXPECT_EQ(held.type(1), ygg::type_id<std::uint32_t>);
    EXPECT_TRUE(held.get<bool>(0));
    EXPECT_THROW(static_cast<void>(held.type(2)), std::out_of_range);
    EXPECT_THROW(static_cast<void>(held.get<bool>(1)), std::invalid_argument);

    const auto stored = repository.insert(owned).first;
    EXPECT_EQ(stored.type(0), ygg::type_id<bool>);
    EXPECT_EQ(stored.get<std::uint32_t>(1), 7U);
    EXPECT_EQ(std::ranges::distance(stored.get<std::uint32_t>(1, 2)), 1);
    EXPECT_THROW(static_cast<void>(stored.get<bool>(0, 2)), std::invalid_argument);
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
                                    || requires(const Repository& repository, const urd::data<Row<T>>& data) { repository.find(data); }
                                    || requires(const Repository& repository, urd::key<T, Row<T>> index) { repository[index]; }
                                    || requires(const Repository& repository, urd::key<T, Row<T>> index) { repository.owner(index); }
                                    || requires(const Repository& repository, urd::index<T> index) { repository.size(index); }
                                    || requires(Repository& repository) { repository.template get<T>(); };
static_assert(!CanUseUnsupportedRelation<ContractRelations, RepositoryTypesElement>);
static_assert(!CanUseUnsupportedRelation<ContractConcurrentRelations, RepositoryTypesElement>);
static_assert(!CanUseUnsupportedRelation<ContractRepository, RepositoryTypesElement>);
static_assert(!Viewable<urd::key<RepositoryTypesElement, Row<RepositoryTypesElement>>, ContractRepository>);

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
    static_assert(Viewable<urd::key<RepositoryTypesRelation, Row<RepositoryTypesRelation>>, Repository>);
    static_assert(Viewable<urd::key<RepositoryTypesElement, Row<RepositoryTypesElement>>, Repository>);

    Repository repository;
    const auto relation = declared<RepositoryTypesRelation>(repository, 1);
    const auto element = declared<RepositoryTypesElement>(repository, 2);
    const auto first = insert_values(repository, relation, { ygg::encode_value(ObjectIndex(3)) }).first;
    const auto second = insert_values(repository, element, { ygg::encode_value(ObjectIndex(7)), ygg::encode_value(ObjectIndex(9)) }).first;
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

/// Whether a row view resolves its columns of T, in each form: one column, a range of columns, every column.
template<typename V, typename T>
concept ResolvesColumn = requires(const V& view) { view.template resolve<T>(std::size_t { 0 }); };
template<typename V, typename T>
concept ResolvesColumnRange = requires(const V& view) { view.template resolve<T>(std::size_t { 0 }, std::size_t { 1 }); };
template<typename V, typename T>
concept ResolvesEveryColumn = requires(const V& view) { view.template resolve<T>(); };

using ObjectRowRepository = formalism::Repository<formalism::SymbolRepository<formalism::Object, RepositoryTypesRelation>,
                                                  formalism::RelationRepository<urd::word, RepositoryTypesRelation>>;
using ObjectRow = RowView<RepositoryTypesRelation, ObjectRowRepository>;
static_assert(ResolvesColumn<ObjectRow, formalism::Object> && ResolvesColumnRange<ObjectRow, formalism::Object>
              && ResolvesEveryColumn<ObjectRow, formalism::Object>);
using FreeRow = RowView<Schema, ygg::FreeRelationRepository>;
static_assert(!ResolvesColumn<FreeRow, formalism::Object> && !ResolvesColumnRange<FreeRow, formalism::Object>
              && !ResolvesEveryColumn<FreeRow, formalism::Object>);
static_assert(!ResolvesColumn<RowBuilderView<Schema, ygg::FreeRelationRepository>, formalism::Object>);

using ObjectRowStorage = formalism::BuilderStorage<ObjectRowRepository, formalism::Object, RepositoryTypesRelation, Row<RepositoryTypesRelation>>;

/// A row copied between sibling layers whose own objects differ names the same objects: each object column's object is
/// copied as a child (a parent's kept, the source layer's interned in the target by its fields), however the layers
/// numbered their own.
TEST(YggdrasilTests, CopyOfARowNamesTheSameObjectsInASiblingLayer)
{
    using ObjectIndex = urd::index<formalism::Object>;
    auto storage = ObjectRowStorage();
    auto parent = ObjectRowRepository();
    const auto relation = declared<RepositoryTypesRelation>(parent, 2);
    auto shared = urd::data<formalism::Object>(1);
    const auto in_parent = parent.insert(shared).first.index();
    auto source = ObjectRowRepository(&parent);
    auto target = ObjectRowRepository(&parent);
    auto own = urd::data<formalism::Object>(2);
    const auto in_source = source.insert(own).first.index();
    auto other = urd::data<formalism::Object>(7);
    EXPECT_EQ(target.insert(other).first.index(), in_source);  // the target's own object takes the source's own index

    const auto row = insert_values(source, relation, { ygg::encode_value(in_parent), ygg::encode_value(in_source) }).first;
    const auto copied = target.view(urd::copy(row, target, storage));
    EXPECT_EQ(&copied.context(), &target);
    EXPECT_EQ(copied.template resolve<formalism::Object>(0).index(), in_parent);
    EXPECT_EQ(copied.template resolve<formalism::Object>(0).get_value(), 1U);
    EXPECT_EQ(copied.template resolve<formalism::Object>(1).get_value(), 2U);
    EXPECT_NE(copied.template get<ObjectIndex>(1), in_source);
    EXPECT_EQ(urd::copy(copied, target, storage), copied.index());
}

static_assert(urd::SeesAll<ObjectRowRepository, formalism::Object>);

/// A layer sees every object a sibling can name where the sibling holds none of its own; a layer it lies over, always.
/// A row of such a sibling is copied with its values as they are.
TEST(YggdrasilTests, SeesAllObjectsOfASiblingWithoutItsOwn)
{
    auto storage = ObjectRowStorage();
    auto parent = ObjectRowRepository();
    const auto relation = declared<RepositoryTypesRelation>(parent, 1);
    auto shared = urd::data<formalism::Object>(1);
    const auto in_parent = parent.insert(shared).first.index();
    auto source = ObjectRowRepository(&parent);
    auto target = ObjectRowRepository(&parent);
    EXPECT_TRUE(target.sees_all<formalism::Object>(source));

    const auto row = insert_values(source, relation, { ygg::encode_value(in_parent) }).first;
    const auto copied = target.view(urd::copy(row, target, storage));
    EXPECT_EQ(copied.template resolve<formalism::Object>(0).index(), in_parent);
    EXPECT_EQ(target.own_size<formalism::Object>(), 0U);

    auto own = urd::data<formalism::Object>(2);
    source.insert(own);
    EXPECT_EQ(source.own_size<formalism::Object>(), 1U);
    EXPECT_FALSE(target.sees_all<formalism::Object>(source));

    auto child = ObjectRowRepository(&target);
    auto other = urd::data<formalism::Object>(7);
    child.insert(other);
    EXPECT_TRUE(child.sees_all<formalism::Object>(target));
    EXPECT_FALSE(target.sees_all<formalism::Object>(child));
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
    const auto relation = declared<RepositoryTypesRelation>(repository, 2);
    const auto stored = insert_values(repository, relation, { ygg::encode_value(b.index()), ygg::encode_value(a.index()) }).first;
    EXPECT_EQ(ygg::values_text<RepositoryTypesRelation>(stored), fmt::format("{} {}", b, a));
    EXPECT_EQ(stored.resolve<Object>(1), a);
    EXPECT_EQ(std::ranges::distance(stored.resolve<Object>(0, 1)), 1);
    EXPECT_EQ(*stored.resolve<Object>().begin(), b);
}

namespace
{
/// A repository whose domain prepares its elements (a hook of its own, counting its calls).
using PreparingRepository = formalism::Repository<formalism::SymbolRepository<RepositoryTypesElement, formalism::Object>,
                                                  formalism::RelationRepository<urd::bit, RepositoryTypesRelation>>;
std::size_t preparations = 0;

void prepare_for_insert(PreparingRepository& /*repository*/, urd::data<RepositoryTypesElement>& data)
{
    ++preparations;
    data.value %= 10;
}
}  // namespace

template<typename Repository, typename T>
concept CanIntern = requires(Repository& repository, urd::data<T>& data) { repository.insert(data); };
// A row is interned as any record: its one insert stores it under the relation it holds.
static_assert(CanIntern<ContractRepository, ContractBinding>);
// A record its domain does not prepare is stored as it is (the no-op default hook).
static_assert(CanIntern<ContractRepository, RepositoryTypesElement>);

TEST(YggdrasilTests, CommonInterningPreparesExactlyOnceOnEveryInsert)
{
    auto repository = PreparingRepository();
    preparations = 0;
    auto data = urd::data<RepositoryTypesElement>();
    data.value = 17;
    const auto [first, created] = repository.insert(data);  // the one public insert prepares: no path skips it
    EXPECT_TRUE(created);
    EXPECT_EQ(first.get_value(), 7);
    EXPECT_EQ(data.index, first.index());
    EXPECT_EQ(preparations, 1);

    data.value = 27;
    data.index = urd::index<RepositoryTypesElement>(99);
    const auto [duplicate, duplicate_created] = repository.insert(data);
    EXPECT_FALSE(duplicate_created);
    EXPECT_EQ(duplicate.index(), first.index());
    EXPECT_EQ(data.index, first.index());
    EXPECT_EQ(preparations, 2);
}

TEST(YggdrasilTests, CommonInterningAcceptsRowsWithoutSymbolIdentity)
{
    auto repository = ContractRepository();
    const auto relation = declared<RepositoryTypesRelation>(repository, 1);
    using Object = formalism::Object;
    const auto [first, created] = insert_values(repository, relation, { ygg::encode_value(urd::index<Object>(3)) });
    EXPECT_TRUE(created);
    const auto [duplicate, duplicate_created] = insert_values(repository, relation, { ygg::encode_value(urd::index<Object>(3)) });
    EXPECT_FALSE(duplicate_created);
    EXPECT_EQ(duplicate.index(), first.index());
    EXPECT_EQ(first.template get<urd::index<Object>>(0), urd::index<Object>(3));
    EXPECT_EQ((urd::as_member<urd::key<RepositoryTypesRelation, Row<RepositoryTypesRelation>>>(first)), first.index());  // a row's view written as its key
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
    const auto object = ygg::encode_value(urd::index<Object>(0));
    const auto [first, first_created] = insert_values(first_repository, relation, { object });
    const auto [second, second_created] = insert_values(second_repository, relation, { object });
    const auto [independent, independent_created] = insert_values(independent_repository, relation, { object });

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
    const auto key = [](const auto& row)
    {
        const auto values = row.get_values();
        return std::pair(row.get_relation().index(), std::vector<ygg::Value>(values.begin(), values.end()));
    };
    EXPECT_FALSE(urd::Less<> {}(key(first), key(second)));
    EXPECT_FALSE(urd::Less<> {}(key(second), key(first)));

    const auto [larger, larger_created] = insert_values(second_repository, relation, { ygg::encode_value(urd::index<Object>(1)) });
    ASSERT_TRUE(larger_created);
    EXPECT_TRUE(urd::Less<> {}(key(first), key(larger)));
}

template<typename V>
concept HasPublishedRowKey = requires(const V& view) { view.index(); };

using RowViewRepository = formalism::Repository<formalism::SymbolRepository<RepositoryTypesRelation, formalism::Object>, ContractRelations>;
using BorrowedContractRowView = RowBuilderView<RepositoryTypesRelation, RowViewRepository>;
using OwnedContractRowView = urd::borrowed_view<ContractBinding, RowViewRepository>;
static_assert(TypedRowViewConcept<OwnedContractRowView, RepositoryTypesRelation>);  // an owned row reads typed through its relation
using PublishedContractRowView = RowView<RepositoryTypesRelation, RowViewRepository>;
static_assert(RowViewConcept<BorrowedContractRowView, RepositoryTypesRelation>);
static_assert(!TypedRowViewConcept<BorrowedContractRowView, RepositoryTypesRelation>);  // a builder reads untyped only
static_assert(TypedRowViewConcept<PublishedContractRowView, RepositoryTypesRelation>);
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
static_assert(!HasPublishedRowKey<BorrowedContractRowView>);
static_assert(HasPublishedRowKey<PublishedContractRowView>);
static_assert(std::same_as<decltype(std::declval<const PublishedContractRowView&>().index()), urd::key<RepositoryTypesRelation, Row<RepositoryTypesRelation>>>);
static_assert(std::same_as<urd::record_of<BorrowedContractRowView>, urd::builder<ContractBinding, RowViewRepository>>);
static_assert(std::same_as<decltype(std::declval<const RowViewRepository&>()[urd::key<RepositoryTypesRelation, ContractBinding>()]), PackedRow<RepositoryTypesRelation>>);

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
    auto built = urd::builder<ContractBinding, RowViewRepository>();
    built.relation = relation;
    built.values.push_back(ygg::encode_value(urd::index<Object>(2)));
    built.values.push_back(ygg::encode_value(urd::index<Object>(5)));
    const auto borrowed = repository.view(built);
    EXPECT_EQ(&borrowed.get_values(), &built.values);
    EXPECT_EQ(&borrowed.context(), &repository);
    EXPECT_EQ(borrowed.value(1), 5U);  // a builder reads untyped only
    auto content = urd::data<ContractBinding>(relation, urd::flat_vector<ygg::Value>(built.values.begin(), built.values.end()));
    EXPECT_FALSE(repository.find(content).has_value());

    // An owned row reads typed through its relation, as the builder does.
    const auto owned = urd::resolve(content, repository);
    EXPECT_EQ(owned.value(1), 5U);
    EXPECT_EQ(owned.type(0), ygg::type_id<urd::index<Object>>);
    EXPECT_EQ(owned.template get<urd::index<Object>>(1), urd::index<Object>(5));
    EXPECT_EQ(&owned.get_values(), &content.values);

    const auto [published, created] = repository.insert(content);
    ASSERT_TRUE(created);
    EXPECT_EQ(content.index, urd::get<ContractBinding>(published.index()));  // the row holds its whole key: its id written in
    const auto key = published.index();
    EXPECT_EQ(&published.context(), &repository);
    EXPECT_TRUE(std::ranges::equal(published.get_values(), borrowed.get_values()));
    EXPECT_EQ(repository.find(content)->index(), key);
    const auto [duplicate, duplicate_created] = repository.insert(content);
    EXPECT_FALSE(duplicate_created);
    EXPECT_EQ(duplicate.index(), key);

    // A returned object range borrows the owned row's values directly; its objects resolve.
    EXPECT_EQ(owned.template get<urd::index<Object>>()[1], urd::index<Object>(5));
    const auto objects = urd::resolve(owned.template get<urd::index<Object>>(), repository);
    content.values[0] = ygg::encode_value(urd::index<Object>(7));
    EXPECT_EQ(objects[0].index(), urd::index<Object>(7));
    EXPECT_EQ(published.type(0), ygg::type_id<urd::index<Object>>);
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
    const auto data = make_bound(relation, objects);
    const auto [view, created] = insert_bound(repository, data);
    ASSERT_TRUE(created);

    auto wrong_objects = urd::flat_vector<ygg::Value> {};
    wrong_objects.push_back(ygg::encode_value(urd::index<Object>(0)));
    const auto wrong = make_bound(relation, wrong_objects);
    EXPECT_FALSE(holds_bound(repository, wrong));
    EXPECT_THROW(insert_bound(repository, wrong), std::invalid_argument);
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
    const auto data = make_bound(declared<RepositoryTypesRelation>(repository, 2), objects);
    insert_bound(repository, data);

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
    const auto data = make_bound(relation, objects);

    EXPECT_EQ(root.row_memory_usage<RepositoryTypesPackedRelation>(), 0);

    const auto [root_view, root_created] = insert_bound(root, data);
    const auto [duplicate_view, duplicate_created] = insert_bound(root, data);

    EXPECT_TRUE(root_created);
    EXPECT_FALSE(duplicate_created);
    EXPECT_GT(root.row_memory_usage<RepositoryTypesPackedRelation>(), 0);
    EXPECT_EQ(ygg::tests::row_of(duplicate_view), ygg::tests::row_of(root_view));
    EXPECT_EQ(root_view.template get<urd::index<Object>>(0), urd::index<Object>(0));
    EXPECT_EQ(root_view.template get<urd::index<Object>>(1), urd::index<Object>(3));

    objects[0] = ygg::encode_value(urd::index<Object>(4));
    objects[1] = ygg::encode_value(urd::index<Object>(0));
    const auto wider_data = make_bound(relation, objects);

    EXPECT_THROW(insert_bound(root, wider_data), std::out_of_range);
    EXPECT_EQ(root.size(relation), 1);

    const auto [root_duplicate_after_failure, created_after_failure] = insert_bound(root, data);
    EXPECT_FALSE(created_after_failure);
    EXPECT_EQ(ygg::tests::row_of(root_duplicate_after_failure), ygg::tests::row_of(root_view));

    auto inherited_width_child = Repository(2, &root);
    objects[0] = ygg::encode_value(urd::index<Object>(3));
    const auto inherited_width_data = make_bound(relation, objects);
    const auto [inherited_width_view, inherited_width_created] = insert_bound(inherited_width_child, inherited_width_data);
    EXPECT_TRUE(inherited_width_created);
    EXPECT_EQ(inherited_width_view.template get<urd::index<Object>>(0), urd::index<Object>(3));

    auto child = std::make_shared<Repository>(5, &root);
    const auto [inherited_view, inherited_created] = insert_bound(*child, data);
    const auto [child_view, child_created] = insert_bound(*child, wider_data);

    EXPECT_FALSE(inherited_created);
    EXPECT_EQ(ygg::tests::row_of(inherited_view), ygg::tests::row_of(root_view));
    EXPECT_EQ(&inherited_view.context(), &root);
    EXPECT_TRUE(child_created);
    EXPECT_EQ(ygg::tests::row_of(child_view), urd::index<Row<RepositoryTypesPackedRelation>>(1));
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

    EXPECT_THROW((void) repository.at(urd::index<RepositoryTypesElement>(0)), std::out_of_range);
    EXPECT_EQ(repository.owner(urd::index<RepositoryTypesElement>(0)), nullptr);
    EXPECT_THROW(static_cast<void>(repository.view(urd::index<RepositoryTypesElement>(0))), std::out_of_range);

    auto data = urd::data<RepositoryTypesElement> {};
    data.value = 7;
    const auto [view, created] = repository.insert(data);

    EXPECT_TRUE(created);
    EXPECT_EQ(repository[view.index()]->value, 7);
    EXPECT_EQ(repository.owner(view.index()), &repository);
    EXPECT_THROW((void) repository.at(urd::index<RepositoryTypesElement>(1)), std::out_of_range);
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
    const auto missing = urd::key<RepositoryTypesRelation, Row<RepositoryTypesRelation>> { relation, urd::index<Row<RepositoryTypesRelation>>(0) };

    EXPECT_THROW(static_cast<void>(repository.at(missing)), std::out_of_range);
    EXPECT_EQ(repository.owner(missing), nullptr);
    EXPECT_THROW(static_cast<void>(urd::resolve(missing, repository)), std::out_of_range);
    const auto rows = std::array { urd::index<Row<RepositoryTypesRelation>>(0) };
    const auto bindings = rows_view(relation, rows, repository);
    EXPECT_THROW((void) bindings[0], std::out_of_range);

    const auto [view, created] = insert_values(repository, relation, { ygg::encode_value(urd::index<Object>(0)), ygg::encode_value(urd::index<Object>(1)) });

    EXPECT_TRUE(created);
    EXPECT_EQ(view.get_values().size(), 2);
    EXPECT_EQ(repository.owner(view.index()), &repository);

    const auto missing_row = urd::key<RepositoryTypesRelation, Row<RepositoryTypesRelation>> { relation, urd::index<Row<RepositoryTypesRelation>>(1) };
    const auto missing_relation = urd::key<RepositoryTypesRelation, Row<RepositoryTypesRelation>> { urd::index<RepositoryTypesRelation>(1), urd::index<Row<RepositoryTypesRelation>>(0) };
    EXPECT_THROW(static_cast<void>(repository.at(missing_row)), std::out_of_range);
    EXPECT_THROW(static_cast<void>(urd::resolve(missing_row, repository)), std::out_of_range);
    EXPECT_THROW(static_cast<void>(repository.at(missing_relation)), std::out_of_range);
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
    using Handle = urd::key<RepositoryTypesRelation, Row<RepositoryTypesRelation>>;
    using StoredRow = RowView<RepositoryTypesRelation, Repository>;
    using Object = formalism::Object;
    auto root = Repository();
    const auto relation = declared<RepositoryTypesRelation>(root, 1);
    auto objects = urd::flat_vector<ygg::Value> {};
    objects.push_back(ygg::encode_value(urd::index<Object>(0)));
    const auto root_view = insert_bound(root, make_bound(relation, objects)).first;
    auto child = Repository(&root);
    objects[0] = ygg::encode_value(urd::index<Object>(1));
    const auto child_view = insert_bound(child, make_bound(relation, objects)).first;
    auto grandchild = Repository(&child);
    objects[0] = ygg::encode_value(urd::index<Object>(2));
    const auto local_view = insert_bound(grandchild, make_bound(relation, objects)).first;

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
    const auto unrelated_view = insert_bound(unrelated, make_bound(relation, objects)).first;
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
