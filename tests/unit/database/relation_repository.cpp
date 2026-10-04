/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <algorithm>
#include <array>
#include <cista/serialization.h>
#include <concepts>
#include <gtest/gtest.h>
#include <limits>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <yggdrasil/database/operations.hpp>
#include <yggdrasil/database/relation_pool.hpp>
#include <yggdrasil/database/relation_repository.hpp>
#include <yggdrasil/semantics/equal_to.hpp>
#include <yggdrasil/semantics/hash.hpp>

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
};

static_assert(std::same_as<IndexView, RelationView<>>);
static_assert(RelationViewConcept<BuilderView>);
static_assert(RelationViewConcept<DataView>);
static_assert(RelationViewConcept<IndexView>);
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
    EXPECT_THROW((void) database::get_or_create(repository, invalid), std::invalid_argument);
    EXPECT_FALSE(repository.find(invalid));
}

TEST(YggdrasilTests, DatabaseColumnsInternOrderedSchemasThroughBuilderDataAndIndexViews)
{
    RelationRepositoryFactory<> factory;
    auto repository = factory.create();
    Builder<Columns> builder { ColumnIndex(8), ColumnIndex(3) };
    const auto [original, created] = intern_columns(builder, repository);
    ASSERT_TRUE(created);
    EXPECT_EQ(builder.get_index(), original.get_index());
    EXPECT_EQ(repository.get_columns(original.get_index()).get_index(), original.get_index());

    Data<Columns> pending;
    make_data(builder, pending);
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
    const auto [duplicate, duplicate_created] = database::get_or_create(repository, pending);
    EXPECT_FALSE(duplicate_created);
    EXPECT_EQ(pending.index, original.get_index());
    EXPECT_TRUE(EqualTo<ColumnsIndexView> {}(original, duplicate));
    EXPECT_EQ(Hash<ColumnsIndexView> {}(original), Hash<ColumnsIndexView> {}(duplicate));
    ASSERT_TRUE(repository.find(pending));
    EXPECT_EQ(repository.find(pending)->get_index(), original.get_index());

    const std::array<ColumnIndex, 2> reversed { ColumnIndex(3), ColumnIndex(8) };
    builder.assign(std::span(reversed));
    const auto [other_order, order_created] = intern_columns(builder, repository);
    EXPECT_TRUE(order_created);
    EXPECT_NE(other_order.get_index(), original.get_index());

    RelationBuilder rows(original);
    rows.insert({ 10, 20 });
    const auto relation = intern_relation(rows, repository).first;
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
    const auto [reused, reused_created] = intern_columns(builder, repository);
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
    const auto [first, created] = intern_relation(builder, repository, 7);
    ASSERT_TRUE(created);
    EXPECT_EQ(builder.get_index(), first.get_index());
    EXPECT_EQ(first.get_data().index, first.get_index());
    EXPECT_EQ(first.get_data().schema_namespace, 7);

    const auto [duplicate, duplicate_created] = intern_relation(builder, repository, 7);
    EXPECT_FALSE(duplicate_created);
    EXPECT_TRUE(EqualTo<IndexView> {}(first, duplicate));
    EXPECT_EQ(Hash<IndexView> {}(first), Hash<IndexView> {}(duplicate));

    const auto [other_namespace, namespace_created] = intern_relation(builder, repository, 8);
    EXPECT_TRUE(namespace_created);
    EXPECT_NE(first.get_index(), other_namespace.get_index());
    EXPECT_EQ(first.at(0).data(), other_namespace.at(0).data());

    const Builder<Columns> reordered_columns { ColumnIndex(8), ColumnIndex(3) };
    builder.rename(reordered_columns.span());
    const auto [other_schema, schema_created] = intern_relation(builder, repository, 7);
    EXPECT_TRUE(schema_created);
    EXPECT_NE(first.get_index(), other_schema.get_index());
    EXPECT_EQ(first.at(0).data(), other_schema.at(0).data());
    EXPECT_EQ(first.columns()[0], ColumnIndex(3));
    EXPECT_EQ(first.columns()[1], ColumnIndex(8));

    const Builder<Columns> original_columns { ColumnIndex(3), ColumnIndex(8) };
    builder.initialize(original_columns.span());
    builder.insert({ 20, 21 });
    builder.insert({ 10, 11 });
    const auto [other_order, order_created] = intern_relation(builder, repository, 7);
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
    const auto false_view = intern_relation(nullary, repository).first;
    nullary.insert({});
    const auto true_view = intern_relation(nullary, repository).first;
    EXPECT_NE(false_view.get_index(), true_view.get_index());
    EXPECT_EQ(false_view.arity(), 0);
    EXPECT_EQ(true_view.arity(), 0);
    EXPECT_TRUE(false_view.empty());
    EXPECT_EQ(true_view.size(), 1);
    EXPECT_TRUE(true_view.contains({}));

    RelationBuilder unary { { ColumnIndex(5) } };
    const auto empty_unary = intern_relation(unary, repository).first;
    RelationBuilder binary { { ColumnIndex(5), ColumnIndex(6) } };
    const auto empty_binary = intern_relation(binary, repository).first;
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
    const auto first = intern_relation(builder, repository).first;
    builder.clear();
    builder.insert({ 30, 31 });
    builder.insert({ 20, 21 });
    const auto second = intern_relation(builder, repository).first;

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
    Data<RelationTag> right_data;
    make_data(right, right_data, repository);
    const auto right_view = get_or_create(repository, right_data).first;
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
    const auto original = intern_relation(*builder, repository).first;
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
    const auto first = intern_relation(builder, first_repository).first;
    const auto second = intern_relation(builder, second_repository).first;
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
    const auto [original, created] = intern_relation(first, repository);
    const auto [duplicate, duplicate_created] = intern_relation(equivalent, repository);
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
    const auto original = intern_relation(builder, repository).first;
    Data<RelationTag> empty_data;
    EXPECT_TRUE(empty_data.columns_index.is_max());
    EXPECT_TRUE(empty_data.row_set_index.is_max());
    EXPECT_THROW(repository.get_or_create(empty_data), std::invalid_argument);
    auto data = original.get_data();
    data.clear();
    EXPECT_TRUE(data.index.is_max());
    EXPECT_TRUE(data.columns_index.is_max());
    EXPECT_TRUE(data.row_set_index.is_max());
    EXPECT_THROW(repository.get_or_create(data), std::invalid_argument);
    data = original.get_data();
    data.columns_index = ColumnsIndex::max();
    EXPECT_THROW(repository.get_or_create(data), std::invalid_argument);
    data = original.get_data();
    data.row_set_index = RowSetIndex::max();
    EXPECT_THROW(repository.get_or_create(data), std::invalid_argument);

    const std::array<ColumnIndex, 1> unary_schema { ColumnIndex(1) };
    data = original.get_data();
    data.columns_index = repository.intern_columns(std::span(unary_schema)).get_index();
    EXPECT_THROW(repository.get_or_create(data), std::invalid_argument);
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
    EXPECT_THROW(repository.get_or_create(invalid_schema), std::invalid_argument);
    EXPECT_TRUE(invalid_schema.index.is_max());

    data = original.get_data();
    const auto row_ids = original.row_indices();
    const std::array<RowIndex, 2> reversed_ids { row_ids[1], row_ids[0] };
    data.row_set_index = RowSetIndex(repository.get_row_set_repository().insert(std::span<const RowIndex>(reversed_ids)));
    EXPECT_THROW(repository.get_or_create(data), std::invalid_argument);
    const std::array<RowIndex, 2> duplicate_ids { row_ids[0], row_ids[0] };
    data.row_set_index = RowSetIndex(repository.get_row_set_repository().insert(std::span<const RowIndex>(duplicate_ids)));
    EXPECT_THROW(repository.get_or_create(data), std::invalid_argument);
    const std::array<RowIndex, 1> unknown_row { RowIndex::max() };
    data.row_set_index = RowSetIndex(repository.get_row_set_repository().insert(std::span<const RowIndex>(unknown_row)));
    EXPECT_THROW(repository.get_or_create(data), std::invalid_argument);

    const auto foreign = intern_relation(builder, foreign_repository).first;
    ASSERT_EQ(foreign.get_index(), original.get_index());
    EXPECT_THROW(repository.rename(foreign, original.columns()), std::invalid_argument);
    EXPECT_THROW(repository.rename(original, std::span<const ColumnIndex>(unary_schema)), std::invalid_argument);
    EXPECT_THROW(repository.rename(original, duplicate_view), std::invalid_argument);
    EXPECT_EQ(repository.size(), 1);
    EXPECT_EQ(original.columns()[0], ColumnIndex(1));
    EXPECT_EQ(original.columns()[1], ColumnIndex(2));
    expect_rows(original, { { 10, 20 }, { 30, 40 } });
}

}  // namespace ygg::tests
