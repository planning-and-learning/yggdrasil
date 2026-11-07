/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/semantics/relation_repository.hpp"
#include <urd/repr/sequences/sequence.hpp>
#include <urd/store.hpp>

#include "yggdrasil/database/semantics/operations.hpp"
#include "yggdrasil/database/semantics/relation_pool.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cista/serialization.h>
#include <cmath>
#include <concepts>
#include <cstring>
#include <gtest/gtest.h>
#include <iterator>
#include <limits>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <urd/elements/equal_to.hpp>
#include <urd/elements/hash.hpp>
#include <utility>
#include <vector>

namespace ygg::tests
{
using Values = boost::mp11::mp_list<uint_t>;
using ColumnIndex = urd::index<database::Column>;

namespace
{
template<typename T = uint_t, typename... Args>
auto cells(Args... values)
{ return std::tuple { T(values)... }; }

template<typename T>
auto packed(std::span<const T> values)
{
    urd::Store<urd::sequence<std::byte>> result(values.size() * ygg::ByteCodec<T>::size);
    for (size_t i = 0; i < values.size(); ++i)
        ygg::ByteCodec<T>::encode(values[i], std::span(result).subspan(i * ygg::ByteCodec<T>::size, ygg::ByteCodec<T>::size));
    return result;
}

using RelationTag = database::Relation;
using RelationBuilder = urd::builder<RelationTag>;
using Repository = database::RelationRepository;
using BuilderView = urd::borrowed_view<RelationTag, Repository, RelationBuilder>;
using DataView = urd::borrowed_view<RelationTag, Repository>;
using IndexView = urd::index_view<RelationTag, Repository>;
using ColumnsIndex = urd::index<database::Columns>;
using RowIndex = urd::index<database::RelationRow>;
using RowSetIndex = urd::index<database::RelationRowSet>;
using ColumnsBuilderView = urd::borrowed_view<database::Columns, Repository, urd::builder<database::Columns>>;
using ColumnsDataView = urd::borrowed_view<database::Columns, Repository>;
using ColumnsIndexView = urd::index_view<database::Columns, Repository>;

struct AliasFreeRelationView
{
    [[nodiscard]] const urd::builder<database::Columns>& columns() const;
    [[nodiscard]] size_t arity() const;
    [[nodiscard]] size_t size() const;
    [[nodiscard]] bool empty() const;
    [[nodiscard]] const void* get_storage_address() const;
    [[nodiscard]] size_t get_storage_index() const;
    [[nodiscard]] size_t column_index(ColumnIndex) const;
    [[nodiscard]] std::span<const std::byte> row(size_t) const;
    [[nodiscard]] bool contains(std::span<const std::byte>) const;
    [[nodiscard]] bool contains(database::Row) const;
};

struct MutableRelationView : AliasFreeRelationView
{
    std::span<const std::byte> row(size_t);
};

static_assert(database::RelationViewConcept<AliasFreeRelationView>);
static_assert(database::RelationViewConcept<AliasFreeRelationView&>);
static_assert(database::RelationViewConcept<const AliasFreeRelationView&>);
static_assert(database::RelationViewConcept<AliasFreeRelationView&&>);
static_assert(!database::RelationViewConcept<MutableRelationView&>);
static_assert(!database::RelationViewConcept<int>);
static_assert(std::same_as<IndexView, database::RelationView>);
static_assert(database::RelationViewConcept<BuilderView>);
static_assert(database::RelationViewConcept<DataView>);
static_assert(database::RelationViewConcept<IndexView>);

static_assert(std::ranges::random_access_range<BuilderView>);
static_assert(std::ranges::random_access_range<DataView>);
static_assert(std::ranges::random_access_range<IndexView>);
static_assert(std::ranges::borrowed_range<BuilderView>);
static_assert(std::ranges::borrowed_range<DataView>);
static_assert(std::ranges::borrowed_range<IndexView>);
static_assert(!std::ranges::contiguous_range<IndexView>);
static_assert(std::same_as<Repository::SymbolTypes, boost::mp11::mp_list<database::Columns, RelationTag>>);
static_assert(formalism::SupportsSymbol<Repository, database::Columns>);
static_assert(formalism::SupportsSymbol<Repository, RelationTag>);
static_assert(!formalism::SupportsSymbol<Repository, database::Column>);

static_assert(std::same_as<decltype(std::declval<const BuilderView&>().row(0)), std::span<const std::byte>>);
static_assert(std::same_as<decltype(std::declval<const DataView&>().row(0)), std::span<const std::byte>>);
static_assert(std::same_as<decltype(std::declval<const IndexView&>().row(0)), std::span<const std::byte>>);
static_assert(std::same_as<decltype(std::declval<const BuilderView&>().data()), const RelationBuilder&>);
static_assert(std::same_as<decltype(std::declval<const DataView&>().data()), const urd::data<RelationTag>&>);
static_assert(std::same_as<decltype(std::declval<const IndexView&>().index()), urd::index<RelationTag>>);
static_assert(std::same_as<decltype(std::declval<const IndexView&>().index()), urd::index<RelationTag>>);
static_assert(std::same_as<decltype(std::declval<const IndexView&>().data()), const urd::data<RelationTag>&>);
static_assert(std::is_trivially_copyable_v<urd::data<RelationTag>>);
static_assert(!std::is_copy_constructible_v<RelationBuilder>);
static_assert(std::is_copy_constructible_v<IndexView>);
static_assert(std::same_as<decltype(urd::data<RelationTag>::columns_index), ColumnsIndex>);
static_assert(std::same_as<decltype(urd::data<RelationTag>::row_set_index), RowSetIndex>);
static_assert(std::same_as<decltype(std::declval<const IndexView&>().row_indices()), std::span<const RowIndex>>);
static_assert(!std::is_constructible_v<ColumnsIndex, RowIndex> && !std::is_constructible_v<RowIndex, RowSetIndex>
              && !std::is_constructible_v<RowSetIndex, ColumnsIndex>);
static_assert(!std::is_convertible_v<uint_t, ColumnsIndex> && !std::is_convertible_v<uint_t, RowIndex> && !std::is_convertible_v<uint_t, RowSetIndex>);
static_assert(!std::is_convertible_v<ColumnsIndex, uint_t> && !std::is_convertible_v<RowIndex, uint_t> && !std::is_convertible_v<RowSetIndex, uint_t>);
static_assert(sizeof(ColumnsIndex) == sizeof(uint_t) && sizeof(RowIndex) == sizeof(uint_t) && sizeof(RowSetIndex) == sizeof(uint_t));
static_assert(std::is_trivially_copyable_v<ColumnsIndex> && std::is_trivially_copyable_v<RowIndex> && std::is_trivially_copyable_v<RowSetIndex>);
static_assert(database::ColumnsViewConcept<urd::builder<database::Columns>>);
static_assert(database::ColumnsViewConcept<ColumnsBuilderView> && database::ColumnsViewConcept<ColumnsDataView>
              && database::ColumnsViewConcept<ColumnsIndexView>);
static_assert(std::same_as<decltype(std::declval<const ColumnsIndexView&>().index()), ColumnsIndex>);
static_assert(std::same_as<decltype(std::declval<const ColumnsIndexView&>().data()), const urd::data<database::Columns>&>);

template<typename T, typename L, typename R>
concept CanJoinRelations = requires(const L& lhs, const R& rhs, urd::builder<database::Relation>& out) {
    database::join(lhs, rhs, out);
    { database::join(lhs, rhs) } -> std::same_as<urd::builder<database::Relation>>;
};

static_assert(CanJoinRelations<uint_t, BuilderView, DataView>);
static_assert(CanJoinRelations<uint_t, IndexView, RelationBuilder>);

template<typename ViewType>
void expect_rows(const ViewType& view, std::initializer_list<std::initializer_list<uint_t>> rows)
{
    ASSERT_EQ(view.size(), rows.size());
    size_t index = 0;
    for (const auto& row : rows)
        EXPECT_TRUE(std::ranges::equal(view.row(index++), packed(std::span<const uint_t>(row.begin(), row.size()))));
}
}  // namespace

TEST(YggdrasilTests, DatabasePreparedInterningRetainsRawSchemaValidation)
{
    auto repository = database::RelationRepositoryFactory().create();
    auto invalid = urd::data<database::Columns>();
    invalid.values.push_back(database::ColumnLayout { ColumnIndex(3), ygg::type_id<uint_t>, 0, sizeof(uint_t) });
    invalid.values.push_back(database::ColumnLayout { ColumnIndex(3), ygg::type_id<uint_t>, sizeof(uint_t), sizeof(uint_t) });
    EXPECT_THROW((void) database::insert(repository, invalid), std::invalid_argument);
    EXPECT_FALSE(repository.find(invalid));
}

TEST(YggdrasilTests, DatabaseColumnsInternOrderedSchemasThroughBuilderDataAndIndexViews)
{
    database::RelationRepositoryFactory factory;
    auto repository = factory.create();
    urd::builder<database::Columns> builder { ColumnIndex(8), ColumnIndex(3) };
    const auto [original, created] = insert(repository, builder);
    ASSERT_TRUE(created);
    EXPECT_EQ(builder.index(), original.index());
    EXPECT_EQ(repository.view(original.index()).index(), original.index());

    urd::data<database::Columns> pending;
    pending.values.set(builder.span().begin(), builder.span().end());
    const auto builder_view = urd::resolve(builder, repository);
    const auto data_view = urd::resolve(pending, repository);
    const auto index_view = repository.view(original.index());
    const urd::builder<database::Columns> expected { ColumnIndex(8), ColumnIndex(3) };
    EXPECT_TRUE(std::ranges::equal(builder_view.span(), expected));
    EXPECT_TRUE(std::ranges::equal(data_view.span(), expected));
    EXPECT_TRUE(std::ranges::equal(index_view.span(), expected));
    EXPECT_EQ(index_view.column_index(ColumnIndex(3)), 1);
    EXPECT_EQ(&builder_view.data(), &builder);
    EXPECT_EQ(&data_view.data(), &pending);
    EXPECT_EQ(&index_view.data(), &repository[original.index()]);

    pending.index = urd::index<database::Columns>(99);
    const auto [duplicate, duplicate_created] = database::insert(repository, pending);
    EXPECT_FALSE(duplicate_created);
    EXPECT_EQ(pending.index, original.index());
    EXPECT_TRUE(urd::EqualTo<ColumnsIndexView> {}(original, duplicate));
    EXPECT_EQ(urd::Hash<ColumnsIndexView> {}(original), urd::Hash<ColumnsIndexView> {}(duplicate));
    ASSERT_TRUE(repository.find(pending));
    EXPECT_EQ(repository.find(pending).value().index(), original.index());

    const std::array<ColumnIndex, 2> reversed { ColumnIndex(3), ColumnIndex(8) };
    builder.assign(std::span(reversed));
    const auto [other_order, order_created] = insert(repository, builder);
    EXPECT_TRUE(order_created);
    EXPECT_NE(other_order.index(), original.index());

    RelationBuilder rows(original);
    rows.insert(cells(10, 20));
    const auto relation = insert(repository, rows).first;
    const database::JoinPlan joining(relation.columns(), relation.columns());
    const database::ProjectionPlan projecting(relation.columns(), { ColumnIndex(3), ColumnIndex(8) });
    database::RelationPool pool;
    auto output = pool.get_or_allocate(relation.columns());
    database::Workspace workspace;
    database::join(relation, relation, joining, *output, workspace);
    expect_rows(*output, { { 10, 20 } });
    output->initialize(other_order);
    database::project(relation, projecting, *output, workspace);
    expect_rows(*output, { { 20, 10 } });
    const auto projected = database::project(relation, { ColumnIndex(8), ColumnIndex(3) });
    expect_rows(projected, { { 10, 20 } });

    builder.assign(original.span());
    EXPECT_TRUE(builder.index().is_none());
    const auto [reused, reused_created] = insert(repository, builder);
    EXPECT_FALSE(reused_created);
    EXPECT_EQ(reused.index(), original.index());
    EXPECT_EQ(builder.index(), original.index());
    builder.clear();
    EXPECT_TRUE(builder.empty());
    EXPECT_TRUE(builder.index().is_none());
    EXPECT_TRUE(std::ranges::equal(original.span(), expected));
    EXPECT_TRUE(std::ranges::equal(other_order.span(), urd::builder<database::Columns>(std::span(reversed))));
}

TEST(YggdrasilTests, DatabaseRelationRepositoryInternsRowSetsWithOrderedSchemasAndNamespaces)
{
    database::RelationRepositoryFactory factory;
    auto repository = factory.create();
    RelationBuilder builder { { ColumnIndex(3), ColumnIndex(8) } };
    builder.insert(cells(10, 11));
    builder.insert(cells(20, 21));
    const auto [first, created] = insert(repository, builder, 7);
    ASSERT_TRUE(created);
    EXPECT_EQ(builder.index(), first.index());
    EXPECT_EQ(first.data().index, first.index());
    EXPECT_EQ(first.data().schema_namespace, 7);

    const auto [duplicate, duplicate_created] = insert(repository, builder, 7);
    EXPECT_FALSE(duplicate_created);
    EXPECT_TRUE(urd::EqualTo<IndexView> {}(first, duplicate));
    EXPECT_EQ(urd::Hash<IndexView> {}(first), urd::Hash<IndexView> {}(duplicate));

    const auto [other_namespace, namespace_created] = insert(repository, builder, 8);
    EXPECT_TRUE(namespace_created);
    EXPECT_NE(first.index(), other_namespace.index());
    EXPECT_EQ(first.at(0).bytes().data(), other_namespace.at(0).bytes().data());

    const urd::builder<database::Columns> reordered_columns { ColumnIndex(8), ColumnIndex(3) };
    builder.rename(reordered_columns.span());
    const auto [other_schema, schema_created] = insert(repository, builder, 7);
    EXPECT_TRUE(schema_created);
    EXPECT_NE(first.index(), other_schema.index());
    EXPECT_EQ(first.at(0).bytes().data(), other_schema.at(0).bytes().data());
    EXPECT_EQ(first.columns()[0].label, ColumnIndex(3));
    EXPECT_EQ(first.columns()[1].label, ColumnIndex(8));

    const urd::builder<database::Columns> original_columns { ColumnIndex(3), ColumnIndex(8) };
    builder.initialize(original_columns.span());
    builder.insert(cells(20, 21));
    builder.insert(cells(10, 11));
    const auto [other_order, order_created] = insert(repository, builder, 7);
    EXPECT_FALSE(order_created);
    EXPECT_EQ(first.index(), other_order.index());
    expect_rows(first, { { 10, 11 }, { 20, 21 } });
    expect_rows(other_order, { { 10, 11 }, { 20, 21 } });
    EXPECT_EQ(repository.size(), 3);
}

TEST(YggdrasilTests, DatabaseRelationRepositoryDistinguishesNullaryTruthAndEmptySchemas)
{
    database::RelationRepositoryFactory factory;
    auto repository = factory.create();
    RelationBuilder nullary;
    const auto false_view = insert(repository, nullary).first;
    nullary.insert(cells());
    const auto true_view = insert(repository, nullary).first;
    EXPECT_NE(false_view.index(), true_view.index());
    EXPECT_EQ(false_view.arity(), 0);
    EXPECT_EQ(true_view.arity(), 0);
    EXPECT_TRUE(false_view.empty());
    EXPECT_EQ(true_view.size(), 1);
    EXPECT_TRUE(true_view.contains(cells()));

    RelationBuilder unary { { ColumnIndex(5) } };
    const auto empty_unary = insert(repository, unary).first;
    RelationBuilder binary { { ColumnIndex(5), ColumnIndex(6) } };
    const auto empty_binary = insert(repository, binary).first;
    EXPECT_NE(empty_unary.index(), false_view.index());
    EXPECT_NE(empty_binary.index(), empty_unary.index());
    EXPECT_EQ(empty_unary.arity(), 1);
    EXPECT_EQ(empty_binary.arity(), 2);

    EXPECT_EQ(false_view.get_storage_index(), empty_unary.get_storage_index());
    database::JoinIndexCache cache;
    const std::array<database::ColumnSlice, 1> key_positions { database::ColumnSlice { ygg::type_id<uint_t>, 0, sizeof(uint_t) } };
    EXPECT_NO_THROW(cache.get_or_create(empty_unary, key_positions));
    EXPECT_EQ(cache.size(), 1);
    EXPECT_THROW(cache.get_or_create(false_view, key_positions), std::out_of_range);
}

TEST(YggdrasilTests, DatabaseRelationRepositorySharesRowsAcrossOverlappingRelations)
{
    database::RelationRepositoryFactory factory;
    auto repository = factory.create();
    RelationBuilder builder { { ColumnIndex(1), ColumnIndex(2) } };
    builder.insert(cells(10, 11));
    builder.insert(cells(20, 21));
    const auto first = insert(repository, builder).first;
    builder.clear();
    builder.insert(cells(30, 31));
    builder.insert(cells(20, 21));
    const auto second = insert(repository, builder).first;

    EXPECT_NE(first.index(), second.index());
    EXPECT_EQ(repository.get_row_repository().size(), 3);
    EXPECT_EQ(repository.get_row_set_repository().size(), 2);
    EXPECT_TRUE(first.contains(cells(20, 21)));
    EXPECT_TRUE(second.contains(cells(20, 21)));
    EXPECT_EQ(first.at(1).bytes().data(), second.at(0).bytes().data());
    EXPECT_FALSE(first.contains(cells(30, 31)));
    EXPECT_FALSE(second.contains(cells(10, 11)));

    RelationBuilder difference_builder { { ColumnIndex(1), ColumnIndex(2) } };
    database::difference(second, first, difference_builder);
    expect_rows(difference_builder, { { 30, 31 } });
}

TEST(YggdrasilTests, DatabaseRelationViewsMixBuildersDataAndInternedIndicesInOperations)
{
    database::RelationRepositoryFactory factory;
    auto repository = factory.create();
    RelationBuilder left { { ColumnIndex(1), ColumnIndex(2) } };
    left.insert(cells(4, 40));
    left.insert(cells(5, 50));
    RelationBuilder right { { ColumnIndex(1), ColumnIndex(3) } };
    right.insert(cells(4, 400));
    right.insert(cells(6, 600));
    const auto right_view = insert(repository, right).first;
    auto right_data = right_view.data();
    const auto left_builder_view = urd::resolve(left, repository);
    const auto right_data_view = urd::resolve(right_data, repository);
    const auto right_index_view = repository.view(right_view.index());
    // Trivially copyable records are held by value; others are borrowed.
    static_assert(std::is_trivially_copyable_v<urd::data<RelationTag>>);
    EXPECT_TRUE(urd::EqualTo<std::remove_cvref_t<decltype(right_data)>> {}(right_data_view.data(), right_data));
    EXPECT_EQ(&left_builder_view.data(), &left);
    EXPECT_EQ(&right_index_view.context(), &repository);
    const auto check_access = [](const auto& view)
    {
        EXPECT_TRUE(view.contains(cells(4, 400)));
        EXPECT_FALSE(view.contains(cells(4, 401)));
        EXPECT_THROW((void) view.contains(cells(4)), std::invalid_argument);
        EXPECT_THROW((void) view.at(view.size()), std::out_of_range);
    };
    check_access(right_data_view);
    check_access(right_index_view);

    database::Workspace workspace;
    RelationBuilder joined { { ColumnIndex(1), ColumnIndex(2), ColumnIndex(3) } };
    database::join(left_builder_view, right_data_view, joined, workspace);
    expect_rows(joined, { { 4, 40, 400 } });
    database::join(left_builder_view, right_index_view, joined, workspace);
    expect_rows(joined, { { 4, 40, 400 } });


    RelationBuilder projected { { ColumnIndex(3) } };
    database::project(right_data_view, { ColumnIndex(3) }, projected, workspace);
    expect_rows(projected, { { 400 }, { 600 } });
    RelationBuilder selected { { ColumnIndex(1), ColumnIndex(3) } };
    database::select(right_index_view, [](database::Row row) { return row.get<uint_t>(size_t { 0 }) == 4; }, selected);
    expect_rows(selected, { { 4, 400 } });
    database::union_(right_data_view, right_index_view, selected);
    expect_rows(selected, { { 4, 400 }, { 6, 600 } });
    database::difference(right_data_view, right_index_view, selected);
    EXPECT_TRUE(selected.empty());

    const urd::builder<database::Columns> labels { ColumnIndex(1), ColumnIndex(30) };
    const auto renamed = repository.rename(right_index_view, labels.span());
    EXPECT_EQ(renamed.get_storage_address(), right_index_view.get_storage_address());
    EXPECT_EQ(renamed.get_storage_index(), right_index_view.get_storage_index());
    RelationBuilder renamed_projection { { ColumnIndex(30) } };
    database::project(renamed, { ColumnIndex(30) }, renamed_projection, workspace);
    expect_rows(renamed_projection, { { 400 }, { 600 } });

    right.rename(labels.span());
    const auto borrowed = urd::resolve(right, repository);
    static_assert(database::RelationViewConcept<decltype(borrowed)>);
    EXPECT_EQ(borrowed.get_storage_address(), &right.storage());
    database::project(borrowed, { ColumnIndex(30) }, renamed_projection, workspace);
    expect_rows(renamed_projection, { { 400 }, { 600 } });
}

TEST(YggdrasilTests, DatabaseInternedRowsOutliveBuilderReuseAndRenamesShareStorage)
{
    database::RelationPoolFactory row_factory;
    auto builders = database::RelationPool(row_factory);
    database::RelationRepositoryFactory factory(row_factory);
    auto repository = factory.create();
    auto builder = builders.get_or_allocate({ ColumnIndex(1), ColumnIndex(2) });
    builder->insert(cells(7, 70));
    builder->insert(cells(8, 80));
    const auto original = insert(repository, *builder).first;
    const auto* builder_address = &*builder;
    builder = {};
    builder = builders.get_or_allocate({ ColumnIndex(4), ColumnIndex(5) });
    EXPECT_EQ(&*builder, builder_address);
    builder->insert(cells(99, 100));
    expect_rows(original, { { 7, 70 }, { 8, 80 } });
    EXPECT_NE(original.at(0).bytes().data(), builder->at(0).bytes().data());

    const auto renamed = [&]()
    {
        const urd::builder<database::Columns> temporary_columns { ColumnIndex(4), ColumnIndex(5) };
        return repository.rename(original, temporary_columns.span());
    }();
    const urd::builder<database::Columns> renamed_columns { ColumnIndex(4), ColumnIndex(5) };
    EXPECT_NE(original.index(), renamed.index());
    EXPECT_EQ(original.at(0).bytes().data(), renamed.at(0).bytes().data());
    EXPECT_EQ(original.get_storage_index(), renamed.get_storage_index());
    EXPECT_NE(original.get_storage_index(), builder->get_storage_index());
    EXPECT_TRUE(std::ranges::equal(renamed.columns(), renamed_columns.span()));
    EXPECT_TRUE(urd::EqualTo<IndexView> {}(repository.rename(original, renamed_columns.span()), renamed));
    expect_rows(renamed, { { 7, 70 }, { 8, 80 } });

    RelationBuilder probe { { ColumnIndex(4), ColumnIndex(6) } };
    probe.insert(cells(7, 700));
    RelationBuilder joined { { ColumnIndex(4), ColumnIndex(5), ColumnIndex(6) } };
    database::JoinPlan plan(renamed.columns(), probe.columns());
    database::JoinIndexCache cache;
    database::Workspace workspace;
    database::join(renamed, urd::resolve(probe, repository), plan, cache, database::JoinReuse { true, false }, joined, workspace);
    expect_rows(joined, { { 7, 70, 700 } });
    EXPECT_EQ(cache.size(), 1);
}

TEST(YggdrasilTests, DatabaseRelationRepositoryViewsKeepRepositoryIdentity)
{
    database::RelationPoolFactory row_factory;
    database::RelationRepositoryFactory factory(row_factory);
    auto first_repository = factory.create();
    auto second_repository = factory.create();
    RelationBuilder builder { { ColumnIndex(1) } };
    builder.insert(cells(12));
    const auto first = insert(first_repository, builder).first;
    const auto second = insert(second_repository, builder).first;
    EXPECT_EQ(first.index(), second.index());
    EXPECT_NE(first_repository.get_index(), second_repository.get_index());
    EXPECT_FALSE(urd::EqualTo<IndexView> {}(first, second));
    EXPECT_NE(first.get_storage_index(), second.get_storage_index());

    auto data = first.data();
    const auto original_hash = urd::Hash<urd::data<RelationTag>> {}(data);
    data.index = urd::index<RelationTag>(123);
    EXPECT_EQ(urd::Hash<urd::data<RelationTag>> {}(data), original_hash);
    EXPECT_TRUE(urd::EqualTo<urd::data<RelationTag>> {}(data, first.data()));
    const auto bytes = cista::serialize(data);
    auto relocated = bytes;
    const auto* round_trip = cista::deserialize<urd::data<RelationTag>>(relocated);
    EXPECT_EQ(round_trip->index, data.index);
    EXPECT_TRUE(urd::EqualTo<urd::data<RelationTag>> {}(*round_trip, data));
}

TEST(YggdrasilTests, DatabaseRelationRepositoryInternsCanonicalEncodings)
{
    database::RelationRepositoryFactory factory;
    auto repository = factory.create();
    urd::builder<database::Columns> columns;
    columns.assign<double>({ ColumnIndex(1), ColumnIndex(2) });
    const double first_nan = std::bit_cast<double>(uint64_t { 0x7ff8000000000001 });
    const double second_nan = std::bit_cast<double>(uint64_t { 0xfff8000000000042 });
    urd::builder<database::Relation> first { columns };
    first.insert(std::tuple { -0.0, 1.0 });
    first.insert(std::tuple { first_nan, 2.0 });
    urd::builder<database::Relation> equivalent { columns };
    equivalent.insert(std::tuple { second_nan, 2.0 });
    equivalent.insert(std::tuple { 0.0, 1.0 });
    const auto [original, created] = insert(repository, first);
    const auto [duplicate, duplicate_created] = insert(repository, equivalent);
    EXPECT_TRUE(created);
    EXPECT_FALSE(duplicate_created);
    EXPECT_EQ(original.index(), duplicate.index());
    EXPECT_EQ(original.at(0).bytes().data(), duplicate.at(0).bytes().data());
    EXPECT_FALSE(std::signbit(original.at(0).get<double>(size_t { 0 })));
    EXPECT_EQ(repository.size(), 1);
}

TEST(YggdrasilTests, DatabaseRelationRepositoryRejectsInvalidDataAndForeignRename)
{
    database::RelationRepositoryFactory factory;
    auto repository = factory.create();
    auto foreign_repository = factory.create();
    RelationBuilder builder { { ColumnIndex(1), ColumnIndex(2) } };
    builder.insert(cells(10, 20));
    builder.insert(cells(30, 40));
    const auto original = insert(repository, builder).first;
    urd::data<RelationTag> empty_data;
    EXPECT_TRUE(empty_data.columns_index.is_none());
    EXPECT_TRUE(empty_data.row_set_index.is_none());
    EXPECT_THROW(repository.insert(empty_data), std::invalid_argument);
    auto data = original.data();
    urd::clear_members(data);
    EXPECT_TRUE(data.index.is_none());
    EXPECT_TRUE(data.columns_index.is_none());
    EXPECT_TRUE(data.row_set_index.is_none());
    EXPECT_THROW(repository.insert(data), std::invalid_argument);
    data = original.data();
    data.columns_index = ColumnsIndex();
    EXPECT_THROW(repository.insert(data), std::invalid_argument);
    data = original.data();
    data.row_set_index = RowSetIndex();
    EXPECT_THROW(repository.insert(data), std::invalid_argument);

    const std::array<ColumnIndex, 1> unary_schema { ColumnIndex(1) };
    data = original.data();
    data.columns_index = repository.insert(std::span(unary_schema)).first.index();
    EXPECT_THROW(repository.insert(data), std::invalid_argument);
    const std::array<ColumnIndex, 2> duplicate_schema { ColumnIndex(1), ColumnIndex(1) };
    const std::span<const ColumnIndex> duplicate_view { std::span(duplicate_schema) };
    EXPECT_THROW(builder.rename(duplicate_view), std::invalid_argument);
    EXPECT_THROW(builder.initialize(duplicate_view), std::invalid_argument);
    EXPECT_EQ(builder.index(), original.index());
    EXPECT_TRUE(std::ranges::equal(builder.columns(), original.columns()));
    expect_rows(builder, { { 10, 20 }, { 30, 40 } });
    urd::data<database::Columns> invalid_schema;
    invalid_schema.values.push_back(database::ColumnLayout { ColumnIndex(1), ygg::type_id<uint_t>, 0, sizeof(uint_t) });
    invalid_schema.values.push_back(database::ColumnLayout { ColumnIndex(1), ygg::type_id<uint_t>, sizeof(uint_t), sizeof(uint_t) });
    EXPECT_THROW(repository.insert(invalid_schema), std::invalid_argument);
    EXPECT_TRUE(invalid_schema.index.is_none());

    const auto foreign = insert(foreign_repository, builder).first;
    ASSERT_EQ(foreign.index(), original.index());
    EXPECT_THROW(repository.rename(foreign, original.columns()), std::invalid_argument);
    EXPECT_THROW(repository.rename(original, std::span<const ColumnIndex>(unary_schema)), std::invalid_argument);
    EXPECT_THROW(repository.rename(original, duplicate_view), std::invalid_argument);
    EXPECT_EQ(repository.size(), 1);
    EXPECT_EQ(original.columns()[0].label, ColumnIndex(1));
    EXPECT_EQ(original.columns()[1].label, ColumnIndex(2));
    expect_rows(original, { { 10, 20 }, { 30, 40 } });
}

TEST(YggdrasilTests, DatabaseConversionsRemapStoredIdentityAndReuseMutableStorage)
{
    database::RelationRepositoryFactory source_factory;
    database::RelationRepositoryFactory target_factory;
    auto source_repository = source_factory.create();
    auto target_repository = target_factory.create();
    RelationBuilder source_builder { { ColumnIndex(7), ColumnIndex(3) } };
    source_builder.insert(cells(4, 5));
    source_builder.insert(cells(1, 2));
    const auto source = insert(source_repository, source_builder, 42).first;
    RelationBuilder seed { { ColumnIndex(99) } };
    seed.insert(cells(77));
    (void) insert(target_repository, seed);

    const auto [target, created] = database::copy(source, target_repository);
    EXPECT_TRUE(created);
    EXPECT_EQ(&target.context(), &target_repository);
    EXPECT_NE(target.data().columns_index, source.data().columns_index);
    EXPECT_NE(target.data().row_set_index, source.data().row_set_index);
    EXPECT_EQ(target.data().schema_namespace, 42);
    EXPECT_TRUE(std::ranges::equal(target.columns(), source.columns()));
    expect_rows(target, { { 4, 5 }, { 1, 2 } });
    EXPECT_NE(target.at(0).bytes().data(), source.at(0).bytes().data());
    EXPECT_FALSE(database::copy(source, target_repository).second);
    const auto [same, same_created] = database::copy(target, target_repository);
    EXPECT_FALSE(same_created);
    EXPECT_EQ(same.index(), target.index());

    auto pending = source.data();
    pending.index = urd::index<RelationTag>(1234);
    pending.schema_namespace = 43;
    const auto changed = database::copy(urd::resolve(pending, source_repository), target_repository);
    EXPECT_TRUE(changed.second);
    EXPECT_EQ(changed.first.data().schema_namespace, 43);
    EXPECT_EQ(pending.index, urd::index<RelationTag>(1234));

    database::RelationPool pool;
    auto output_handle = pool.get_or_allocate({ ColumnIndex(3), ColumnIndex(7) });
    auto& output = *output_handle;
    const auto storage_index = output.get_storage_index();
    output.insert(cells(99, 88));
    assign(output, source);
    EXPECT_TRUE(std::ranges::equal(output.columns(), source.columns()));
    expect_rows(output, { { 4, 5 }, { 1, 2 } });
    const auto capacity = output.memory_usage();
    const auto* columns_storage = output.columns().data();
    const auto canonical = insert(source_repository, output).first;
    EXPECT_FALSE(output.index().is_none());
    EXPECT_EQ(&assign(output, target), &output);
    EXPECT_TRUE(output.index().is_none());
    EXPECT_EQ(output.memory_usage(), capacity);
    EXPECT_EQ(output.columns().data(), columns_storage);
    EXPECT_EQ(output.get_storage_index(), storage_index);
    expect_rows(output, { { 4, 5 }, { 1, 2 } });

    const auto* first_row = output.row(0).data();
    (void) insert(source_repository, output);
    EXPECT_EQ(&assign(output, output), &output);
    EXPECT_TRUE(output.index().is_none());
    EXPECT_EQ(output.row(0).data(), first_row);
    (void) insert(source_repository, output);
    EXPECT_EQ(&assign(output, urd::resolve(output, source_repository)), &output);
    EXPECT_TRUE(output.index().is_none());
    EXPECT_EQ(output.row(0).data(), first_row);
    EXPECT_EQ(output.columns().data(), columns_storage);
    EXPECT_EQ(output.memory_usage(), capacity);
    EXPECT_EQ(output.get_storage_index(), storage_index);
    expect_rows(output, { { 4, 5 }, { 1, 2 } });

    // A changed arity replaces storage; row values follow the source's column order.
    assign(seed, source_builder);
    EXPECT_TRUE(seed.index().is_none());
    EXPECT_TRUE(std::ranges::equal(seed.columns(), source.columns()));
    expect_rows(seed, { { 4, 5 }, { 1, 2 } });
    assign(seed, urd::resolve(pending, source_repository));
    expect_rows(seed, { { 4, 5 }, { 1, 2 } });
    EXPECT_EQ(canonical.data().schema_namespace, 0);

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
    nullary.insert(cells());
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
    auto repository = database::RelationRepositoryFactory().create();
    auto builder = RelationBuilder { ColumnIndex(0) };
    builder.insert(cells(10));
    builder.insert(cells(20));
    builder.insert(cells(30));
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
        EXPECT_TRUE(std::ranges::equal((*copy).bytes(), view[0].bytes()));
        EXPECT_TRUE(std::ranges::equal((*it).bytes(), view[1].bytes()));
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
        EXPECT_TRUE(std::ranges::equal((end[-1]).bytes(), view[2].bytes()));
        EXPECT_TRUE(std::ranges::equal(((begin + 1)[-1]).bytes(), view[0].bytes()));
    };
    check(urd::resolve(builder, repository));
    check(urd::resolve(stored.data(), repository));
    check(repository.view(stored.index()));

    auto nullary = RelationBuilder {};
    const auto check_empty = [](auto view)
    {
        EXPECT_EQ(view.begin(), view.end());
        EXPECT_EQ(view.end() - view.begin(), 0);
    };
    const auto empty = database::insert(repository, nullary).first;
    check_empty(urd::resolve(nullary, repository));
    check_empty(urd::resolve(empty.data(), repository));
    check_empty(repository.view(empty.index()));
    nullary.insert(cells());
    const auto unit = database::insert(repository, nullary).first;
    const auto check_unit = [](auto view)
    {
        using V = decltype(view);
        const auto it = V(view).end() - 1;
        EXPECT_TRUE((*it).empty());
        EXPECT_EQ(it + 1, view.end());
    };
    check_unit(urd::resolve(nullary, repository));
    check_unit(urd::resolve(unit.data(), repository));
    check_unit(repository.view(unit.index()));
}

namespace
{
/// Columns store indices as uint_t; their owner resolves them.
template<typename Repository>
auto resolve_in(uint_t value, const Repository& repository)
{ return urd::resolve(ColumnsIndex(value), repository); }
}

TEST(YggdrasilTests, TypedRelationRowsLeaveIndicesOfOtherRepositoriesRaw)
{
    auto entities = database::RelationRepositoryFactory().create();
    const auto first = entities.insert(std::array { ColumnIndex(7) }).first;
    const auto second = entities.insert(std::array { ColumnIndex(9) }).first;
    const auto first_value = first.index().value;
    const auto second_value = second.index().value;
    auto rows = database::RelationRepositoryFactory().create();
    urd::builder<database::Columns> columns;
    columns.push_back<uint_t>(ColumnIndex(1));
    columns.push_back<uint_t>(ColumnIndex(2));
    auto builder = urd::builder<database::Relation>(columns);
    builder.insert(std::tuple { first_value, second_value });
    const auto published = database::insert(rows, builder, 42).first;
    const auto index_view = rows.view(published.index());
    const auto data_view = urd::resolve(published.data(), rows);
    // Even in the repository that owns the values, a row leaves them raw.
    const auto builder_view = urd::resolve(builder, entities);
    static_assert(database::RelationViewConcept<decltype(index_view)>);
    static_assert(std::ranges::forward_range<decltype(index_view)>);
    static_assert(std::ranges::borrowed_range<decltype(index_view)>);
    static_assert(std::same_as<decltype(index_view[0].get<uint_t>(size_t { 0 })), uint_t>);
    static_assert(std::same_as<decltype(builder_view[0].get<uint_t>(size_t { 0 })), uint_t>);
    const auto check = [&](const auto& view)
    {
        ASSERT_EQ(view.size(), 1);
        EXPECT_EQ((database::Row(view.row(0), view.columns().span()).get<uint_t>(size_t { 0 })), first_value);
        // Rows of interned relations name values owned elsewhere; callers resolve them explicitly.
        EXPECT_EQ(resolve_in(view[0].template get<uint_t>(size_t { 0 }), entities), first);
        EXPECT_EQ(resolve_in(view.at(0).template get<uint_t>(size_t { 1 }), entities), second);
        EXPECT_THROW((void) view.at(1), std::out_of_range);
        size_t count = 0;
        for (auto objects : view)
        {
            EXPECT_EQ(resolve_in(objects.template get<uint_t>(size_t { 0 }), entities), first);
            EXPECT_EQ(resolve_in(objects.template get<uint_t>(size_t { 1 }), entities), second);
            ++count;
        }
        EXPECT_EQ(count, 1);
    };
    check(index_view);
    check(data_view);
    check(builder_view);
    const auto iterator = index_view.begin();
    EXPECT_EQ((*iterator).get<uint_t>(size_t { 1 }), second_value);
    const auto resolved = index_view[0].visit(size_t { 0 }, [](auto value) { return static_cast<uint_t>(value); });
    EXPECT_EQ(resolved, first_value);

    const auto renamed = rows.rename(index_view, std::array { ColumnIndex(3), ColumnIndex(4) });
    static_assert(std::same_as<decltype(renamed), decltype(index_view)>);
    EXPECT_EQ(&renamed.context(), &rows);
    EXPECT_EQ(renamed.data().schema_namespace, 42);
    EXPECT_EQ(renamed.get_storage_address(), index_view.get_storage_address());
    EXPECT_EQ(renamed[0].get<uint_t>(size_t { 0 }), first_value);
    auto other_rows = database::RelationRepositoryFactory().create();
    EXPECT_THROW(other_rows.rename(index_view, std::array { ColumnIndex(3), ColumnIndex(4) }), std::invalid_argument);

    urd::builder<database::Columns> probe_columns;
    probe_columns.push_back<uint_t>(ColumnIndex(2));
    auto probe = urd::builder<database::Relation>(probe_columns);
    probe.insert(std::tuple { second_value });
    auto joined = database::join(data_view, urd::resolve(probe, entities));
    EXPECT_TRUE(joined.contains(std::tuple { first_value, second_value }));
    auto projected = database::project(index_view, { ColumnIndex(2) });
    EXPECT_TRUE(projected.contains(std::tuple { second_value }));
    auto assigned = urd::builder<database::Relation>();
    assign(assigned, index_view);
    EXPECT_TRUE(assigned.contains(std::tuple { first_value, second_value }));
    const auto copied = database::copy(index_view, other_rows).first;
    EXPECT_TRUE(copied.contains(std::tuple { first_value, second_value }));

    auto truth = urd::builder<database::Relation>();
    truth.insert(cells<uint_t>());
    const auto truth_view = urd::resolve(truth, entities);
    EXPECT_TRUE((*truth_view.begin()).empty());
    EXPECT_EQ(std::ranges::distance(truth_view), 1);
    truth.clear();
    EXPECT_EQ(truth_view.begin(), truth_view.end());
}

namespace
{
template<typename Destination, typename Source>
concept CanAssignColumns = requires(Destination& destination, const Source& source) { database::assign(destination, source); };

template<typename Source, typename Destination>
concept CanCopyColumns = requires(const Source& source, Destination& destination) { database::copy(source, destination); };

struct UnsupportedColumn
{
    void* pointer;
};

template<typename Visitor>
concept CanVisitColumnType = requires(Visitor&& visitor) { ygg::visit_type_id(ygg::TypeId::uint32, std::forward<Visitor>(visitor)); };

static_assert(CanVisitColumnType<decltype([](auto) { return size_t {}; })>);
static_assert(!CanVisitColumnType<decltype([](std::type_identity<uint_t>) { return size_t {}; })>);
static_assert(!CanVisitColumnType<int>);

static_assert(ygg::ByteCodable<uint_t> && ygg::ByteCodable<double> && ygg::ByteCodable<bool>);
static_assert(!ygg::ByteCodable<ColumnsIndex> && !ygg::ByteCodable<const uint_t> && !ygg::ByteCodable<UnsupportedColumn>);
static_assert(ygg::type_id<std::uint32_t> == ygg::TypeId::uint32 && ygg::type_id<bool> == ygg::TypeId::boolean);
static_assert(ygg::byte_size(ygg::type_id<double>) == 8 && ygg::byte_size(ygg::TypeId { 0 }) == 0);
}

TEST(YggdrasilTests, DatabaseMixedWidthRowsPreserveTypedValuesAndResolveIndices)
{
    auto entities = database::RelationRepositoryFactory().create();
    const auto entity = entities.insert(std::array { ColumnIndex(11) }).first;
    urd::builder<database::Columns> columns;
    columns.push_back<uint_t>(ColumnIndex(4));
    columns.push_back<double>(ColumnIndex(9));
    columns.push_back<bool>(ColumnIndex(2));
    columns.push_back<uint_t>(ColumnIndex(7));
    ASSERT_EQ(columns.size(), 4);
    EXPECT_EQ(columns.row_size(), 2 * ygg::ByteCodec<uint_t>::size + sizeof(double) + 1);
    EXPECT_EQ(columns[0].offset, 0);
    EXPECT_EQ(columns[1].offset, ygg::ByteCodec<uint_t>::size);
    EXPECT_EQ(columns[2].offset, ygg::ByteCodec<uint_t>::size + sizeof(double));
    EXPECT_EQ(columns[3].offset, ygg::ByteCodec<uint_t>::size + sizeof(double) + 1);
    EXPECT_EQ(columns[3].size, ygg::ByteCodec<uint_t>::size);

    auto rows = database::RelationRepositoryFactory().create();
    urd::builder<database::Relation> builder(columns);
    const auto tuple = std::tuple { uint_t { 42 }, 1.25, true, entity.index().value };
    builder.insert(tuple);
    const auto stored = database::insert(rows, builder).first;
    const auto verify = [&](const auto& view)
    {
        const auto row = view[0];
        EXPECT_EQ(row.template get<uint_t>(ColumnIndex(4)), 42);
        EXPECT_DOUBLE_EQ(row.template get<double>(ColumnIndex(9)), 1.25);
        EXPECT_TRUE(row.template get<bool>(ColumnIndex(2)));
        EXPECT_EQ(resolve_in(row.template get<uint_t>(ColumnIndex(7)), entities), entity);
        EXPECT_THROW((void) row.template get<double>(ColumnIndex(4)), std::invalid_argument);
        EXPECT_THROW((void) row.template get<uint_t>(ColumnIndex(99)), std::out_of_range);
        EXPECT_THROW((void) row.template get<uint_t>(size_t { 4 }), std::out_of_range);
        EXPECT_TRUE(row.visit(3,
                              [&](auto value)
                              {
                                  if constexpr (std::same_as<decltype(value), uint_t>)
                                      return ColumnsIndex(value) == entity.index();
                                  else
                                      return false;
                              }));
    };
    verify(urd::resolve(builder, entities));
    verify(urd::resolve(stored.data(), rows));
    verify(rows.view(stored.index()));
    EXPECT_EQ(ColumnsIndex(builder[0].get<uint_t>(ColumnIndex(7))), entity.index());
    EXPECT_TRUE(stored.contains(tuple));

    const auto original_bytes = cista::serialize(columns);
    auto relocated_bytes = original_bytes;
    const auto* restored = cista::deserialize<urd::builder<database::Columns>>(relocated_bytes);
    EXPECT_TRUE(std::ranges::equal(columns.span(), restored->span()));
    EXPECT_EQ(restored->row_size(), columns.row_size());
    auto encoded = database::encode_row(tuple, restored->span());
    EXPECT_DOUBLE_EQ(restored->read<double>(encoded, ColumnIndex(9)), 1.25);
    restored->write(encoded, ColumnIndex(9), -2.5);
    EXPECT_DOUBLE_EQ(restored->read<double>(encoded, ColumnIndex(9)), -2.5);

    urd::data<database::Columns> data;
    data.values.set(columns.span().begin(), columns.span().end());
    auto data_bytes = cista::serialize(data);
    const auto* restored_data = cista::deserialize<urd::data<database::Columns>>(data_bytes);
    EXPECT_TRUE(std::ranges::equal(restored_data->values, columns.span()));
}

TEST(YggdrasilTests, DatabaseFloatingPointRowsInternCanonicalNaNsAndSignedZero)
{
    urd::builder<database::Columns> columns;
    columns.push_back<double>(ColumnIndex(1));
    columns.push_back<bool>(ColumnIndex(2));
    urd::builder<database::Relation> builder(columns);
    const double first_nan = std::bit_cast<double>(uint64_t { 0x7ff8000000000001 });
    const double second_nan = std::bit_cast<double>(uint64_t { 0xfff8000000000042 });
    EXPECT_EQ(builder.insert(std::tuple { -0.0, false }), 0);
    EXPECT_EQ(builder.insert(std::tuple { 0.0, false }), 0);
    EXPECT_EQ(builder.insert(std::tuple { first_nan, true }), 1);
    EXPECT_EQ(builder.insert(std::tuple { second_nan, true }), 1);
    EXPECT_EQ(builder.size(), 2);
    EXPECT_FALSE(std::signbit(builder[0].get<double>(ColumnIndex(1))));
    EXPECT_TRUE(std::isnan(builder[1].get<double>(ColumnIndex(1))));

    auto repository = database::RelationRepositoryFactory().create();
    const auto first = database::insert(repository, builder).first;
    builder.clear();
    builder.insert(std::tuple { second_nan, true });
    builder.insert(std::tuple { +0.0, false });
    const auto [second, created] = database::insert(repository, builder);
    EXPECT_FALSE(created);
    EXPECT_EQ(first.index(), second.index());
    EXPECT_TRUE(second.contains(std::tuple { first_nan, true }));

    const auto canonical = builder.index();
    const auto invalid = database::encode_row(std::tuple { +0.0, false }, columns.span());
    const auto truncated = std::span<const std::byte>(invalid).first(invalid.size() - 1);
    EXPECT_THROW(builder.insert(truncated), std::invalid_argument);
    EXPECT_THROW((void) builder.contains(truncated), std::invalid_argument);
    EXPECT_EQ(builder.index(), canonical);
    EXPECT_EQ(builder.size(), 2);
}

TEST(YggdrasilTests, DatabaseTypedSchemaRejectsMalformedMetadataBeforeMutation)
{
    urd::builder<database::Columns> columns;
    columns.push_back<uint_t>(ColumnIndex(1));
    columns.push_back<double>(ColumnIndex(2));
    urd::builder<database::Relation> relation(columns);
    relation.insert(std::tuple { uint_t { 7 }, 3.25 });
    const auto canonical = urd::index<database::Relation>(5);
    relation.set_index(canonical);
    auto invalid = urd::Store<urd::sequence<database::ColumnLayout>>(columns.begin(), columns.end());
    invalid[1].offset += 1;
    EXPECT_THROW(relation.initialize(std::span(invalid)), std::invalid_argument);
    invalid.assign(columns.begin(), columns.end());
    invalid[1].size -= 1;
    EXPECT_THROW(relation.initialize(std::span(invalid)), std::invalid_argument);
    invalid.assign(columns.begin(), columns.end());
    invalid[1].type = ygg::type_id<bool>;
    EXPECT_THROW(relation.initialize(std::span(invalid)), std::invalid_argument);
    invalid[1].type = ygg::TypeId { 99 };
    EXPECT_THROW(relation.initialize(std::span(invalid)), std::invalid_argument);
    EXPECT_THROW(relation.insert(std::tuple { 3.25, uint_t { 7 } }), std::invalid_argument);
    EXPECT_EQ(relation.index(), canonical);
    ASSERT_EQ(relation.size(), 1);
    EXPECT_TRUE(relation.contains(std::tuple { uint_t { 7 }, 3.25 }));
}

TEST(YggdrasilTests, DatabaseBuiltinCodecsPreserveSignedAndWideValues)
{
    urd::builder<database::Columns> columns;
    columns.push_back<uint32_t>(ColumnIndex(0));
    columns.push_back<int32_t>(ColumnIndex(1));
    columns.push_back<uint64_t>(ColumnIndex(2));
    columns.push_back<int64_t>(ColumnIndex(3));
    columns.push_back<float>(ColumnIndex(4));
    columns.push_back<double>(ColumnIndex(5));
    columns.push_back<bool>(ColumnIndex(6));
    urd::builder<database::Relation> relation(columns);
    const auto values = std::tuple { std::numeric_limits<uint32_t>::max(),
                                     std::numeric_limits<int32_t>::min(),
                                     std::numeric_limits<uint64_t>::max(),
                                     std::numeric_limits<int64_t>::min(),
                                     1.25F,
                                     std::numeric_limits<double>::infinity(),
                                     true };
    relation.insert(values);
    const auto row = relation[0];
    EXPECT_EQ(row.bytes().size(), 37);
    EXPECT_EQ(row.get<uint32_t>(size_t { 0 }), std::get<0>(values));
    EXPECT_EQ(row.get<int32_t>(size_t { 1 }), std::get<1>(values));
    EXPECT_EQ(row.get<uint64_t>(size_t { 2 }), std::get<2>(values));
    EXPECT_EQ(row.get<int64_t>(size_t { 3 }), std::get<3>(values));
    EXPECT_FLOAT_EQ(row.get<float>(size_t { 4 }), 1.25F);
    EXPECT_EQ(row.get<double>(size_t { 5 }), std::numeric_limits<double>::infinity());
    EXPECT_TRUE(row.get<bool>(size_t { 6 }));
    EXPECT_EQ(relation.insert(row), 0);
    EXPECT_TRUE(relation.contains(values));
}

TEST(YggdrasilTests, HomogeneousRowsRoundTripThroughReusableBuffers)
{
    urd::builder<database::Columns> columns;
    columns.push_back<std::uint64_t>(ColumnIndex(1));
    columns.push_back<std::uint64_t>(ColumnIndex(2));
    auto bytes = urd::Store<urd::sequence<std::byte>, urd::contiguous> {};
    const auto values = urd::Store<urd::sequence<std::uint64_t>> { 4, 9 };
    const auto encoded = database::encode_row<std::uint64_t>(values, bytes);
    EXPECT_EQ(encoded.data(), bytes.data());
    EXPECT_EQ(encoded.size(), columns.row_size());

    auto decoded = urd::Store<urd::sequence<std::uint64_t>, urd::contiguous> {};
    const auto row = database::Row(encoded, columns.span());
    EXPECT_TRUE(std::ranges::equal(row.get_all(decoded), values));
    EXPECT_EQ(row.get_all(decoded).data(), decoded.data());

    auto relation = urd::builder<database::Relation>(columns);
    relation.insert(std::tuple { std::uint64_t { 4 }, std::uint64_t { 9 } });
    EXPECT_TRUE(relation.contains(encoded));
}

}  // namespace ygg::tests
