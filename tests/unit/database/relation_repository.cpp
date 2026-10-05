/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/relation_repository.hpp"

#include "yggdrasil/database/operations.hpp"
#include "yggdrasil/database/relation_pool.hpp"
#include "yggdrasil/semantics/equal_to.hpp"
#include "yggdrasil/semantics/hash.hpp"

#include <algorithm>
#include <array>
#include <cista/serialization.h>
#include <concepts>
#include <gtest/gtest.h>
#include <iterator>
#include <limits>
#include <ranges>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace ygg::tests
{
struct DatabaseInternedValue
{
    uint_t value;
};
}  // namespace ygg::tests

namespace ygg
{
template<>
struct Hash<tests::DatabaseInternedValue>
{
    hash_t operator()(const tests::DatabaseInternedValue& value) const noexcept { return value.value % 10; }
};

template<>
struct EqualTo<tests::DatabaseInternedValue>
{
    bool operator()(const tests::DatabaseInternedValue& lhs, const tests::DatabaseInternedValue& rhs) const noexcept
    {
        return lhs.value % 10 == rhs.value % 10;
    }
};
}  // namespace ygg

namespace ygg::tests
{
using namespace database;
using ColumnIndex = Index<database::Column>;

namespace
{
using RelationTag = Relation<>;
using RelationBuilder = Builder<RelationTag>;
using Repository = RelationRepository<>;
using BuilderView = View<RelationBuilder, Repository>;
using DataView = View<Data<RelationTag>, Repository>;
using IndexView = View<Index<RelationTag>, Repository>;
using ColumnsIndex = Index<Columns>;
using RowIndex = Index<RelationRow<uint_t>>;
using RowSetIndex = Index<RelationRowSet<uint_t>>;
using ColumnsBuilderView = View<Builder<Columns>, Repository>;
using ColumnsDataView = View<Data<Columns>, Repository>;
using ColumnsIndexView = View<ColumnsIndex, Repository>;

struct RelationContext
{
    const Repository& repository;
    friend const Repository& get_relation_repository(const RelationContext& context) noexcept { return context.repository; }
    friend const Repository& get_repository(const RelationContext& context) noexcept { return context.repository; }
};

static_assert(std::same_as<IndexView, RelationView<>>);
static_assert(RelationViewConcept<BuilderView>);
static_assert(RelationViewConcept<DataView>);
static_assert(RelationViewConcept<IndexView>);
static_assert(std::ranges::random_access_range<BuilderView>);
static_assert(std::ranges::random_access_range<DataView>);
static_assert(std::ranges::random_access_range<IndexView>);
static_assert(std::ranges::borrowed_range<BuilderView>);
static_assert(std::ranges::borrowed_range<DataView>);
static_assert(std::ranges::borrowed_range<IndexView>);
static_assert(!std::ranges::contiguous_range<IndexView>);
static_assert(ViewConcept<RelationBuilder, Repository>);
static_assert(ViewConcept<Data<RelationTag>, Repository>);
static_assert(ViewConcept<Index<RelationTag>, Repository>);
static_assert(ViewConcept<Builder<Columns>, Repository>);
static_assert(ViewConcept<Data<Columns>, Repository>);
static_assert(ViewConcept<Index<Columns>, Repository>);
static_assert(std::same_as<Repository::SymbolTypes, TypeList<Columns, RelationTag>>);
static_assert(formalism::SupportsSymbol<Repository, Columns>);
static_assert(formalism::SupportsSymbol<Repository, RelationTag>);
static_assert(!formalism::SupportsSymbol<Repository, Column>);
static_assert(!formalism::SupportsSymbol<Repository, Relation<double>>);

static_assert(std::same_as<typename BuilderView::ElementType, uint_t>);
static_assert(std::same_as<typename DataView::ElementType, uint_t>);
static_assert(std::same_as<typename IndexView::ElementType, uint_t>);
static_assert(std::same_as<decltype(std::declval<const BuilderView&>().get_handle()), const RelationBuilder&>);
static_assert(std::same_as<decltype(std::declval<const DataView&>().get_handle()), const Data<RelationTag>&>);
static_assert(std::same_as<decltype(std::declval<const IndexView&>().get_handle()), const Index<RelationTag>&>);
static_assert(std::same_as<decltype(std::declval<const IndexView&>().get_index()), Index<RelationTag>>);
static_assert(std::same_as<decltype(std::declval<const IndexView&>().get_data()), const Data<RelationTag>&>);
static_assert(uses_trivial_storage_v<RelationTag>);
static_assert(!std::is_copy_constructible_v<RelationBuilder>);
static_assert(std::is_copy_constructible_v<IndexView>);
static_assert(std::same_as<decltype(Data<RelationTag>::columns_index), ColumnsIndex>);
static_assert(std::same_as<decltype(Data<RelationTag>::row_set_index), RowSetIndex>);
static_assert(std::same_as<decltype(std::declval<const IndexView&>().row_indices()), std::span<const RowIndex>>);
static_assert(!std::is_constructible_v<ColumnsIndex, RowIndex> && !std::is_constructible_v<RowIndex, RowSetIndex>
              && !std::is_constructible_v<RowSetIndex, ColumnsIndex>);
static_assert(!std::is_convertible_v<uint_t, ColumnsIndex> && !std::is_convertible_v<uint_t, RowIndex> && !std::is_convertible_v<uint_t, RowSetIndex>);
static_assert(!std::is_convertible_v<ColumnsIndex, uint_t> && !std::is_convertible_v<RowIndex, uint_t> && !std::is_convertible_v<RowSetIndex, uint_t>);
static_assert(sizeof(ColumnsIndex) == sizeof(uint_t) && sizeof(RowIndex) == sizeof(uint_t) && sizeof(RowSetIndex) == sizeof(uint_t));
static_assert(std::is_trivially_copyable_v<ColumnsIndex> && std::is_trivially_copyable_v<RowIndex> && std::is_trivially_copyable_v<RowSetIndex>);
static_assert(ColumnsViewConcept<Builder<Columns>>);
static_assert(ColumnsViewConcept<ColumnsBuilderView> && ColumnsViewConcept<ColumnsDataView> && ColumnsViewConcept<ColumnsIndexView>);
static_assert(std::same_as<decltype(std::declval<const ColumnsIndexView&>().get_handle()), const ColumnsIndex&>);
static_assert(std::same_as<decltype(std::declval<const ColumnsIndexView&>().get_data()), const Data<Columns>&>);

template<typename T, typename L, typename R>
concept CanJoinRelations = requires(const L& lhs, const R& rhs, Builder<Relation<T>>& out) {
    join(lhs, rhs, out);
    { join<T>(lhs, rhs) } -> std::same_as<Builder<Relation<T>>>;
};

static_assert(CanJoinRelations<uint_t, BuilderView, DataView>);
static_assert(CanJoinRelations<uint_t, IndexView, RelationBuilder>);
static_assert(!CanJoinRelations<uint_t, IndexView, Builder<Relation<double>>>);
static_assert(!CanJoinRelations<double, BuilderView, DataView>);

template<typename ViewType>
void expect_rows(const ViewType& view, std::initializer_list<std::initializer_list<uint_t>> rows)
{
    ASSERT_EQ(view.size(), rows.size());
    size_t index = 0;
    for (const auto& row : rows)
        EXPECT_TRUE(std::ranges::equal(view.at(index++), row));
}
}  // namespace

TEST(YggdrasilTests, DatabasePreparedInterningRetainsRawSchemaValidation)
{
    auto repository = RelationRepositoryFactory<>().create();
    auto invalid = Data<Columns>();
    invalid.values.push_back(ColumnIndex(3));
    invalid.values.push_back(ColumnIndex(3));
    EXPECT_THROW((void) database::insert(repository, invalid), std::invalid_argument);
    EXPECT_FALSE(repository.find(invalid));
}

TEST(YggdrasilTests, DatabaseColumnsInternOrderedSchemasThroughBuilderDataAndIndexViews)
{
    RelationRepositoryFactory<> factory;
    auto repository = factory.create();
    Builder<Columns> builder { ColumnIndex(8), ColumnIndex(3) };
    const auto [original, created] = insert(repository, builder);
    ASSERT_TRUE(created);
    EXPECT_EQ(builder.get_index(), original.get_index());
    EXPECT_EQ(repository.get_columns(original.get_index()).get_index(), original.get_index());

    Data<Columns> pending;
    assign(pending, builder);
    const auto builder_view = make_view(builder, repository);
    const auto data_view = make_view(pending, repository);
    const auto index_view = make_view(original.get_index(), repository);
    const std::array<ColumnIndex, 2> expected { ColumnIndex(8), ColumnIndex(3) };
    EXPECT_TRUE(std::ranges::equal(builder_view.span(), expected));
    EXPECT_TRUE(std::ranges::equal(data_view.span(), expected));
    EXPECT_TRUE(std::ranges::equal(index_view.span(), expected));
    EXPECT_EQ(index_view.column_index(ColumnIndex(3)), 1);
    EXPECT_EQ(&builder_view.get_handle(), &builder);
    EXPECT_EQ(&data_view.get_handle(), &pending);
    EXPECT_EQ(&index_view.get_data(), &repository[original.get_index()]);

    pending.index = Index<Columns>(99);
    const auto [duplicate, duplicate_created] = database::insert(repository, pending);
    EXPECT_FALSE(duplicate_created);
    EXPECT_EQ(pending.index, original.get_index());
    EXPECT_TRUE(EqualTo<ColumnsIndexView> {}(original, duplicate));
    EXPECT_EQ(Hash<ColumnsIndexView> {}(original), Hash<ColumnsIndexView> {}(duplicate));
    ASSERT_TRUE(repository.find(pending));
    EXPECT_EQ(repository.find(pending)->get_index(), original.get_index());

    const std::array<ColumnIndex, 2> reversed { ColumnIndex(3), ColumnIndex(8) };
    builder.assign(std::span(reversed));
    const auto [other_order, order_created] = insert(repository, builder);
    EXPECT_TRUE(order_created);
    EXPECT_NE(other_order.get_index(), original.get_index());

    RelationBuilder rows(original);
    rows.insert({ 10, 20 });
    const auto relation = insert(repository, rows).first;
    const JoinPlan joining(relation.columns(), relation.columns());
    const ProjectionPlan projecting(relation.columns(), other_order);
    RelationPool<> pool;
    auto output = pool.get_or_allocate(relation.columns());
    Workspace<> workspace;
    join(relation, relation, joining, *output, workspace);
    expect_rows(*output, { { 10, 20 } });
    output->initialize(other_order);
    project(relation, projecting, *output, workspace);
    expect_rows(*output, { { 20, 10 } });
    const auto projected = project<uint_t>(relation, relation.columns());
    expect_rows(projected, { { 10, 20 } });

    builder.assign(original);
    EXPECT_TRUE(builder.get_index().is_max());
    const auto [reused, reused_created] = insert(repository, builder);
    EXPECT_FALSE(reused_created);
    EXPECT_EQ(reused.get_index(), original.get_index());
    EXPECT_EQ(builder.get_index(), original.get_index());
    builder.clear();
    EXPECT_TRUE(builder.empty());
    EXPECT_TRUE(builder.get_index().is_max());
    EXPECT_TRUE(std::ranges::equal(original.span(), expected));
    EXPECT_TRUE(std::ranges::equal(other_order.span(), reversed));
}

TEST(YggdrasilTests, DatabaseRelationRepositoryInternsRowSetsWithOrderedSchemasAndNamespaces)
{
    RelationRepositoryFactory<> factory;
    auto repository = factory.create();
    RelationBuilder builder { { ColumnIndex(3), ColumnIndex(8) } };
    builder.insert({ 10, 11 });
    builder.insert({ 20, 21 });
    const auto [first, created] = insert(repository, builder, 7);
    ASSERT_TRUE(created);
    EXPECT_EQ(builder.get_index(), first.get_index());
    EXPECT_EQ(first.get_data().index, first.get_index());
    EXPECT_EQ(first.get_data().schema_namespace, 7);

    const auto [duplicate, duplicate_created] = insert(repository, builder, 7);
    EXPECT_FALSE(duplicate_created);
    EXPECT_TRUE(EqualTo<IndexView> {}(first, duplicate));
    EXPECT_EQ(Hash<IndexView> {}(first), Hash<IndexView> {}(duplicate));

    const auto [other_namespace, namespace_created] = insert(repository, builder, 8);
    EXPECT_TRUE(namespace_created);
    EXPECT_NE(first.get_index(), other_namespace.get_index());
    EXPECT_EQ(first.at(0).data(), other_namespace.at(0).data());

    const Builder<Columns> reordered_columns { ColumnIndex(8), ColumnIndex(3) };
    builder.rename(reordered_columns.span());
    const auto [other_schema, schema_created] = insert(repository, builder, 7);
    EXPECT_TRUE(schema_created);
    EXPECT_NE(first.get_index(), other_schema.get_index());
    EXPECT_EQ(first.at(0).data(), other_schema.at(0).data());
    EXPECT_EQ(first.columns()[0], ColumnIndex(3));
    EXPECT_EQ(first.columns()[1], ColumnIndex(8));

    const Builder<Columns> original_columns { ColumnIndex(3), ColumnIndex(8) };
    builder.initialize(original_columns.span());
    builder.insert({ 20, 21 });
    builder.insert({ 10, 11 });
    const auto [other_order, order_created] = insert(repository, builder, 7);
    EXPECT_FALSE(order_created);
    EXPECT_EQ(first.get_index(), other_order.get_index());
    expect_rows(first, { { 10, 11 }, { 20, 21 } });
    expect_rows(other_order, { { 10, 11 }, { 20, 21 } });
    EXPECT_EQ(repository.size(), 3);
}

TEST(YggdrasilTests, DatabaseRelationRepositoryDistinguishesNullaryTruthAndEmptySchemas)
{
    RelationRepositoryFactory<> factory;
    auto repository = factory.create();
    RelationBuilder nullary;
    const auto false_view = insert(repository, nullary).first;
    nullary.insert({});
    const auto true_view = insert(repository, nullary).first;
    EXPECT_NE(false_view.get_index(), true_view.get_index());
    EXPECT_EQ(false_view.arity(), 0);
    EXPECT_EQ(true_view.arity(), 0);
    EXPECT_TRUE(false_view.empty());
    EXPECT_EQ(true_view.size(), 1);
    EXPECT_TRUE(true_view.contains({}));

    RelationBuilder unary { { ColumnIndex(5) } };
    const auto empty_unary = insert(repository, unary).first;
    RelationBuilder binary { { ColumnIndex(5), ColumnIndex(6) } };
    const auto empty_binary = insert(repository, binary).first;
    EXPECT_NE(empty_unary.get_index(), false_view.get_index());
    EXPECT_NE(empty_binary.get_index(), empty_unary.get_index());
    EXPECT_EQ(empty_unary.arity(), 1);
    EXPECT_EQ(empty_binary.arity(), 2);

    EXPECT_EQ(false_view.get_storage_index(), empty_unary.get_storage_index());
    JoinIndexCache<> cache;
    const std::array<size_t, 1> key_positions { 0 };
    EXPECT_NO_THROW(cache.get_or_create(empty_unary, key_positions));
    EXPECT_EQ(cache.size(), 1);
    EXPECT_THROW(cache.get_or_create(false_view, key_positions), std::out_of_range);
}

TEST(YggdrasilTests, DatabaseRelationRepositorySharesRowsAcrossOverlappingRelations)
{
    RelationRepositoryFactory<> factory;
    auto repository = factory.create();
    RelationBuilder builder { { ColumnIndex(1), ColumnIndex(2) } };
    builder.insert({ 10, 11 });
    builder.insert({ 20, 21 });
    const auto first = insert(repository, builder).first;
    builder.clear();
    builder.insert({ 30, 31 });
    builder.insert({ 20, 21 });
    const auto second = insert(repository, builder).first;

    EXPECT_NE(first.get_index(), second.get_index());
    EXPECT_EQ(repository.get_row_repository().size(), 3);
    EXPECT_EQ(repository.get_row_set_repository().size(), 2);
    EXPECT_TRUE(first.contains({ 20, 21 }));
    EXPECT_TRUE(second.contains({ 20, 21 }));
    EXPECT_EQ(first.at(1).data(), second.at(0).data());
    EXPECT_FALSE(first.contains({ 30, 31 }));
    EXPECT_FALSE(second.contains({ 10, 11 }));

    RelationBuilder difference_builder { { ColumnIndex(1), ColumnIndex(2) } };
    difference(second, first, difference_builder);
    expect_rows(difference_builder, { { 30, 31 } });
}

TEST(YggdrasilTests, DatabaseRelationViewsMixBuildersDataAndInternedIndicesInOperations)
{
    RelationRepositoryFactory<> factory;
    auto repository = factory.create();
    RelationBuilder left { { ColumnIndex(1), ColumnIndex(2) } };
    left.insert({ 4, 40 });
    left.insert({ 5, 50 });
    RelationBuilder right { { ColumnIndex(1), ColumnIndex(3) } };
    right.insert({ 4, 400 });
    right.insert({ 6, 600 });
    const auto right_view = insert(repository, right).first;
    auto right_data = right_view.get_data();
    const auto left_builder_view = make_view(left, repository);
    const auto right_data_view = make_view(right_data, repository);
    const auto right_index_view = make_view(right_view.get_index(), repository);
    EXPECT_EQ(&right_data_view.get_handle(), &right_data);
    EXPECT_EQ(&left_builder_view.get_handle(), &left);
    EXPECT_EQ(&right_index_view.get_context(), &repository);
    const auto check_access = [](const auto& view)
    {
        EXPECT_TRUE(view.contains({ 4, 400 }));
        EXPECT_FALSE(view.contains({ 4, 401 }));
        EXPECT_THROW(view.contains({ 4 }), std::invalid_argument);
        EXPECT_THROW(view.at(view.size()), std::out_of_range);
    };
    check_access(right_data_view);
    check_access(right_index_view);

    Workspace<> workspace;
    RelationBuilder joined { { ColumnIndex(1), ColumnIndex(2), ColumnIndex(3) } };
    join(left_builder_view, right_data_view, joined, workspace);
    expect_rows(joined, { { 4, 40, 400 } });
    join(left_builder_view, right_index_view, joined, workspace);
    expect_rows(joined, { { 4, 40, 400 } });

    const RelationContext context { repository };
    const auto contextual_index_view = make_view(right_view.get_index(), context);
    static_assert(RelationViewConcept<decltype(contextual_index_view)>);
    EXPECT_EQ(&contextual_index_view.get_context(), &context);
    EXPECT_EQ(&contextual_index_view.get_data(), &right_view.get_data());
    EXPECT_EQ(Hash<decltype(contextual_index_view)> {}(contextual_index_view), Hash<IndexView> {}(right_view));
    join(make_view(left, context), make_view(right_data, context), joined, workspace);
    expect_rows(joined, { { 4, 40, 400 } });

    RelationBuilder projected { { ColumnIndex(3) } };
    project(right_data_view, { ColumnIndex(3) }, projected, workspace);
    expect_rows(projected, { { 400 }, { 600 } });
    RelationBuilder selected { { ColumnIndex(1), ColumnIndex(3) } };
    select(right_index_view, [](std::span<const uint_t> row) { return row[0] == 4; }, selected);
    expect_rows(selected, { { 4, 400 } });
    union_(right_data_view, right_index_view, selected);
    expect_rows(selected, { { 4, 400 }, { 6, 600 } });
    difference(right_data_view, right_index_view, selected);
    EXPECT_TRUE(selected.empty());

    const Builder<Columns> labels { ColumnIndex(1), ColumnIndex(30) };
    const auto renamed = repository.rename(right_index_view, labels.span());
    EXPECT_EQ(renamed.get_storage_address(), right_index_view.get_storage_address());
    EXPECT_EQ(renamed.get_storage_index(), right_index_view.get_storage_index());
    RelationBuilder renamed_projection { { ColumnIndex(30) } };
    project(renamed, { ColumnIndex(30) }, renamed_projection, workspace);
    expect_rows(renamed_projection, { { 400 }, { 600 } });

    right.rename(labels.span());
    const auto borrowed = make_view(right, repository);
    static_assert(RelationViewConcept<decltype(borrowed)>);
    EXPECT_EQ(borrowed.get_storage_address(), &right.storage());
    project(borrowed, { ColumnIndex(30) }, renamed_projection, workspace);
    expect_rows(renamed_projection, { { 400 }, { 600 } });
}

TEST(YggdrasilTests, DatabaseInternedRowsOutliveBuilderReuseAndRenamesShareStorage)
{
    RelationPoolFactory<> row_factory;
    auto builders = row_factory.create_pool();
    RelationRepositoryFactory<> factory(row_factory);
    auto repository = factory.create();
    auto builder = builders.get_or_allocate({ ColumnIndex(1), ColumnIndex(2) });
    builder->insert({ 7, 70 });
    builder->insert({ 8, 80 });
    const auto original = insert(repository, *builder).first;
    const auto* builder_address = &*builder;
    builder = {};
    builder = builders.get_or_allocate({ ColumnIndex(4), ColumnIndex(5) });
    EXPECT_EQ(&*builder, builder_address);
    builder->insert({ 99, 100 });
    expect_rows(original, { { 7, 70 }, { 8, 80 } });
    EXPECT_NE(original.at(0).data(), builder->at(0).data());

    const auto renamed = [&]()
    {
        const Builder<Columns> temporary_columns { ColumnIndex(4), ColumnIndex(5) };
        return repository.rename(original, temporary_columns.span());
    }();
    const Builder<Columns> renamed_columns { ColumnIndex(4), ColumnIndex(5) };
    EXPECT_NE(original.get_index(), renamed.get_index());
    EXPECT_EQ(original.at(0).data(), renamed.at(0).data());
    EXPECT_EQ(original.get_storage_index(), renamed.get_storage_index());
    EXPECT_NE(original.get_storage_index(), builder->get_storage_index());
    EXPECT_TRUE(std::ranges::equal(renamed.columns(), renamed_columns.span()));
    EXPECT_TRUE(EqualTo<IndexView> {}(repository.rename(original, renamed_columns.span()), renamed));
    expect_rows(renamed, { { 7, 70 }, { 8, 80 } });

    RelationBuilder probe { { ColumnIndex(4), ColumnIndex(6) } };
    probe.insert({ 7, 700 });
    RelationBuilder joined { { ColumnIndex(4), ColumnIndex(5), ColumnIndex(6) } };
    JoinPlan plan(renamed.columns(), probe.columns());
    JoinIndexCache<> cache;
    Workspace<> workspace;
    join(renamed, make_view(probe, repository), plan, cache, JoinReuse { true, false }, joined, workspace);
    expect_rows(joined, { { 7, 70, 700 } });
    EXPECT_EQ(cache.size(), 1);
}

TEST(YggdrasilTests, DatabaseRelationRepositoryViewsKeepRepositoryIdentity)
{
    RelationPoolFactory<> row_factory;
    RelationRepositoryFactory<> factory(row_factory);
    auto first_repository = factory.create();
    auto second_repository = factory.create();
    RelationBuilder builder { { ColumnIndex(1) } };
    builder.insert({ 12 });
    const auto first = insert(first_repository, builder).first;
    const auto second = insert(second_repository, builder).first;
    EXPECT_EQ(first.get_index(), second.get_index());
    EXPECT_NE(first_repository.get_index(), second_repository.get_index());
    EXPECT_FALSE(EqualTo<IndexView> {}(first, second));
    EXPECT_NE(first.get_storage_index(), second.get_storage_index());

    auto data = first.get_data();
    const auto original_hash = Hash<Data<RelationTag>> {}(data);
    data.index = Index<RelationTag>(123);
    EXPECT_EQ(Hash<Data<RelationTag>> {}(data), original_hash);
    EXPECT_TRUE(EqualTo<Data<RelationTag>> {}(data, first.get_data()));
    const auto bytes = cista::serialize(data);
    auto relocated = bytes;
    const auto* round_trip = cista::deserialize<Data<RelationTag>>(relocated);
    EXPECT_EQ(round_trip->index, data.index);
    EXPECT_TRUE(EqualTo<Data<RelationTag>> {}(*round_trip, data));
}

TEST(YggdrasilTests, DatabaseRelationRepositoryUsesElementHashAndEquality)
{
    using ValueRelation = Relation<DatabaseInternedValue>;
    RelationRepositoryFactory<DatabaseInternedValue> factory;
    auto repository = factory.create();
    Builder<ValueRelation> first { { ColumnIndex(1), ColumnIndex(2) } };
    first.insert({ { 12 }, { 13 } });
    first.insert({ { 15 }, { 16 } });
    Builder<ValueRelation> equivalent { { ColumnIndex(1), ColumnIndex(2) } };
    equivalent.insert({ { 25 }, { 26 } });
    equivalent.insert({ { 22 }, { 23 } });
    const auto [original, created] = insert(repository, first);
    const auto [duplicate, duplicate_created] = insert(repository, equivalent);
    EXPECT_TRUE(created);
    EXPECT_FALSE(duplicate_created);
    EXPECT_EQ(original.get_index(), duplicate.get_index());
    EXPECT_EQ(original.at(0).data(), duplicate.at(0).data());
    EXPECT_EQ(original.at(0)[0].value, 12);
    EXPECT_EQ(repository.size(), 1);
}

TEST(YggdrasilTests, DatabaseRelationRepositoryRejectsInvalidDataAndForeignRename)
{
    RelationRepositoryFactory<> factory;
    auto repository = factory.create();
    auto foreign_repository = factory.create();
    RelationBuilder builder { { ColumnIndex(1), ColumnIndex(2) } };
    builder.insert({ 10, 20 });
    builder.insert({ 30, 40 });
    const auto original = insert(repository, builder).first;
    Data<RelationTag> empty_data;
    EXPECT_TRUE(empty_data.columns_index.is_max());
    EXPECT_TRUE(empty_data.row_set_index.is_max());
    EXPECT_THROW(repository.insert(empty_data), std::invalid_argument);
    auto data = original.get_data();
    data.clear();
    EXPECT_TRUE(data.index.is_max());
    EXPECT_TRUE(data.columns_index.is_max());
    EXPECT_TRUE(data.row_set_index.is_max());
    EXPECT_THROW(repository.insert(data), std::invalid_argument);
    data = original.get_data();
    data.columns_index = ColumnsIndex::max();
    EXPECT_THROW(repository.insert(data), std::invalid_argument);
    data = original.get_data();
    data.row_set_index = RowSetIndex::max();
    EXPECT_THROW(repository.insert(data), std::invalid_argument);

    const std::array<ColumnIndex, 1> unary_schema { ColumnIndex(1) };
    data = original.get_data();
    data.columns_index = repository.insert(std::span(unary_schema)).first.get_index();
    EXPECT_THROW(repository.insert(data), std::invalid_argument);
    const std::array<ColumnIndex, 2> duplicate_schema { ColumnIndex(1), ColumnIndex(1) };
    const std::span<const ColumnIndex> duplicate_view { std::span(duplicate_schema) };
    EXPECT_THROW(builder.rename(duplicate_view), std::invalid_argument);
    EXPECT_THROW(builder.initialize(duplicate_view), std::invalid_argument);
    EXPECT_EQ(builder.get_index(), original.get_index());
    EXPECT_TRUE(std::ranges::equal(builder.columns(), original.columns()));
    expect_rows(builder, { { 10, 20 }, { 30, 40 } });
    Data<Columns> invalid_schema;
    invalid_schema.values.push_back(ColumnIndex(1));
    invalid_schema.values.push_back(ColumnIndex(1));
    EXPECT_THROW(repository.insert(invalid_schema), std::invalid_argument);
    EXPECT_TRUE(invalid_schema.index.is_max());

    data = original.get_data();
    const auto row_ids = original.row_indices();
    const std::array<RowIndex, 2> reversed_ids { row_ids[1], row_ids[0] };
    data.row_set_index = RowSetIndex(repository.get_row_set_repository().insert(std::span<const RowIndex>(reversed_ids)));
    EXPECT_THROW(repository.insert(data), std::invalid_argument);
    const std::array<RowIndex, 2> duplicate_ids { row_ids[0], row_ids[0] };
    data.row_set_index = RowSetIndex(repository.get_row_set_repository().insert(std::span<const RowIndex>(duplicate_ids)));
    EXPECT_THROW(repository.insert(data), std::invalid_argument);
    const std::array<RowIndex, 1> unknown_row { RowIndex::max() };
    data.row_set_index = RowSetIndex(repository.get_row_set_repository().insert(std::span<const RowIndex>(unknown_row)));
    EXPECT_THROW(repository.insert(data), std::invalid_argument);

    const auto foreign = insert(foreign_repository, builder).first;
    ASSERT_EQ(foreign.get_index(), original.get_index());
    EXPECT_THROW(repository.rename(foreign, original.columns()), std::invalid_argument);
    EXPECT_THROW(repository.rename(original, std::span<const ColumnIndex>(unary_schema)), std::invalid_argument);
    EXPECT_THROW(repository.rename(original, duplicate_view), std::invalid_argument);
    EXPECT_EQ(repository.size(), 1);
    EXPECT_EQ(original.columns()[0], ColumnIndex(1));
    EXPECT_EQ(original.columns()[1], ColumnIndex(2));
    expect_rows(original, { { 10, 20 }, { 30, 40 } });
}

TEST(YggdrasilTests, DatabaseConversionsRemapStoredIdentityAndReuseMutableStorage)
{
    RelationRepositoryFactory<> source_factory;
    RelationRepositoryFactory<> target_factory;
    auto source_repository = source_factory.create();
    auto target_repository = target_factory.create();
    RelationBuilder source_builder { { ColumnIndex(7), ColumnIndex(3) } };
    source_builder.insert({ 4, 5 });
    source_builder.insert({ 1, 2 });
    const auto source = insert(source_repository, source_builder, 42).first;
    RelationBuilder seed { { ColumnIndex(99) } };
    seed.insert({ 77 });
    (void) insert(target_repository, seed);

    const auto [target, created] = database::copy(source, target_repository);
    EXPECT_TRUE(created);
    EXPECT_EQ(&target.get_context(), &target_repository);
    EXPECT_NE(target.get_data().columns_index, source.get_data().columns_index);
    EXPECT_NE(target.get_data().row_set_index, source.get_data().row_set_index);
    EXPECT_EQ(target.get_data().schema_namespace, 42);
    EXPECT_TRUE(std::ranges::equal(target.columns(), source.columns()));
    expect_rows(target, { { 4, 5 }, { 1, 2 } });
    EXPECT_NE(target.at(0).data(), source.at(0).data());
    EXPECT_FALSE(database::copy(source, target_repository).second);
    const auto [same, same_created] = database::copy(target, target_repository);
    EXPECT_FALSE(same_created);
    EXPECT_EQ(same.get_index(), target.get_index());

    auto pending = source.get_data();
    pending.index = Index<RelationTag>(1234);
    pending.schema_namespace = 43;
    const auto changed = database::copy(make_view(pending, source_repository), target_repository);
    EXPECT_TRUE(changed.second);
    EXPECT_EQ(changed.first.get_data().schema_namespace, 43);
    EXPECT_EQ(pending.index, Index<RelationTag>(1234));

    RelationPool<> pool;
    auto output_handle = pool.get_or_allocate({ ColumnIndex(3), ColumnIndex(7) });
    auto& output = *output_handle;
    const auto storage_index = output.get_storage_index();
    output.insert({ 99, 88 });
    assign(output, source);
    EXPECT_TRUE(std::ranges::equal(output.columns(), source.columns()));
    expect_rows(output, { { 4, 5 }, { 1, 2 } });
    const auto capacity = output.memory_usage();
    const auto* columns_storage = output.columns().data();
    const auto canonical = insert(source_repository, output).first;
    EXPECT_FALSE(output.get_index().is_max());
    EXPECT_EQ(&assign(output, target), &output);
    EXPECT_TRUE(output.get_index().is_max());
    EXPECT_EQ(output.memory_usage(), capacity);
    EXPECT_EQ(output.columns().data(), columns_storage);
    EXPECT_EQ(output.get_storage_index(), storage_index);
    expect_rows(output, { { 4, 5 }, { 1, 2 } });

    const auto* first_row = output[0].data();
    (void) insert(source_repository, output);
    EXPECT_EQ(&assign(output, output), &output);
    EXPECT_TRUE(output.get_index().is_max());
    EXPECT_EQ(output[0].data(), first_row);
    (void) insert(source_repository, output);
    EXPECT_EQ(&assign(output, make_view(output, source_repository)), &output);
    EXPECT_TRUE(output.get_index().is_max());
    EXPECT_EQ(output[0].data(), first_row);
    EXPECT_EQ(output.columns().data(), columns_storage);
    EXPECT_EQ(output.memory_usage(), capacity);
    EXPECT_EQ(output.get_storage_index(), storage_index);
    expect_rows(output, { { 4, 5 }, { 1, 2 } });

    // A changed arity replaces storage; row values follow the source's column order.
    assign(seed, source_builder);
    EXPECT_TRUE(seed.get_index().is_max());
    EXPECT_TRUE(std::ranges::equal(seed.columns(), source.columns()));
    expect_rows(seed, { { 4, 5 }, { 1, 2 } });
    assign(seed, make_view(pending, source_repository));
    expect_rows(seed, { { 4, 5 }, { 1, 2 } });
    EXPECT_EQ(canonical.get_data().schema_namespace, 0);

    RelationBuilder empty { ColumnIndex(9), ColumnIndex(8) };
    assign(output, empty);
    EXPECT_TRUE(output.empty());
    EXPECT_TRUE(std::ranges::equal(output.columns(), empty.columns()));
    EXPECT_EQ(output.memory_usage(), capacity);
    EXPECT_EQ(output.get_storage_index(), storage_index);
    expect_rows(canonical, { { 4, 5 }, { 1, 2 } });

    RelationBuilder nullary;
    assign(seed, nullary);
    EXPECT_EQ(seed.arity(), 0);
    EXPECT_TRUE(seed.empty());
    nullary.insert({});
    assign(seed, nullary);
    EXPECT_EQ(seed.arity(), 0);
    expect_rows(seed, { {} });

    source_repository.clear();
    source_builder.clear();
    expect_rows(target, { { 4, 5 }, { 1, 2 } });
    expect_rows(changed.first, { { 4, 5 }, { 1, 2 } });
}

TEST(YggdrasilTests, DatabaseRelationIteratorsSupportRandomAccessThroughTemporaryViews)
{
    auto repository = RelationRepositoryFactory<>().create();
    auto builder = RelationBuilder { ColumnIndex(0) };
    builder.insert({ 10 });
    builder.insert({ 20 });
    builder.insert({ 30 });
    const auto stored = database::insert(repository, builder).first;
    const auto check = [](auto view)
    {
        using V = decltype(view);
        using Iterator = std::ranges::iterator_t<V>;
        static_assert(std::random_access_iterator<Iterator>);
        static_assert(std::same_as<typename std::iterator_traits<Iterator>::reference, typename Iterator::value_type>);
        static_assert(std::same_as<typename std::iterator_traits<Iterator>::pointer, void>);
        EXPECT_EQ(Iterator {}, Iterator {});
        const auto begin = V(view).begin();
        const auto end = V(view).end();
        EXPECT_EQ(end - begin, 3);
        EXPECT_EQ(begin - end, -3);
        EXPECT_EQ(std::ranges::distance(begin, end), 3);
        EXPECT_LT(begin, end);
        EXPECT_LE(begin, begin);
        EXPECT_GT(end, begin);
        EXPECT_GE(end, end);
        EXPECT_NE(begin, end);
        auto it = begin;
        const auto copy = it;
        EXPECT_EQ(it++, copy);
        EXPECT_TRUE(std::ranges::equal(*copy, view[0]));
        EXPECT_TRUE(std::ranges::equal(*it, view[1]));
        EXPECT_EQ(++it, begin + 2);
        EXPECT_EQ(it--, begin + 2);
        EXPECT_EQ(--it, begin);
        it += 3;
        EXPECT_EQ(it, end);
        it += -2;
        EXPECT_EQ(it, 1 + begin);
        it -= -1;
        EXPECT_EQ(it, end - 1);
        it -= 2;
        EXPECT_EQ(it, begin);
        EXPECT_TRUE(std::ranges::equal(end[-1], view[2]));
        EXPECT_TRUE(std::ranges::equal((begin + 1)[-1], view[0]));
    };
    check(make_view(builder, repository));
    check(make_view(stored.get_data(), repository));
    check(make_view(stored.get_index(), repository));

    auto nullary = RelationBuilder {};
    const auto check_empty = [](auto view)
    {
        EXPECT_EQ(view.begin(), view.end());
        EXPECT_EQ(view.end() - view.begin(), 0);
    };
    const auto empty = database::insert(repository, nullary).first;
    check_empty(make_view(nullary, repository));
    check_empty(make_view(empty.get_data(), repository));
    check_empty(make_view(empty.get_index(), repository));
    nullary.insert({});
    const auto unit = database::insert(repository, nullary).first;
    const auto check_unit = [](auto view)
    {
        using V = decltype(view);
        const auto it = V(view).end() - 1;
        EXPECT_TRUE((*it).empty());
        EXPECT_EQ(it + 1, view.end());
    };
    check_unit(make_view(nullary, repository));
    check_unit(make_view(unit.get_data(), repository));
    check_unit(make_view(unit.get_index(), repository));
}

namespace
{
// The row's entity owner is separate from the relation's physical storage.
struct TypedRelationContext
{
    const RelationRepository<ColumnsIndex>& rows;
    const Repository& entities;
    friend const auto& get_relation_repository(const TypedRelationContext& context) noexcept { return context.rows; }
    friend const auto& get_repository(const TypedRelationContext& context) noexcept { return context.entities; }
};
}

TEST(YggdrasilTests, TypedRelationsResolveRowsThroughTheirElementRepository)
{
    auto entities = RelationRepositoryFactory<>().create();
    const auto first = entities.insert(std::array { ColumnIndex(7) }).first;
    const auto second = entities.insert(std::array { ColumnIndex(9) }).first;
    auto rows = RelationRepositoryFactory<ColumnsIndex>().create();
    const auto context = TypedRelationContext { rows, entities };
    auto builder = Builder<Relation<ColumnsIndex>>({ ColumnIndex(1), ColumnIndex(2) });
    builder.insert({ first.get_index(), second.get_index() });
    const auto published = database::insert(rows, builder, 42).first;
    const auto index_view = make_view(published.get_index(), context);
    const auto data_view = make_view(published.get_data(), context);
    const auto builder_view = make_view(builder, context);
    static_assert(RelationViewConcept<decltype(index_view), ColumnsIndex>);
    static_assert(std::ranges::forward_range<decltype(index_view)>);
    static_assert(std::ranges::borrowed_range<decltype(index_view)>);
    static_assert(std::same_as<decltype(index_view[0][0]), ColumnsIndexView>);
    const auto check = [&](const auto& view)
    {
        ASSERT_EQ(view.size(), 1);
        EXPECT_EQ(view.row(0)[0], first.get_index());
        EXPECT_EQ(view[0][0], first);
        EXPECT_EQ(view.at(0)[1], second);
        EXPECT_THROW(view.at(1), std::out_of_range);
        size_t count = 0;
        for (auto objects : view)
        {
            EXPECT_EQ(objects.front(), first);
            EXPECT_EQ(objects.back(), second);
            EXPECT_EQ(&objects.get_context(), &entities);
            ++count;
        }
        EXPECT_EQ(count, 1);
    };
    check(index_view);
    check(data_view);
    check(builder_view);
    const auto iterator = make_view(published.get_index(), context).begin();
    EXPECT_EQ((*iterator)[1], second);
    const auto element_iterator = make_view(published.get_index(), context)[0].begin();
    EXPECT_EQ(*element_iterator, first);

    const auto renamed = rows.rename(index_view, std::array { ColumnIndex(3), ColumnIndex(4) });
    static_assert(std::same_as<decltype(renamed), decltype(index_view)>);
    EXPECT_EQ(&renamed.get_context(), &context);
    EXPECT_EQ(renamed.get_data().schema_namespace, 42);
    EXPECT_EQ(renamed.get_storage_address(), index_view.get_storage_address());
    EXPECT_EQ(renamed[0][0], first);
    auto other_rows = RelationRepositoryFactory<ColumnsIndex>().create();
    EXPECT_THROW(other_rows.rename(index_view, std::array { ColumnIndex(3), ColumnIndex(4) }), std::invalid_argument);

    auto probe = Builder<Relation<ColumnsIndex>>({ ColumnIndex(2) });
    probe.insert({ second.get_index() });
    auto joined = join<ColumnsIndex>(data_view, make_view(probe, context));
    EXPECT_TRUE(joined.contains({ first.get_index(), second.get_index() }));
    auto projected = project<ColumnsIndex>(index_view, { ColumnIndex(2) });
    EXPECT_TRUE(projected.contains({ second.get_index() }));
    auto assigned = Builder<Relation<ColumnsIndex>>();
    assign(assigned, index_view);
    EXPECT_TRUE(assigned.contains({ first.get_index(), second.get_index() }));
    const auto copied = database::copy(index_view, other_rows).first;
    EXPECT_TRUE(copied.contains({ first.get_index(), second.get_index() }));

    auto truth = Builder<Relation<ColumnsIndex>>();
    truth.insert({});
    const auto truth_view = make_view(truth, context);
    EXPECT_TRUE((*truth_view.begin()).empty());
    EXPECT_EQ(std::ranges::distance(truth_view), 1);
    truth.clear();
    EXPECT_EQ(truth_view.begin(), truth_view.end());
}

}  // namespace ygg::tests
