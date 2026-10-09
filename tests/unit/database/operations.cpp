/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/operations.hpp"

#include "yggdrasil/database/relation_pool.hpp"
#include "yggdrasil/database/relation_repository.hpp"

#include <array>
#include <cista/serialization.h>
#include <gtest/gtest.h>
#include <initializer_list>
#include <limits>
#include <random>
#include <ranges>
#include <set>
#include <span>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace ygg::tests
{
struct DatabaseCountedValue
{
    uint_t value;
    static inline size_t encodes = 0;
};

struct DatabaseModuloValue
{
    uint_t value;
};

struct DatabaseThrowingValue
{
    uint_t value;
    static inline bool fail = false;
};
}  // namespace ygg::tests

namespace ygg::database
{
template<>
struct ColumnCodec<tests::DatabaseCountedValue>
{
    static constexpr size_t size = ColumnCodec<uint_t>::size;
    static void encode(tests::DatabaseCountedValue value, std::span<std::byte> bytes)
    {
        ++tests::DatabaseCountedValue::encodes;
        ColumnCodec<uint_t>::encode(value.value, bytes);
    }
    static tests::DatabaseCountedValue decode(std::span<const std::byte> bytes) { return { ColumnCodec<uint_t>::decode(bytes) }; }
};

template<>
struct ColumnCodec<tests::DatabaseModuloValue>
{
    static constexpr size_t size = ColumnCodec<uint_t>::size;
    static void encode(tests::DatabaseModuloValue value, std::span<std::byte> bytes) { ColumnCodec<uint_t>::encode(value.value % 10, bytes); }
    static tests::DatabaseModuloValue decode(std::span<const std::byte> bytes) { return { ColumnCodec<uint_t>::decode(bytes) }; }
};

template<>
struct ColumnCodec<tests::DatabaseThrowingValue>
{
    static constexpr size_t size = ColumnCodec<uint_t>::size;
    static void encode(tests::DatabaseThrowingValue value, std::span<std::byte> bytes)
    {
        if (tests::DatabaseThrowingValue::fail && value.value == 5)
            throw std::runtime_error("encoding failed");
        ColumnCodec<uint_t>::encode(value.value, bytes);
    }
    static tests::DatabaseThrowingValue decode(std::span<const std::byte> bytes) { return { ColumnCodec<uint_t>::decode(bytes) }; }
};
}  // namespace ygg::database

namespace ygg::tests
{

using namespace database;
using Values = TypeList<uint_t>;
using ColumnIndex = Index<database::Column>;

static_assert(database::detail::ColumnSliceBuffer<std::vector<ColumnSlice>>);
static_assert(database::detail::ColumnSliceBuffer<::cista::offset::vector<ColumnSlice>>);
static_assert(!database::detail::ColumnSliceBuffer<std::span<ColumnSlice>>);

static_assert(!std::is_copy_constructible_v<Builder<Relation<Values>>>);
static_assert(std::is_move_constructible_v<Builder<Relation<Values>>>);
static_assert(RelationViewConcept<Builder<Relation<Values>>, Values>);

static_assert(!std::same_as<ColumnIndex, uint_t>);
static_assert(!std::is_convertible_v<uint_t, ColumnIndex> && !std::is_convertible_v<ColumnIndex, uint_t>);
static_assert(sizeof(ColumnIndex) == sizeof(uint_t));
static_assert(std::is_trivially_copyable_v<ColumnIndex>);

template<typename C>
concept CanBorrowColumns = requires(C&& columns) { std::forward<C>(columns).span(); };

static_assert(CanBorrowColumns<const Builder<Columns<Values>>&>);
static_assert(!CanBorrowColumns<Builder<Columns<Values>>>);
static_assert(!CanBorrowColumns<const Builder<Columns<Values>>>);

template<typename Owner>
concept CanBorrowRelationColumns = requires(Owner&& owner) { std::forward<Owner>(owner).columns(); };

template<typename Plan>
concept CanBorrowPlanColumns = requires(Plan&& plan) { std::forward<Plan>(plan).output_columns(); };

static_assert(CanBorrowRelationColumns<const Builder<Relation<Values>>&>);
static_assert(!CanBorrowRelationColumns<Builder<Relation<Values>>>);
static_assert(CanBorrowPlanColumns<const ProjectionPlan<Values>&>);
static_assert(!CanBorrowPlanColumns<ProjectionPlan<Values>>);
static_assert(CanBorrowPlanColumns<const JoinPlan<Values>&>);
static_assert(!CanBorrowPlanColumns<JoinPlan<Values>>);
static_assert(std::same_as<decltype(std::declval<const Builder<Relation<Values>>&>().columns()), const Builder<Columns<Values>>&>);
static_assert(std::same_as<decltype(std::declval<const ProjectionPlan<Values>&>().output_columns()), View<Builder<Columns<Values>>, ProjectionPlan<Values>>>);
static_assert(std::same_as<decltype(std::declval<const JoinPlan<Values>&>().output_columns()), View<Builder<Columns<Values>>, JoinPlan<Values>>>);

namespace
{
template<typename T = uint_t, typename... Args>
auto cells(Args... values)
{
    return std::tuple { T(values)... };
}

template<typename T>
auto packed(std::span<const T> values)
{
    std::vector<std::byte> result(values.size() * ColumnCodec<T>::size);
    for (size_t i = 0; i < values.size(); ++i)
        ColumnCodec<T>::encode(values[i], std::span(result).subspan(i * ColumnCodec<T>::size, ColumnCodec<T>::size));
    return result;
}

template<typename T>
auto relocated_serialization(T value)
{
    const auto original = cista::serialize(value);
    auto relocated = original;
    EXPECT_NE(relocated.data(), original.data());
    return relocated;
}

template<typename T>
void expect_relation(const Builder<Relation<TypeList<T>>>& relation,
                     std::initializer_list<ColumnIndex> columns,
                     std::initializer_list<std::initializer_list<std::type_identity_t<T>>> rows)
{
    std::vector<ColumnIndex> labels;
    for (const auto column : relation.columns())
        labels.push_back(column.label);
    EXPECT_EQ(labels, std::vector<ColumnIndex>(columns));
    ASSERT_EQ(relation.size(), rows.size());
    for (const auto row : rows)
        EXPECT_TRUE(relation.contains(packed(std::span<const T>(row.begin(), row.size()))));
}

std::set<std::vector<uint_t>> reference_join(const Builder<Relation<Values>>& lhs, const Builder<Relation<Values>>& rhs)
{
    std::vector<std::pair<size_t, size_t>> keys;
    std::vector<size_t> extra;
    for (size_t right = 0; right < rhs.arity(); ++right)
    {
        bool shared = false;
        for (size_t left = 0; left < lhs.arity(); ++left)
        {
            if (lhs.columns()[left].label == rhs.columns()[right].label)
            {
                keys.emplace_back(left, right);
                shared = true;
                break;
            }
        }
        if (!shared)
            extra.push_back(right);
    }
    std::set<std::vector<uint_t>> result;
    for (size_t left = 0; left < lhs.size(); ++left)
    {
        for (size_t right = 0; right < rhs.size(); ++right)
        {
            bool matches = true;
            for (const auto& [l, r] : keys)
                matches = matches && lhs[left].get<uint_t>(l) == rhs[right].get<uint_t>(r);
            if (!matches)
                continue;
            auto row = std::vector<uint_t>();
            for (size_t column = 0; column < lhs.arity(); ++column)
                row.push_back(lhs[left].get<uint_t>(column));
            for (const auto column : extra)
                row.push_back(rhs[right].get<uint_t>(column));
            result.insert(std::move(row));
        }
    }
    return result;
}

}  // namespace

TEST(YggdrasilTests, DatabaseColumnsOwnValidSchemasAndViewsBorrowLabels)
{
    std::vector<ColumnIndex> labels { ColumnIndex(4), ColumnIndex(9) };
    Builder<Columns<Values>> columns(labels);
    labels[0] = ColumnIndex(8);
    EXPECT_EQ(columns.span()[0].label, ColumnIndex(4));
    EXPECT_EQ(columns.size(), 2);
    EXPECT_FALSE(columns.empty());
    EXPECT_TRUE(Builder<Columns<Values>>().empty());
    auto repository = RelationRepositoryFactory<Values>().create();
    const auto borrowed = make_view(columns, repository);
    EXPECT_EQ(borrowed.data(), columns.span().data());
    EXPECT_EQ(borrowed.column_index(ColumnIndex(9)), 1);
    EXPECT_EQ(columns.column_index(ColumnIndex(4)), 0);
    EXPECT_THROW(columns.column_index(ColumnIndex(8)), std::out_of_range);
    EXPECT_THROW(borrowed.column_index(ColumnIndex(8)), std::out_of_range);
    auto copied = columns;
    Builder<Columns<Values>> copied_view(borrowed.span());
    EXPECT_NE(copied.span().data(), borrowed.data());
    EXPECT_NE(copied_view.span().data(), borrowed.data());
    EXPECT_EQ(copied_view.column_index(ColumnIndex(9)), 1);
    const Builder<Columns<Values>> replacement { ColumnIndex(5), ColumnIndex(6) };
    columns.assign(replacement.span());
    EXPECT_EQ(copied.column_index(ColumnIndex(4)), 0);
    EXPECT_EQ(copied_view.column_index(ColumnIndex(4)), 0);
    EXPECT_THROW((Builder<Columns<Values>> { ColumnIndex(4), ColumnIndex(4) }), std::invalid_argument);
    const std::array<ColumnIndex, 2> duplicates { ColumnIndex(4), ColumnIndex(4) };
    const std::span<const ColumnIndex> duplicate_view { std::span(duplicates) };
    EXPECT_EQ(duplicate_view.data(), duplicates.data());
    EXPECT_THROW((Builder<Columns<Values>>(duplicate_view)), std::invalid_argument);
    const auto* storage = columns.span().data();
    EXPECT_THROW(columns.assign(duplicate_view), std::invalid_argument);
    EXPECT_EQ(columns.span().data(), storage);
    EXPECT_TRUE(std::ranges::equal(columns.span(), replacement.span()));
    EXPECT_THROW((Builder<Relation<Values>>(duplicate_view)), std::invalid_argument);
}

TEST(YggdrasilTests, DatabaseColumnsAssignmentRetainsStorageForSelfSubviews)
{
    Builder<Columns<Values>> columns { ColumnIndex(1), ColumnIndex(2), ColumnIndex(3), ColumnIndex(4) };
    const auto* storage = columns.span().data();
    const auto memory = columns.memory_usage();
    columns.assign(columns.span());
    EXPECT_EQ(columns.span().data(), storage);
    EXPECT_EQ(columns.size(), 4);
    EXPECT_THROW(columns.assign(columns.span().subspan(1, 2)), std::invalid_argument);
    columns.assign(columns.span().first(2));
    EXPECT_EQ(columns.span()[0].label, ColumnIndex(1));
    EXPECT_EQ(columns.span()[1].label, ColumnIndex(2));
    EXPECT_EQ(columns.size(), 2);
    EXPECT_EQ(columns.span().data(), storage);
    EXPECT_EQ(columns.memory_usage(), memory);
    columns.assign(std::span<const ColumnIndex>());
    EXPECT_TRUE(columns.empty());
    EXPECT_EQ(columns.memory_usage(), memory);
    const Builder<Columns<Values>> replacement { ColumnIndex(5), ColumnIndex(6), ColumnIndex(7), ColumnIndex(8) };
    columns.assign(replacement.span());
    EXPECT_EQ(columns.span().data(), storage);
    EXPECT_EQ(columns.memory_usage(), memory);
    EXPECT_EQ(columns.column_index(ColumnIndex(8)), 3);
}

TEST(YggdrasilTests, DatabaseRelationRenamePreservesRowsAndStorage)
{
    Builder<Relation<Values>> relation { ColumnIndex(1), ColumnIndex(2) };
    relation.insert(cells(10, 20));
    relation.insert(cells(30, 40));
    const auto* columns = relation.columns().data();
    const auto* first_row = relation.row(0).data();
    const auto* second_row = relation.row(1).data();
    const auto memory = relation.memory_usage();
    {
        const Builder<Columns<Values>> labels { ColumnIndex(4), ColumnIndex(9) };
        relation.rename(labels);
        relation.rename(labels);
    }
    relation.rename(relation.columns());
    expect_relation(relation, { ColumnIndex(4), ColumnIndex(9) }, { { 10, 20 }, { 30, 40 } });
    EXPECT_EQ(relation.columns().data(), columns);
    EXPECT_EQ(relation.row(0).data(), first_row);
    EXPECT_EQ(relation.row(1).data(), second_row);
    EXPECT_EQ(relation.memory_usage(), memory);
    EXPECT_EQ(relation.insert(cells(10, 20)), 0);

    const Builder<Columns<Values>> wrong_arity { ColumnIndex(4) };
    EXPECT_THROW(relation.rename(wrong_arity), std::invalid_argument);
    expect_relation(relation, { ColumnIndex(4), ColumnIndex(9) }, { { 10, 20 }, { 30, 40 } });
    EXPECT_EQ(relation.columns().data(), columns);
    EXPECT_EQ(relation.row(0).data(), first_row);
    EXPECT_EQ(relation.row(1).data(), second_row);

    Builder<Relation<Values>> nullary;
    nullary.rename(std::span<const ColumnIndex>());
    EXPECT_TRUE(nullary.empty());
    nullary.insert(cells());
    nullary.rename(nullary.columns());
    expect_relation(nullary, {}, { {} });
}

TEST(YggdrasilTests, DatabaseRelationMaintainsSetAndSchemaInvariants)
{
    Builder<Relation<Values>> relation(Builder<Columns<Values>> { ColumnIndex(4), ColumnIndex(9) });
    EXPECT_EQ(relation.arity(), 2);
    EXPECT_TRUE(relation.empty());
    EXPECT_EQ(relation.insert(cells(1, 2)), 0);
    EXPECT_EQ(relation.insert(cells(1, 2)), 0);
    EXPECT_EQ(relation.insert(cells(3, 4)), 1);
    EXPECT_EQ(relation.size(), 2);
    EXPECT_EQ(relation.at(1).get<uint_t>(size_t { 0 }), 3);
    EXPECT_FALSE(relation.contains(cells(1, 4)));
    EXPECT_THROW(relation.insert(cells(1)), std::invalid_argument);
    EXPECT_THROW(relation.contains(cells(1)), std::invalid_argument);
    EXPECT_THROW(relation.at(2), std::out_of_range);
    EXPECT_THROW((Builder<Relation<Values>>({ ColumnIndex(4), ColumnIndex(4) })), std::invalid_argument);
    const auto& view = relation;
    EXPECT_EQ(view.column_index(ColumnIndex(9)), 1);
    EXPECT_THROW(view.column_index(ColumnIndex(5)), std::out_of_range);
    EXPECT_TRUE(view.contains(cells(3, 4)));
    EXPECT_THROW(view.at(2), std::out_of_range);
    relation.clear();
    expect_relation(relation, { ColumnIndex(4), ColumnIndex(9) }, {});
    EXPECT_EQ(relation.insert(cells(5, 6)), 0);
}

TEST(YggdrasilTests, DatabaseBuilderViewsShareSchemasAndObserveRefills)
{
    RelationRepositoryFactory<Values> factory;
    auto repository = factory.create();
    Builder<Relation<Values>> relation({ ColumnIndex(1), ColumnIndex(2) });
    relation.insert(cells(7, 8));
    const auto view = make_view(relation, repository);
    const auto copy = view;
    EXPECT_EQ(view.columns().data(), relation.columns().data());
    EXPECT_EQ(copy.columns().data(), relation.columns().data());
    const auto* first_row = view.row(0).data();
    const Builder<Columns<Values>> labels { ColumnIndex(3), ColumnIndex(4) };
    relation.rename(labels);
    EXPECT_EQ(view.column_index(ColumnIndex(4)), 1);
    EXPECT_EQ(copy.column_index(ColumnIndex(3)), 0);
    EXPECT_EQ(view.row(0).data(), first_row);
    relation.clear();
    EXPECT_TRUE(view.empty());
    relation.insert(cells(9, 10));
    EXPECT_TRUE(copy.contains(cells(9, 10)));
    auto projected = project<Values>(view, { ColumnIndex(4), ColumnIndex(3) });
    expect_relation(projected, { ColumnIndex(4), ColumnIndex(3) }, { { 10, 9 } });
    auto projected_columns = project<Values>(view, projected.columns());
    expect_relation(projected_columns, { ColumnIndex(4), ColumnIndex(3) }, { { 10, 9 } });
}

TEST(YggdrasilTests, DatabaseRelationReinitializationRetainsCompatibleStorage)
{
    Builder<Relation<Values>> relation({ ColumnIndex(1), ColumnIndex(2) });
    relation.insert(cells(3, 4));
    const auto memory = relation.memory_usage();
    EXPECT_THROW(relation.initialize({ ColumnIndex(5), ColumnIndex(5) }), std::invalid_argument);
    expect_relation(relation, { ColumnIndex(1), ColumnIndex(2) }, { { 3, 4 } });
    relation.initialize(relation.columns());
    EXPECT_EQ(relation.memory_usage(), memory);
    expect_relation(relation, { ColumnIndex(1), ColumnIndex(2) }, {});
    relation.insert(cells(3, 4));
    const Builder<Columns<Values>> validated { ColumnIndex(5), ColumnIndex(6) };
    relation.initialize(validated);
    EXPECT_EQ(relation.memory_usage(), memory);
    expect_relation(relation, { ColumnIndex(5), ColumnIndex(6) }, {});
    relation.initialize({ ColumnIndex(7) });
    relation.insert(cells(9));
    expect_relation(relation, { ColumnIndex(7) }, { { 9 } });
    relation.initialize({});
    relation.insert(cells());
    expect_relation(relation, {}, { {} });
}

TEST(YggdrasilTests, DatabasePoolKeepsLiveRelationsDistinctAndReusesByArity)
{
    RelationPool<Values> pool;
    auto first = pool.get_or_allocate({ ColumnIndex(1), ColumnIndex(2) });
    first->insert(cells(3, 4));
    auto second = pool.get_or_allocate({ ColumnIndex(1), ColumnIndex(2) });
    second->insert(cells(5, 6));
    EXPECT_NE(first.get(), second.get());
    const auto* released = second.get();
    const auto memory = second->memory_usage();
    second = {};
    auto unary = pool.get_or_allocate({ ColumnIndex(1) });
    unary->insert(cells(8));
    const Builder<Columns<Values>> validated { ColumnIndex(7), ColumnIndex(8) };
    auto reused = pool.get_or_allocate(validated);
    EXPECT_EQ(reused.get(), released);
    EXPECT_EQ(reused->memory_usage(), memory);
    expect_relation(*reused, { ColumnIndex(7), ColumnIndex(8) }, {});
    expect_relation(*first, { ColumnIndex(1), ColumnIndex(2) }, { { 3, 4 } });
    const std::array<ColumnIndex, 2> duplicates { ColumnIndex(9), ColumnIndex(9) };
    EXPECT_THROW((void) pool.get_or_allocate(std::span(duplicates)), std::invalid_argument);
    expect_relation(*first, { ColumnIndex(1), ColumnIndex(2) }, { { 3, 4 } });
    expect_relation(*reused, { ColumnIndex(7), ColumnIndex(8) }, {});
    auto boolean = pool.get_or_allocate({});
    boolean->insert(cells());
    expect_relation(*boolean, {}, { {} });
}

TEST(YggdrasilTests, DatabaseProjectionReordersDeduplicatesAndOwnsResults)
{
    Builder<Relation<Values>> relation({ ColumnIndex(1), ColumnIndex(2), ColumnIndex(3) });
    relation.insert(cells(7, 8, 1));
    relation.insert(cells(7, 8, 2));
    relation.insert(cells(9, 8, 3));
    auto projected = project<Values>(relation, { ColumnIndex(2), ColumnIndex(1) });
    expect_relation(projected, { ColumnIndex(2), ColumnIndex(1) }, { { 8, 7 }, { 8, 9 } });
    EXPECT_THROW(project<Values>(relation, { ColumnIndex(1), ColumnIndex(1) }), std::invalid_argument);
    EXPECT_THROW(project<Values>(relation, { ColumnIndex(4) }), std::out_of_range);
    Builder<Relation<Values>> output({ ColumnIndex(2), ColumnIndex(1) });
    output.insert(cells(99, 99));
    const std::array<ColumnIndex, 2> duplicates { ColumnIndex(1), ColumnIndex(1) };
    EXPECT_THROW(project(relation, std::span(duplicates), output), std::invalid_argument);
    expect_relation(output, { ColumnIndex(2), ColumnIndex(1) }, { { 99, 99 } });
    project(relation, { ColumnIndex(2), ColumnIndex(1) }, output);
    expect_relation(output, { ColumnIndex(2), ColumnIndex(1) }, { { 8, 7 }, { 8, 9 } });
    relation.clear();
    expect_relation(projected, { ColumnIndex(2), ColumnIndex(1) }, { { 8, 7 }, { 8, 9 } });
    project(relation, { ColumnIndex(2), ColumnIndex(1) }, output);
    expect_relation(output, { ColumnIndex(2), ColumnIndex(1) }, {});
}

TEST(YggdrasilTests, DatabaseRawColumnInputsAcceptSpansArraysAndVectors)
{
    std::array<ColumnIndex, 3> labels { ColumnIndex(1), ColumnIndex(2), ColumnIndex(3) };
    Builder<Columns<Values>> columns { std::span<const ColumnIndex>(labels) };
    Builder<Relation<Values>> relation(labels);
    relation.insert(cells(7, 8, 9));
    labels.fill(ColumnIndex(0));
    EXPECT_EQ(columns.column_index(ColumnIndex(3)), 2);
    expect_relation(relation, { ColumnIndex(1), ColumnIndex(2), ColumnIndex(3) }, { { 7, 8, 9 } });

    std::array<ColumnIndex, 2> output_labels { ColumnIndex(3), ColumnIndex(1) };
    auto from_span = project<Values>(relation, std::span(output_labels));
    auto from_array = project<Values>(relation, output_labels);
    auto from_vector = project<Values>(relation, std::vector<ColumnIndex> { ColumnIndex(3), ColumnIndex(1) });
    output_labels.fill(ColumnIndex(0));
    expect_relation(from_span, { ColumnIndex(3), ColumnIndex(1) }, { { 9, 7 } });
    expect_relation(from_array, { ColumnIndex(3), ColumnIndex(1) }, { { 9, 7 } });
    expect_relation(from_vector, { ColumnIndex(3), ColumnIndex(1) }, { { 9, 7 } });
}

TEST(YggdrasilTests, DatabasePlansOwnSchemasAndReuseResolvedPositions)
{
    constexpr auto large = ColumnIndex::max();
    auto plans = [large]
    {
        std::array<ColumnIndex, 3> lhs { large, ColumnIndex(1), ColumnIndex(2) };
        std::array<ColumnIndex, 3> rhs { ColumnIndex(2), ColumnIndex(3), large };
        auto projection = ProjectionPlan<Values>(lhs, std::array { ColumnIndex(2), large });
        auto join = JoinPlan<Values>(lhs, rhs);
        lhs.fill(ColumnIndex(0));
        rhs.fill(ColumnIndex(0));
        return std::pair(std::move(projection), std::move(join));
    }();
    auto copied = plans;
    auto [projection, joining] = std::move(copied);
    Builder<Relation<Values>> left({ large, ColumnIndex(1), ColumnIndex(2) });
    Builder<Relation<Values>> right({ ColumnIndex(2), ColumnIndex(3), large });
    Builder<Relation<Values>> projected({ ColumnIndex(2), large });
    Builder<Relation<Values>> joined({ large, ColumnIndex(1), ColumnIndex(2), ColumnIndex(3) });
    Workspace<Values> workspace;
    for (uint_t generation = 0; generation < 3; ++generation)
    {
        left.clear();
        right.clear();
        left.insert(cells(7 + generation, 8, 9));
        left.insert(cells(7 + generation, 10, 9));
        right.insert(cells(9, 11, 7 + generation));
        project(left, projection, projected, workspace);
        expect_relation(projected, { ColumnIndex(2), large }, { { 9, 7 + generation } });
        join(left, right, joining, joined, workspace);
        expect_relation(joined, { large, ColumnIndex(1), ColumnIndex(2), ColumnIndex(3) }, { { 7 + generation, 8, 9, 11 }, { 7 + generation, 10, 9, 11 } });
    }
    const ProjectionPlan<Values> guard(left.columns(), {});
    Builder<Relation<Values>> exists;
    project(left, guard, exists, workspace);
    expect_relation(exists, {}, { {} });
    left.clear();
    project(left, guard, exists, workspace);
    expect_relation(exists, {}, {});
}

TEST(YggdrasilTests, DatabaseColumnsAndPlansSurviveCistaRelocation)
{
    // The owning objects and original byte buffers are gone before decoding.
    auto columns_bytes = relocated_serialization(Builder<Columns<Values>> { ColumnIndex(3), ColumnIndex(1), ColumnIndex(2) });
    auto projection_bytes =
        relocated_serialization(ProjectionPlan<Values>({ ColumnIndex(3), ColumnIndex(1), ColumnIndex(2), ColumnIndex(4) }, { ColumnIndex(4), ColumnIndex(3) }));
    auto join_bytes =
        relocated_serialization(JoinPlan<Values>({ ColumnIndex(3), ColumnIndex(1), ColumnIndex(2) }, { ColumnIndex(2), ColumnIndex(4), ColumnIndex(1) }));
    const auto* columns = cista::deserialize<Builder<Columns<Values>>>(columns_bytes);
    const auto* projection = cista::deserialize<ProjectionPlan<Values>>(projection_bytes);
    const auto* joining = cista::deserialize<JoinPlan<Values>>(join_bytes);
    const auto values = [](auto range)
    {
        std::vector<ColumnIndex> labels;
        for (const auto column : range)
            labels.push_back(column.label);
        return labels;
    };
    const auto offsets = [](auto range)
    {
        std::vector<size_t> result;
        for (const auto column : range)
        {
            EXPECT_EQ(column.type, (column_type<Values, uint_t>) );
            EXPECT_EQ(column.size, ColumnCodec<uint_t>::size);
            result.push_back(column.offset / ColumnCodec<uint_t>::size);
        }
        return result;
    };
    EXPECT_EQ(values(columns->span()), (std::vector<ColumnIndex> { ColumnIndex(3), ColumnIndex(1), ColumnIndex(2) }));
    EXPECT_EQ(columns->column_index(ColumnIndex(1)), 1);
    EXPECT_EQ(values(projection->input_columns()), (std::vector<ColumnIndex> { ColumnIndex(3), ColumnIndex(1), ColumnIndex(2), ColumnIndex(4) }));
    EXPECT_EQ(values(projection->output_columns()), (std::vector<ColumnIndex> { ColumnIndex(4), ColumnIndex(3) }));
    EXPECT_EQ(offsets(projection->positions()), (std::vector<size_t> { 3, 0 }));
    EXPECT_EQ(values(joining->lhs_columns()), (std::vector<ColumnIndex> { ColumnIndex(3), ColumnIndex(1), ColumnIndex(2) }));
    EXPECT_EQ(values(joining->rhs_columns()), (std::vector<ColumnIndex> { ColumnIndex(2), ColumnIndex(4), ColumnIndex(1) }));
    EXPECT_EQ(values(joining->output_columns()), (std::vector<ColumnIndex> { ColumnIndex(3), ColumnIndex(1), ColumnIndex(2), ColumnIndex(4) }));
    EXPECT_EQ(offsets(joining->lhs_keys()), (std::vector<size_t> { 2, 1 }));
    EXPECT_EQ(offsets(joining->rhs_keys()), (std::vector<size_t> { 0, 2 }));
    EXPECT_EQ(offsets(joining->rhs_payload()), (std::vector<size_t> { 1 }));

    // Keep the relocated buffers unchanged and alive while using decoded plans.
    Builder<Relation<Values>> left(columns->span());
    Builder<Relation<Values>> right(joining->rhs_columns());
    left.insert(cells(10, 1, 2));
    left.insert(cells(11, 1, 3));
    right.insert(cells(2, 20, 1));
    right.insert(cells(3, 30, 1));
    right.insert(cells(2, 99, 9));
    Builder<Relation<Values>> joined(joining->output_columns());
    Builder<Relation<Values>> projected(projection->output_columns());
    Workspace<Values> workspace;
    join(left, right, *joining, joined, workspace);
    expect_relation(joined, { ColumnIndex(3), ColumnIndex(1), ColumnIndex(2), ColumnIndex(4) }, { { 10, 1, 2, 20 }, { 11, 1, 3, 30 } });
    project(joined, *projection, projected, workspace);
    expect_relation(projected, { ColumnIndex(4), ColumnIndex(3) }, { { 20, 10 }, { 30, 11 } });
}

TEST(YggdrasilTests, DatabaseDefaultPlansRoundTripAndEvaluateNullaryRelations)
{
    auto columns_bytes = relocated_serialization(Builder<Columns<Values>>());
    auto projection_bytes = relocated_serialization(ProjectionPlan<Values>());
    auto join_bytes = relocated_serialization(JoinPlan<Values>());
    const auto* columns = cista::deserialize<Builder<Columns<Values>>>(columns_bytes);
    const auto* projection = cista::deserialize<ProjectionPlan<Values>>(projection_bytes);
    const auto* joining = cista::deserialize<JoinPlan<Values>>(join_bytes);
    EXPECT_TRUE(columns->empty());
    EXPECT_TRUE(projection->input_columns().empty());
    EXPECT_TRUE(projection->output_columns().empty());
    EXPECT_TRUE(projection->positions().empty());
    EXPECT_TRUE(joining->lhs_columns().empty());
    EXPECT_TRUE(joining->rhs_columns().empty());
    EXPECT_TRUE(joining->output_columns().empty());
    EXPECT_TRUE(joining->lhs_keys().empty());
    EXPECT_TRUE(joining->rhs_keys().empty());
    EXPECT_TRUE(joining->rhs_payload().empty());

    Builder<Relation<Values>> left, right, projected, joined;
    Workspace<Values> workspace;
    for (const bool lhs_nonempty : { false, true })
        for (const bool rhs_nonempty : { false, true })
        {
            left.clear();
            right.clear();
            if (lhs_nonempty)
                left.insert(cells());
            if (rhs_nonempty)
                right.insert(cells());
            project(left, *projection, projected, workspace);
            EXPECT_EQ(projected.arity(), 0);
            EXPECT_EQ(projected.size(), lhs_nonempty ? 1 : 0);
            join(left, right, *joining, joined, workspace);
            EXPECT_EQ(joined.arity(), 0);
            EXPECT_EQ(joined.size(), lhs_nonempty && rhs_nonempty ? 1 : 0);
        }
}

TEST(YggdrasilTests, DatabasePreparedPlansValidateSchemasAndAliasesBeforeClearingOutputs)
{
    Builder<Relation<Values>> left({ ColumnIndex(1), ColumnIndex(2) });
    Builder<Relation<Values>> right({ ColumnIndex(2), ColumnIndex(3) });
    const ProjectionPlan<Values> projection(left.columns(), { ColumnIndex(1) });
    const JoinPlan<Values> joining(left.columns(), right.columns());
    Builder<Relation<Values>> projected({ ColumnIndex(1) });
    projected.insert(cells(99));
    Builder<Relation<Values>> joined({ ColumnIndex(1), ColumnIndex(2), ColumnIndex(3) });
    joined.insert(cells(99, 99, 99));
    Workspace<Values> workspace;
    JoinIndexCache<Values> cache;
    const JoinReuse reuse { true, true };

    // A stale plan must be rejected even when its input contains no rows.
    left.initialize({ ColumnIndex(2), ColumnIndex(1) });
    EXPECT_THROW(project(left, projection, projected, workspace), std::invalid_argument);
    EXPECT_THROW(join(left, right, joining, joined, workspace), std::invalid_argument);
    EXPECT_THROW(join(left, right, joining, cache, reuse, joined, workspace), std::invalid_argument);
    left.initialize({ ColumnIndex(1), ColumnIndex(4) });
    EXPECT_THROW(project(left, projection, projected, workspace), std::invalid_argument);
    EXPECT_THROW(join(left, right, joining, joined, workspace), std::invalid_argument);
    EXPECT_THROW(join(left, right, joining, cache, reuse, joined, workspace), std::invalid_argument);
    left.initialize({ ColumnIndex(1), ColumnIndex(2) });
    right.initialize({ ColumnIndex(3), ColumnIndex(2) });
    EXPECT_THROW(join(left, right, joining, joined, workspace), std::invalid_argument);
    EXPECT_THROW(join(left, right, joining, cache, reuse, joined, workspace), std::invalid_argument);
    right.initialize({ ColumnIndex(2), ColumnIndex(4) });
    EXPECT_THROW(join(left, right, joining, joined, workspace), std::invalid_argument);
    EXPECT_THROW(join(left, right, joining, cache, reuse, joined, workspace), std::invalid_argument);
    expect_relation(projected, { ColumnIndex(1) }, { { 99 } });
    expect_relation(joined, { ColumnIndex(1), ColumnIndex(2), ColumnIndex(3) }, { { 99, 99, 99 } });

    right.initialize({ ColumnIndex(2), ColumnIndex(3) });
    left.insert(cells(7, 8));
    right.insert(cells(8, 9));
    projected.initialize({ ColumnIndex(2) });
    projected.insert(cells(88));
    joined.initialize({ ColumnIndex(1), ColumnIndex(3), ColumnIndex(2) });
    joined.insert(cells(88, 88, 88));
    EXPECT_THROW(project(left, projection, projected, workspace), std::invalid_argument);
    EXPECT_THROW(join(left, right, joining, joined, workspace), std::invalid_argument);
    EXPECT_THROW(join(left, right, joining, cache, reuse, joined, workspace), std::invalid_argument);
    expect_relation(projected, { ColumnIndex(2) }, { { 88 } });
    expect_relation(joined, { ColumnIndex(1), ColumnIndex(3), ColumnIndex(2) }, { { 88, 88, 88 } });

    const ProjectionPlan<Values> identity(left.columns(), { ColumnIndex(1), ColumnIndex(2) });
    const JoinPlan<Values> same(left.columns(), left.columns());
    EXPECT_THROW(project(left, identity, left, workspace), std::invalid_argument);
    EXPECT_THROW(join(left, left, same, left, workspace), std::invalid_argument);
    EXPECT_THROW(join(left, left, same, cache, reuse, left, workspace), std::invalid_argument);
    expect_relation(left, { ColumnIndex(1), ColumnIndex(2) }, { { 7, 8 } });
    EXPECT_EQ(cache.size(), 0);

    const std::array<ColumnIndex, 2> duplicates { ColumnIndex(1), ColumnIndex(1) };
    const std::span<const ColumnIndex> duplicate_view { std::span(duplicates) };
    EXPECT_THROW((ProjectionPlan<Values>(duplicate_view, duplicate_view)), std::invalid_argument);
    EXPECT_THROW((ProjectionPlan<Values>(left.columns(), duplicate_view)), std::invalid_argument);
    EXPECT_THROW((ProjectionPlan<Values>({ ColumnIndex(1) }, { ColumnIndex(2) })), std::out_of_range);
    EXPECT_THROW((JoinPlan<Values>(duplicate_view, std::array { ColumnIndex(2), ColumnIndex(3) })), std::invalid_argument);
    EXPECT_THROW((JoinPlan<Values>(std::array { ColumnIndex(1), ColumnIndex(2) }, duplicate_view)), std::invalid_argument);
}

TEST(YggdrasilTests, DatabaseSelectionsSupportColumnsConstantsAndPredicates)
{
    Builder<Relation<Values>> relation({ ColumnIndex(1), ColumnIndex(2) });
    relation.insert(cells(1, 1));
    relation.insert(cells(1, 2));
    relation.insert(cells(2, 2));
    expect_relation(select_equal_columns<Values>(relation, ColumnIndex(1), ColumnIndex(2)), { ColumnIndex(1), ColumnIndex(2) }, { { 1, 1 }, { 2, 2 } });
    expect_relation(select_equal_value<Values>(relation, ColumnIndex(1), uint_t(1)), { ColumnIndex(1), ColumnIndex(2) }, { { 1, 1 }, { 1, 2 } });
    const auto less = [](Row<Values> row) { return row.get<uint_t>(size_t { 0 }) < row.get<uint_t>(size_t { 1 }); };
    expect_relation(select<Values>(relation, less), { ColumnIndex(1), ColumnIndex(2) }, { { 1, 2 } });
    Builder<Relation<Values>> output({ ColumnIndex(1), ColumnIndex(2) });
    output.insert(cells(99, 99));
    select_equal_columns(relation, ColumnIndex(1), ColumnIndex(2), output);
    expect_relation(output, { ColumnIndex(1), ColumnIndex(2) }, { { 1, 1 }, { 2, 2 } });
    select_equal_value(relation, ColumnIndex(2), uint_t(2), output);
    expect_relation(output, { ColumnIndex(1), ColumnIndex(2) }, { { 1, 2 }, { 2, 2 } });
    select(relation, less, output);
    expect_relation(output, { ColumnIndex(1), ColumnIndex(2) }, { { 1, 2 } });
    EXPECT_THROW(select_equal_columns<Values>(relation, ColumnIndex(1), ColumnIndex(3)), std::out_of_range);
    EXPECT_THROW(select_equal_value<Values>(relation, ColumnIndex(3), uint_t(1)), std::out_of_range);
}

TEST(YggdrasilTests, DatabaseJoinPreservesAllMatchingRowsAndLogicalColumnOrder)
{
    Builder<Relation<Values>> left({ ColumnIndex(3), ColumnIndex(1), ColumnIndex(2) });
    Builder<Relation<Values>> right({ ColumnIndex(2), ColumnIndex(4), ColumnIndex(1) });
    left.insert(cells(10, 1, 2));
    left.insert(cells(11, 1, 2));
    left.insert(cells(12, 1, 3));
    right.insert(cells(2, 20, 1));
    right.insert(cells(2, 21, 1));
    right.insert(cells(2, 22, 9));
    right.insert(cells(3, 23, 1));
    right.insert(cells(8, 24, 8));
    expect_relation(join<Values>(left, right),
                    { ColumnIndex(3), ColumnIndex(1), ColumnIndex(2), ColumnIndex(4) },
                    { { 10, 1, 2, 20 }, { 10, 1, 2, 21 }, { 11, 1, 2, 20 }, { 11, 1, 2, 21 }, { 12, 1, 3, 23 } });
    expect_relation(join<Values>(right, left),
                    { ColumnIndex(2), ColumnIndex(4), ColumnIndex(1), ColumnIndex(3) },
                    { { 2, 20, 1, 10 }, { 2, 20, 1, 11 }, { 2, 21, 1, 10 }, { 2, 21, 1, 11 }, { 3, 23, 1, 12 } });
    Builder<Relation<Values>> output({ ColumnIndex(3), ColumnIndex(1), ColumnIndex(2), ColumnIndex(4) });
    output.insert(cells(99, 99, 99, 99));
    join(left, right, output);
    EXPECT_EQ(output.size(), 5);
    right.clear();
    join(left, right, output);
    expect_relation(output, { ColumnIndex(3), ColumnIndex(1), ColumnIndex(2), ColumnIndex(4) }, {});
}

TEST(YggdrasilTests, DatabasePoolFactoryIdentifiesStorageAcrossPoolsAndRenames)
{
    RelationPoolFactory<Values> factory;
    auto copy = factory;
    auto first_pool = factory.create_pool();
    auto second_pool = copy.create_pool();
    auto first = first_pool.get_or_allocate({ ColumnIndex(1) });
    auto second = second_pool.get_or_allocate({ ColumnIndex(1) });
    EXPECT_NE(first->get_storage_index(), second->get_storage_index());
    first->insert(cells(4));
    second->insert(cells(5));
    JoinIndexCache<Values> cache;
    const std::array<ColumnSlice, 1> keys { ColumnSlice { 0, 0, sizeof(uint_t) } };
    cache.get_or_create(*first, keys);
    cache.get_or_create(*second, keys);
    EXPECT_EQ(cache.size(), 2);
    const Builder<Columns<Values>> labels { ColumnIndex(2) };
    const auto* cached = &cache.get_or_create(*first, keys);
    const auto storage_index = first->get_storage_index();
    first->rename(labels);
    EXPECT_EQ(first->get_storage_index(), storage_index);
    EXPECT_EQ(cached, &cache.get_or_create(*first, keys));

    cache.clear();  // Cached rows must be released before storage is reused.
    const auto index = first->get_storage_index();
    first = {};
    auto reused = first_pool.get_or_allocate({ ColumnIndex(9) });
    EXPECT_EQ(reused->get_storage_index(), index);
    EXPECT_TRUE(reused->empty());
    reused->insert(cells(8));
    cache.get_or_create(*reused, keys);
    EXPECT_EQ(cache.size(), 1);
    auto pair = first_pool.get_or_allocate({ ColumnIndex(1), ColumnIndex(2) });
    EXPECT_NE(pair->get_storage_index(), index);
}

TEST(YggdrasilTests, DatabaseJoinIndexRejectsUnidentifiedStorage)
{
    Builder<Relation<Values>> left({ ColumnIndex(1) });
    Builder<Relation<Values>> right({ ColumnIndex(1) });
    left.insert(cells(1));
    right.insert(cells(1));
    const std::array<ColumnSlice, 1> keys { ColumnSlice { 0, 0, sizeof(uint_t) } };
    EXPECT_THROW((JoinIndex<Values>(left, keys)), std::invalid_argument);
    JoinIndexCache<Values> cache;
    EXPECT_THROW(cache.get_or_create(left, keys), std::invalid_argument);
    EXPECT_EQ(cache.size(), 0);
    Workspace<Values> workspace;
    const JoinPlan<Values> plan(left.columns(), right.columns());
    Builder<Relation<Values>> result({ ColumnIndex(1) });
    result.insert(cells(99));
    EXPECT_THROW(join(left, right, plan, cache, JoinReuse { true, false }, result, workspace), std::invalid_argument);
    expect_relation(result, { ColumnIndex(1) }, { { 99 } });
    // Uncached operations continue to accept directly constructed relations.
    join(left, right, plan, result, workspace);
    expect_relation(result, { ColumnIndex(1) }, { { 1 } });
}

TEST(YggdrasilTests, DatabaseJoinIndexReusesImmutableRowsWithChangingProbes)
{
    RelationPool<Values> pool;
    auto build = pool.get_or_allocate({ ColumnIndex(3), ColumnIndex(1), ColumnIndex(2) });
    Builder<Relation<Values>> probe({ ColumnIndex(2), ColumnIndex(4), ColumnIndex(1) });
    build->insert(cells(10, 1, 2));
    build->insert(cells(11, 1, 2));
    build->insert(cells(12, 1, 3));
    const JoinPlan<Values> left_plan(build->columns(), probe.columns());
    const JoinPlan<Values> right_plan(probe.columns(), build->columns());
    const JoinIndex<Values> left_index(*build, left_plan.lhs_keys());
    const JoinIndex<Values> right_index(*build, right_plan.rhs_keys());
    Builder<Relation<Values>> left_result(left_plan.output_columns());
    Builder<Relation<Values>> right_result(right_plan.output_columns());
    Workspace<Values> workspace;
    JoinIndexCache<Values> cache;
    for (uint_t generation = 0; generation < 3; ++generation)
    {
        probe.clear();
        probe.insert(cells(2, 20 + generation, 1));
        probe.insert(cells(3, 30 + generation, 1));
        probe.insert(cells(2, 40 + generation, 9));
        join(*build, probe, left_plan, left_index, left_result, workspace);
        expect_relation(left_result,
                        { ColumnIndex(3), ColumnIndex(1), ColumnIndex(2), ColumnIndex(4) },
                        { { 10, 1, 2, 20 + generation }, { 11, 1, 2, 20 + generation }, { 12, 1, 3, 30 + generation } });
        join(probe, *build, right_plan, right_index, right_result, workspace);
        expect_relation(right_result,
                        { ColumnIndex(2), ColumnIndex(4), ColumnIndex(1), ColumnIndex(3) },
                        { { 2, 20 + generation, 1, 10 }, { 2, 20 + generation, 1, 11 }, { 3, 30 + generation, 1, 12 } });
        join(*build, probe, left_plan, cache, JoinReuse { true, false }, left_result, workspace);
        expect_relation(left_result,
                        { ColumnIndex(3), ColumnIndex(1), ColumnIndex(2), ColumnIndex(4) },
                        { { 10, 1, 2, 20 + generation }, { 11, 1, 2, 20 + generation }, { 12, 1, 3, 30 + generation } });
        join(probe, *build, right_plan, cache, JoinReuse { false, true }, right_result, workspace);
        expect_relation(right_result,
                        { ColumnIndex(2), ColumnIndex(4), ColumnIndex(1), ColumnIndex(3) },
                        { { 2, 20 + generation, 1, 10 }, { 2, 20 + generation, 1, 11 }, { 3, 30 + generation, 1, 12 } });
        EXPECT_EQ(cache.size(), 2);
    }
    probe.clear();
    join(*build, probe, left_plan, left_index, left_result, workspace);
    EXPECT_TRUE(left_result.empty());
    join(*build, probe, left_plan, cache, JoinReuse { true, false }, left_result, workspace);
    EXPECT_TRUE(left_result.empty());
    join(probe, *build, right_plan, cache, JoinReuse { false, true }, right_result, workspace);
    EXPECT_TRUE(right_result.empty());
    EXPECT_EQ(cache.size(), 2);
}

TEST(YggdrasilTests, DatabaseJoinIndexCacheSharesStorageAndOrderedPositionsAcrossPlans)
{
    RelationPool<Values> pool;
    auto build = pool.get_or_allocate({ ColumnIndex(1), ColumnIndex(2), ColumnIndex(3) });
    Builder<Relation<Values>> probe({ ColumnIndex(1), ColumnIndex(2), ColumnIndex(4) });
    build->insert(cells(7, 8, 90));
    build->insert(cells(8, 7, 91));
    probe.insert(cells(7, 8, 10));
    const JoinPlan<Values> plan(build->columns(), probe.columns());
    const JoinPlan<Values> reversed(probe.columns(), build->columns());
    Builder<Relation<Values>> result(plan.output_columns());
    Builder<Relation<Values>> reverse_result(reversed.output_columns());
    Workspace<Values> workspace;
    JoinIndexCache<Values> cache;

    join(*build, probe, plan, cache, JoinReuse { true, false }, result, workspace);
    expect_relation(result, { ColumnIndex(1), ColumnIndex(2), ColumnIndex(3), ColumnIndex(4) }, { { 7, 8, 90, 10 } });
    EXPECT_EQ(cache.size(), 1);
    const std::array<ColumnSlice, 2> keys { ColumnSlice { 0, 0, sizeof(uint_t) }, ColumnSlice { 0, sizeof(uint_t), sizeof(uint_t) } };
    EXPECT_EQ(&cache.get_or_create(*build, plan.lhs_keys()), &cache.get_or_create(*build, keys));
    join(probe, *build, reversed, cache, JoinReuse { false, true }, reverse_result, workspace);
    expect_relation(reverse_result, { ColumnIndex(1), ColumnIndex(2), ColumnIndex(4), ColumnIndex(3) }, { { 7, 8, 10, 90 } });
    EXPECT_EQ(cache.size(), 1);

    const Builder<Columns<Values>> build_labels { ColumnIndex(11), ColumnIndex(12), ColumnIndex(13) };
    const Builder<Columns<Values>> probe_labels { ColumnIndex(11), ColumnIndex(12), ColumnIndex(14) };
    build->rename(build_labels);
    probe.rename(probe_labels);
    const JoinPlan<Values> renamed_plan(build->columns(), probe.columns());
    result.initialize(renamed_plan.output_columns());
    join(*build, probe, renamed_plan, cache, JoinReuse { true, false }, result, workspace);
    expect_relation(result, { ColumnIndex(11), ColumnIndex(12), ColumnIndex(13), ColumnIndex(14) }, { { 7, 8, 90, 10 } });
    EXPECT_EQ(cache.size(), 1);

    build->rename(plan.lhs_columns());
    const Builder<Columns<Values>> reordered_labels { ColumnIndex(2), ColumnIndex(1), ColumnIndex(4) };
    probe.rename(reordered_labels);
    const JoinPlan<Values> reordered_plan(build->columns(), probe.columns());
    result.initialize(reordered_plan.output_columns());
    join(*build, probe, reordered_plan, cache, JoinReuse { true, false }, result, workspace);
    expect_relation(result, { ColumnIndex(1), ColumnIndex(2), ColumnIndex(3), ColumnIndex(4) }, { { 8, 7, 91, 10 } });
    EXPECT_EQ(cache.size(), 2);

    probe.rename(plan.rhs_columns());
    auto other = pool.get_or_allocate(build->columns());
    other->insert(cells(7, 8, 92));
    join(*other, probe, plan, cache, JoinReuse { true, false }, result, workspace);
    expect_relation(result, { ColumnIndex(1), ColumnIndex(2), ColumnIndex(3), ColumnIndex(4) }, { { 7, 8, 92, 10 } });
    EXPECT_EQ(cache.size(), 3);

    cache.clear();
    EXPECT_EQ(cache.size(), 0);
    build->clear();
    build->insert(cells(7, 8, 93));
    join(*build, probe, plan, cache, JoinReuse { true, false }, result, workspace);
    expect_relation(result, { ColumnIndex(1), ColumnIndex(2), ColumnIndex(3), ColumnIndex(4) }, { { 7, 8, 93, 10 } });
    EXPECT_EQ(cache.size(), 1);
}

TEST(YggdrasilTests, DatabaseJoinIndexCacheSkipsEmptyCartesianAndNonreusableInputs)
{
    Builder<Relation<Values>> left({ ColumnIndex(1) });
    Builder<Relation<Values>> right({ ColumnIndex(1) });
    right.insert(cells(7));
    const JoinPlan<Values> plan(left.columns(), right.columns());
    Builder<Relation<Values>> result(plan.output_columns());
    Workspace<Values> workspace;
    JoinIndexCache<Values> cache;
    for (const auto reuse : { JoinReuse {}, JoinReuse { true, false }, JoinReuse { false, true }, JoinReuse { true, true } })
    {
        result.insert(cells(99));
        join(left, right, plan, cache, reuse, result, workspace);
        EXPECT_TRUE(result.empty());
        result.insert(cells(99));
        join(right, left, plan, cache, reuse, result, workspace);
        EXPECT_TRUE(result.empty());
        EXPECT_EQ(cache.size(), 0);
    }
    left.insert(cells(7));
    join(left, right, plan, cache, JoinReuse {}, result, workspace);
    expect_relation(result, { ColumnIndex(1) }, { { 7 } });
    EXPECT_EQ(cache.size(), 0);

    const Builder<Columns<Values>> labels { ColumnIndex(2) };
    right.rename(labels);
    const JoinPlan<Values> cartesian(left.columns(), right.columns());
    result.initialize(cartesian.output_columns());
    join(left, right, cartesian, cache, JoinReuse { true, true }, result, workspace);
    expect_relation(result, { ColumnIndex(1), ColumnIndex(2) }, { { 7, 7 } });
    EXPECT_EQ(cache.size(), 0);
}

TEST(YggdrasilTests, DatabaseJoinIndexRejectsWrongStorageAndKeysBeforeClearingOutput)
{
    RelationPool<Values> pool;
    auto left = pool.get_or_allocate({ ColumnIndex(1), ColumnIndex(2) });
    auto right = pool.get_or_allocate({ ColumnIndex(2), ColumnIndex(3) });
    auto other = pool.get_or_allocate({ ColumnIndex(1), ColumnIndex(2) });
    const JoinPlan<Values> plan(left->columns(), right->columns());
    const JoinIndex<Values> unrelated(*other, plan.lhs_keys());
    const std::array<ColumnSlice, 1> wrong_position { ColumnSlice { 0, 0, sizeof(uint_t) } };
    const JoinIndex<Values> wrong_keys(*left, wrong_position);
    const std::array<ColumnSlice, 1> out_of_bounds { ColumnSlice { 0, 2 * sizeof(uint_t), sizeof(uint_t) } };
    EXPECT_THROW((JoinIndex<Values>(*left, out_of_bounds)), std::out_of_range);
    Builder<Relation<Values>> result(plan.output_columns());
    result.insert(cells(7, 8, 9));
    Workspace<Values> workspace;
    EXPECT_THROW(join(*left, *right, plan, unrelated, result, workspace), std::invalid_argument);
    EXPECT_THROW(join(*left, *right, plan, wrong_keys, result, workspace), std::invalid_argument);
    expect_relation(result, { ColumnIndex(1), ColumnIndex(2), ColumnIndex(3) }, { { 7, 8, 9 } });

    const JoinPlan<Values> same(left->columns(), left->columns());
    const JoinIndex<Values> same_index(*left, same.lhs_keys());
    EXPECT_THROW(join(*left, *left, same, same_index, *left, workspace), std::invalid_argument);
}

TEST(YggdrasilTests, DatabaseJoinIndexDoesNotReencodeBuildRowsWhenProbing)
{
    using Value = DatabaseCountedValue;
    RelationPool<TypeList<Value>> pool;
    auto build = pool.get_or_allocate({ ColumnIndex(1), ColumnIndex(2) });
    Builder<Relation<TypeList<Value>>> probe({ ColumnIndex(1) });
    for (uint_t i = 0; i < 2048; ++i)
        build->insert(std::tuple { Value { i }, Value { i + 1 } });
    const JoinPlan<TypeList<Value>> plan(probe.columns(), build->columns());
    const JoinIndex<TypeList<Value>> index(*build, plan.rhs_keys());
    JoinIndexCache<TypeList<Value>> cache;
    cache.get_or_create(*build, plan.rhs_keys());
    Builder<Relation<TypeList<Value>>> result(plan.output_columns());
    Workspace<TypeList<Value>> workspace;
    for (uint_t key : { 5, 1000, 1500 })
    {
        probe.clear();
        probe.insert(std::tuple { Value { key } });
        for (const bool cached : { false, true })
        {
            Value::encodes = 0;
            if (cached)
                join(probe, *build, plan, cache, JoinReuse { false, true }, result, workspace);
            else
                join(probe, *build, plan, index, result, workspace);
            EXPECT_LT(Value::encodes, 16);
            ASSERT_EQ(result.size(), 1);
            EXPECT_EQ(result[0].get<Value>(size_t { 1 }).value, key + 1);
        }
    }
}

TEST(YggdrasilTests, DatabaseJoinHandlesCartesianProductsAndAllSharedColumns)
{
    Builder<Relation<Values>> left({ ColumnIndex(1) });
    Builder<Relation<Values>> right({ ColumnIndex(2) });
    left.insert(cells(3));
    left.insert(cells(4));
    right.insert(cells(5));
    right.insert(cells(6));
    expect_relation(join<Values>(left, right), { ColumnIndex(1), ColumnIndex(2) }, { { 3, 5 }, { 3, 6 }, { 4, 5 }, { 4, 6 } });
    auto pairs = join<Values>(left, right);
    Builder<Relation<Values>> reversed({ ColumnIndex(2), ColumnIndex(1) });
    reversed.insert(cells(5, 3));
    reversed.insert(cells(8, 3));
    expect_relation(join<Values>(pairs, reversed), { ColumnIndex(1), ColumnIndex(2) }, { { 3, 5 } });
}

TEST(YggdrasilTests, DatabaseSetOperationsRequireAlignedSignatures)
{
    Builder<Relation<Values>> left({ ColumnIndex(1), ColumnIndex(2) });
    Builder<Relation<Values>> right({ ColumnIndex(1), ColumnIndex(2) });
    left.insert(cells(1, 2));
    left.insert(cells(3, 4));
    right.insert(cells(3, 4));
    right.insert(cells(5, 6));
    expect_relation(union_<Values>(left, right), { ColumnIndex(1), ColumnIndex(2) }, { { 1, 2 }, { 3, 4 }, { 5, 6 } });
    expect_relation(difference<Values>(left, right), { ColumnIndex(1), ColumnIndex(2) }, { { 1, 2 } });
    expect_relation(difference<Values>(left, left), { ColumnIndex(1), ColumnIndex(2) }, {});
    Builder<Relation<Values>> output({ ColumnIndex(1), ColumnIndex(2) });
    output.insert(cells(99, 99));
    union_(left, right, output);
    EXPECT_EQ(output.size(), 3);
    difference(left, right, output);
    expect_relation(output, { ColumnIndex(1), ColumnIndex(2) }, { { 1, 2 } });
    auto reversed = project<Values>(right, { ColumnIndex(2), ColumnIndex(1) });
    EXPECT_THROW(union_<Values>(left, reversed), std::invalid_argument);
    EXPECT_THROW(difference<Values>(left, reversed), std::invalid_argument);
    auto aligned = project<Values>(reversed, { ColumnIndex(1), ColumnIndex(2) });
    expect_relation(difference<Values>(left, aligned), { ColumnIndex(1), ColumnIndex(2) }, { { 1, 2 } });
}

TEST(YggdrasilTests, DatabaseNullaryRelationsObeyBooleanLaws)
{
    Builder<Relation<Values>> false_;
    Builder<Relation<Values>> true_;
    EXPECT_EQ(true_.insert(cells()), 0);
    EXPECT_EQ(true_.insert(cells()), 0);
    EXPECT_EQ(true_.size(), 1);
    EXPECT_EQ(true_.arity(), 0);
    EXPECT_TRUE(true_.contains(cells()));
    EXPECT_FALSE(false_.contains(cells()));
    expect_relation(join<Values>(true_, true_), {}, { {} });
    expect_relation(join<Values>(true_, false_), {}, {});
    expect_relation(union_<Values>(true_, false_), {}, { {} });
    expect_relation(difference<Values>(true_, false_), {}, { {} });
    expect_relation(difference<Values>(true_, true_), {}, {});
    Builder<Relation<Values>> objects({ ColumnIndex(1) });
    objects.insert(cells(7));
    expect_relation(join<Values>(objects, true_), { ColumnIndex(1) }, { { 7 } });
    expect_relation(join<Values>(true_, objects), { ColumnIndex(1) }, { { 7 } });
    expect_relation(join<Values>(false_, objects), { ColumnIndex(1) }, {});
    expect_relation(project<Values>(objects, {}), {}, { {} });
    Builder<Relation<Values>> output;
    project(objects, {}, output);
    expect_relation(output, {}, { {} });
    objects.clear();
    project(objects, {}, output);
    expect_relation(output, {}, {});
    expect_relation(select<Values>(true_, [](Row<Values> row) { return row.empty(); }), {}, { {} });
    expect_relation(select<Values>(true_, [](Row<Values>) { return false; }), {}, {});
}

TEST(YggdrasilTests, DatabaseOutputGuardsPreserveExistingResults)
{
    Builder<Relation<Values>> input({ ColumnIndex(1), ColumnIndex(2) });
    input.insert(cells(3, 4));
    Builder<Relation<Values>> wrong({ ColumnIndex(2), ColumnIndex(1) });
    wrong.insert(cells(8, 9));
    const auto keep = [](Row<Values>) { return true; };
    EXPECT_THROW(select(input, keep, wrong), std::invalid_argument);
    EXPECT_THROW(select_equal_columns(input, ColumnIndex(1), ColumnIndex(2), wrong), std::invalid_argument);
    EXPECT_THROW(select_equal_value(input, ColumnIndex(1), uint_t(3), wrong), std::invalid_argument);
    EXPECT_THROW(project(input, { ColumnIndex(1), ColumnIndex(2) }, wrong), std::invalid_argument);
    EXPECT_THROW(join(input, input, wrong), std::invalid_argument);
    EXPECT_THROW(union_(input, input, wrong), std::invalid_argument);
    EXPECT_THROW(difference(input, input, wrong), std::invalid_argument);
    expect_relation(wrong, { ColumnIndex(2), ColumnIndex(1) }, { { 8, 9 } });
    EXPECT_THROW(select(input, keep, input), std::invalid_argument);
    EXPECT_THROW(select_equal_columns(input, ColumnIndex(1), ColumnIndex(2), input), std::invalid_argument);
    EXPECT_THROW(select_equal_value(input, ColumnIndex(1), uint_t(3), input), std::invalid_argument);
    EXPECT_THROW(project(input, { ColumnIndex(1), ColumnIndex(2) }, input), std::invalid_argument);
    EXPECT_THROW(join(input, input, input), std::invalid_argument);
    EXPECT_THROW(union_(input, input, input), std::invalid_argument);
    EXPECT_THROW(difference(input, input, input), std::invalid_argument);
    EXPECT_THROW(join(input, wrong, input), std::invalid_argument);
    Builder<Relation<Values>> rhs_only({ ColumnIndex(1), ColumnIndex(2) });
    rhs_only.insert(cells(9, 10));
    EXPECT_THROW(join(input, rhs_only, rhs_only), std::invalid_argument);
    EXPECT_THROW(union_(input, rhs_only, rhs_only), std::invalid_argument);
    EXPECT_THROW(difference(input, rhs_only, rhs_only), std::invalid_argument);
    expect_relation(rhs_only, { ColumnIndex(1), ColumnIndex(2) }, { { 9, 10 } });
    RelationRepositoryFactory<Values> factory;
    auto repository = factory.create();
    const auto alias = make_view(input, repository);
    EXPECT_THROW(select(alias, keep, input), std::invalid_argument);
    expect_relation(input, { ColumnIndex(1), ColumnIndex(2) }, { { 3, 4 } });
    Builder<Relation<Values>> output({ ColumnIndex(1), ColumnIndex(2) });
    output.insert(cells(8, 9));
    EXPECT_THROW(project(input, { ColumnIndex(1), ColumnIndex(9) }, output), std::out_of_range);
    EXPECT_THROW(select_equal_columns(input, ColumnIndex(1), ColumnIndex(9), output), std::out_of_range);
    EXPECT_THROW(select_equal_value(input, ColumnIndex(9), uint_t(3), output), std::out_of_range);
    EXPECT_THROW(union_(input, wrong, output), std::invalid_argument);
    EXPECT_THROW(difference(input, wrong, output), std::invalid_argument);
    expect_relation(output, { ColumnIndex(1), ColumnIndex(2) }, { { 8, 9 } });
}

TEST(YggdrasilTests, DatabaseOperatorsRespectCanonicalCustomEquality)
{
    using Value = DatabaseModuloValue;
    RelationPool<TypeList<Value>> pool;
    auto left = pool.get_or_allocate({ ColumnIndex(1), ColumnIndex(2) });
    auto right = pool.get_or_allocate({ ColumnIndex(1), ColumnIndex(3) });
    left->insert(std::tuple { Value { 1 }, Value { 2 } });
    left->insert(std::tuple { Value { 3 }, Value { 4 } });
    EXPECT_EQ(left->insert(std::tuple { Value { 11 }, Value { 12 } }), 0);
    right->insert(std::tuple { Value { 11 }, Value { 5 } });
    right->insert(std::tuple { Value { 6 }, Value { 7 } });
    auto joined = join<TypeList<Value>>(*left, *right);
    EXPECT_EQ(joined.size(), 1);
    EXPECT_TRUE(joined.contains(std::tuple { Value { 1 }, Value { 2 }, Value { 5 } }));
    const JoinPlan<TypeList<Value>> plan(left->columns(), right->columns());
    const JoinIndex<TypeList<Value>> left_index(*left, plan.lhs_keys());
    const JoinIndex<TypeList<Value>> right_index(*right, plan.rhs_keys());
    Workspace<TypeList<Value>> workspace;
    for (const auto* index : { &left_index, &right_index })
    {
        join(*left, *right, plan, *index, joined, workspace);
        EXPECT_EQ(joined.size(), 1);
        EXPECT_TRUE(joined.contains(std::tuple { Value { 1 }, Value { 2 }, Value { 5 } }));
    }
    auto selected = select_equal_value<TypeList<Value>>(*left, ColumnIndex(1), Value { 11 });
    EXPECT_EQ(selected.size(), 1);
    EXPECT_TRUE(selected.contains(std::tuple { Value { 1 }, Value { 2 } }));
    Builder<Relation<TypeList<Value>>> pairs({ ColumnIndex(1), ColumnIndex(2) });
    pairs.insert(std::tuple { Value { 1 }, Value { 11 } });
    pairs.insert(std::tuple { Value { 2 }, Value { 3 } });
    auto equal = select_equal_columns<TypeList<Value>>(pairs, ColumnIndex(1), ColumnIndex(2));
    EXPECT_EQ(equal.size(), 1);
    EXPECT_TRUE(equal.contains(std::tuple { Value { 1 }, Value { 11 } }));
}

TEST(YggdrasilTests, DatabaseKeyedJoinDoesNotEncodeEveryPairOfRows)
{
    using Value = DatabaseCountedValue;
    constexpr uint_t count = 2048;
    Builder<Relation<TypeList<Value>>> left({ ColumnIndex(1), ColumnIndex(2) });
    Builder<Relation<TypeList<Value>>> right({ ColumnIndex(1), ColumnIndex(3) });
    for (uint_t i = 0; i < count; ++i)
    {
        left.insert(std::tuple { Value { i }, Value { i + 1 } });
        right.insert(std::tuple { Value { i }, Value { i + 2 } });
    }
    Value::encodes = 0;
    auto result = join<TypeList<Value>>(left, right);
    EXPECT_EQ(result.size(), count);
    // Encoding work remains far below the quadratic cross product.
    EXPECT_LT(Value::encodes, size_t(count) * 64);
}

TEST(YggdrasilTests, DatabaseNaturalJoinMatchesSmallReferenceAcrossSchemas)
{
    RelationPool<Values> pool;
    using SchemaPair = std::pair<std::vector<ColumnIndex>, std::vector<ColumnIndex>>;
    const std::array<SchemaPair, 6> schemas = { {
        { { ColumnIndex(1), ColumnIndex(2), ColumnIndex(3) }, { ColumnIndex(3), ColumnIndex(4), ColumnIndex(1) } },
        { { ColumnIndex(1), ColumnIndex(2) }, { ColumnIndex(2), ColumnIndex(1) } },
        { { ColumnIndex(1) }, { ColumnIndex(2), ColumnIndex(3) } },
        { {}, { ColumnIndex(1) } },
        { { ColumnIndex(1) }, {} },
        { {}, {} },
    } };
    std::mt19937 random(1701);
    Workspace<Values> workspace;
    for (const auto& [left_columns, right_columns] : schemas)
    {
        const JoinPlan<Values> join_plan(left_columns, right_columns);
        const ProjectionPlan<Values> project_plan(join_plan.output_columns(), left_columns);
        for (size_t trial = 0; trial < 25; ++trial)
        {
            SCOPED_TRACE(trial);
            auto left = pool.get_or_allocate(left_columns);
            auto right = pool.get_or_allocate(right_columns);
            for (auto* relation : { left.get(), right.get() })
            {
                const auto count = random() % 9;
                std::vector<uint_t> row(relation->arity());
                for (size_t i = 0; i < count; ++i)
                {
                    for (auto& value : row)
                        value = random() % 4;
                    relation->insert(packed(std::span<const uint_t>(row)));
                }
            }
            const auto expected = reference_join(*left, *right);
            auto result = join<Values>(*left, *right);
            ASSERT_EQ(result.size(), expected.size());
            for (const auto& row : expected)
                EXPECT_TRUE(result.contains(packed(std::span<const uint_t>(row))));
            join(*left, *right, join_plan, result, workspace);
            ASSERT_EQ(result.size(), expected.size());
            for (const auto& row : expected)
                EXPECT_TRUE(result.contains(packed(std::span<const uint_t>(row))));
            const JoinIndex<Values> left_index(*left, join_plan.lhs_keys());
            const JoinIndex<Values> right_index(*right, join_plan.rhs_keys());
            for (const auto* index : { &left_index, &right_index })
            {
                join(*left, *right, join_plan, *index, result, workspace);
                ASSERT_EQ(result.size(), expected.size());
                for (const auto& row : expected)
                    EXPECT_TRUE(result.contains(packed(std::span<const uint_t>(row))));
            }
            JoinIndexCache<Values> cache;
            for (const auto reuse : { JoinReuse {}, JoinReuse { true, false }, JoinReuse { false, true }, JoinReuse { true, true } })
            {
                join(*left, *right, join_plan, cache, reuse, result, workspace);
                ASSERT_EQ(result.size(), expected.size());
                for (const auto& row : expected)
                    EXPECT_TRUE(result.contains(packed(std::span<const uint_t>(row))));
            }
            Builder<Relation<Values>> projected(left_columns);
            project(result, project_plan, projected, workspace);
            std::set<std::vector<uint_t>> expected_projection;
            for (const auto& row : expected)
                expected_projection.emplace(row.begin(), row.begin() + left_columns.size());
            ASSERT_EQ(projected.size(), expected_projection.size());
            for (const auto& row : expected_projection)
                EXPECT_TRUE(projected.contains(packed(std::span<const uint_t>(row))));
            std::vector<ColumnIndex> expected_columns = left_columns;
            for (const auto column : right_columns)
            {
                bool shared = false;
                for (const auto left_column : left_columns)
                    shared = shared || column == left_column;
                if (!shared)
                    expected_columns.push_back(column);
            }
            std::vector<ColumnIndex> actual_columns;
            for (const auto column : result.columns())
                actual_columns.push_back(column.label);
            EXPECT_EQ(actual_columns, expected_columns);
            // Reuse one workspace across changing cardinalities and schemas.
            join(*left, *right, result, workspace);
            ASSERT_EQ(result.size(), expected.size());
            for (const auto& row : expected)
                EXPECT_TRUE(result.contains(packed(std::span<const uint_t>(row))));
        }
    }
}

template<typename R>
concept CanInsertTypedRow = requires(Builder<Relation<TypeList<ColumnIndex>>>& relation, const R& row) { relation.insert(row); };

static_assert(CanInsertTypedRow<std::tuple<ColumnIndex, ColumnIndex>>);
static_assert(CanInsertTypedRow<std::span<const std::byte>>);
static_assert(!CanInsertTypedRow<std::span<const uint_t>>);

template<typename V, typename R>
concept CanQueryTypedRow = requires(const V& view, const R& row) {
    { view.contains(row) } -> std::same_as<bool>;
};

using TypedRelationRepository = RelationRepository<TypeList<ColumnIndex>>;
using TypedBuilderView = View<Builder<Relation<TypeList<ColumnIndex>>>, TypedRelationRepository>;
using TypedDataView = View<Data<Relation<TypeList<ColumnIndex>>>, TypedRelationRepository>;
using TypedIndexView = View<Index<Relation<TypeList<ColumnIndex>>>, TypedRelationRepository>;
static_assert(CanQueryTypedRow<TypedBuilderView, std::tuple<ColumnIndex, ColumnIndex>>);
static_assert(CanQueryTypedRow<TypedDataView, std::tuple<ColumnIndex, ColumnIndex>>);
static_assert(CanQueryTypedRow<TypedIndexView, std::tuple<ColumnIndex, ColumnIndex>>);
static_assert(!CanQueryTypedRow<TypedBuilderView, std::span<const uint_t>>);
static_assert(!CanQueryTypedRow<TypedDataView, std::span<const uint_t>>);
static_assert(!CanQueryTypedRow<TypedIndexView, std::span<const uint_t>>);

TEST(YggdrasilTests, DatabaseRelationInsertsTypedTuplesAndCanonicalBytesWithoutStaging)
{
    const auto values = std::tuple { ColumnIndex(2), ColumnIndex(5), ColumnIndex(7) };
    auto relation = Builder<Relation<TypeList<ColumnIndex>>>({ ColumnIndex(0), ColumnIndex(1), ColumnIndex(2) });
    const auto encoded = encode_row<TypeList<ColumnIndex>>(values, relation.columns().span());
    EXPECT_EQ(relation.insert(values), 0);
    relation.set_index(Index<Relation<TypeList<ColumnIndex>>>(17));
    EXPECT_EQ(relation.insert(encoded), 0);
    EXPECT_NE(relation.get_index(), Index<Relation<TypeList<ColumnIndex>>>(17));
    EXPECT_EQ(relation.size(), 1);
    EXPECT_TRUE(relation.contains(values));
    EXPECT_TRUE(relation.contains(encoded));
    EXPECT_TRUE(std::ranges::equal(relation.row(0), encoded));
}

TEST(YggdrasilTests, DatabaseRelationViewsQueryTypedRowsWithinTheirOwnRowSet)
{
    auto repository = RelationRepositoryFactory<TypeList<ColumnIndex>>().create();
    auto relation = Builder<Relation<TypeList<ColumnIndex>>>({ ColumnIndex(0), ColumnIndex(1), ColumnIndex(2) });
    const auto values = std::tuple { ColumnIndex(2), ColumnIndex(5), ColumnIndex(7) };
    relation.insert(values);
    const auto encoded = encode_row<TypeList<ColumnIndex>>(values, relation.columns().span());
    const auto indexed = insert(repository, relation).first;
    const auto data_view = make_view(indexed.get_data(), repository);
    const auto builder_view = make_view(relation, repository);
    auto other = Builder<Relation<TypeList<ColumnIndex>>>({ ColumnIndex(0), ColumnIndex(1), ColumnIndex(2) });
    const auto other_row = std::tuple { ColumnIndex(2), ColumnIndex(5), ColumnIndex(6) };
    other.insert(other_row);
    const auto other_indexed = insert(repository, other).first;
    ASSERT_TRUE(other_indexed.contains(other_row));
    const auto missing = std::tuple { ColumnIndex(2), ColumnIndex(5), ColumnIndex(4) };
    const auto verify = [&](const auto& view)
    {
        EXPECT_TRUE(view.contains(values));
        EXPECT_TRUE(view.contains(encoded));
        EXPECT_FALSE(view.contains(other_row));
        EXPECT_FALSE(view.contains(missing));
        EXPECT_THROW(view.contains(std::tuple { ColumnIndex(2), ColumnIndex(5) }), std::invalid_argument);
        EXPECT_THROW(view.contains(std::span(encoded).first(encoded.size() - 1)), std::invalid_argument);
    };
    verify(relation);
    verify(builder_view);
    verify(data_view);
    verify(indexed);
}

TEST(YggdrasilTests, DatabaseRelationViewsQueryEmptyTypedRows)
{
    auto repository = RelationRepositoryFactory<TypeList<ColumnIndex>>().create();
    auto relation = Builder<Relation<TypeList<ColumnIndex>>>();
    const auto row = std::tuple {};
    const auto empty = insert(repository, relation).first;
    EXPECT_FALSE(make_view(relation, repository).contains(row));
    EXPECT_FALSE(make_view(empty.get_data(), repository).contains(row));
    EXPECT_FALSE(empty.contains(row));
    relation.insert(row);
    const auto unit = insert(repository, relation).first;
    EXPECT_TRUE(make_view(relation, repository).contains(row));
    EXPECT_TRUE(make_view(unit.get_data(), repository).contains(row));
    EXPECT_TRUE(unit.contains(row));
    EXPECT_FALSE(empty.contains(row));
}

TEST(YggdrasilTests, DatabaseRelationFailedEncodingPreservesCanonicalIndex)
{
    using TypedValues = TypeList<DatabaseThrowingValue>;
    auto relation = Builder<Relation<TypedValues>>({ ColumnIndex(0), ColumnIndex(1) });
    const auto canonical = Index<Relation<TypedValues>>(17);
    relation.set_index(canonical);
    const auto values = std::tuple { DatabaseThrowingValue { 2 }, DatabaseThrowingValue { 5 } };
    DatabaseThrowingValue::fail = true;
    EXPECT_THROW(relation.insert(values), std::runtime_error);
    EXPECT_THROW(relation.contains(values), std::runtime_error);
    DatabaseThrowingValue::fail = false;
    EXPECT_EQ(relation.get_index(), canonical);
    EXPECT_TRUE(relation.empty());
    EXPECT_THROW(relation.insert(std::tuple { DatabaseThrowingValue { 2 } }), std::invalid_argument);
    EXPECT_EQ(relation.get_index(), canonical);
    EXPECT_EQ(relation.insert(values), 0);
    EXPECT_NE(relation.get_index(), canonical);
    EXPECT_TRUE(relation.contains(values));
}

}  // namespace ygg::tests
