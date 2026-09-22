/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <array>
#include <cista/serialization.h>
#include <gtest/gtest.h>
#include <initializer_list>
#include <limits>
#include <random>
#include <set>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>
#include <yggdrasil/database/operations.hpp>
#include <yggdrasil/database/relation_pool.hpp>

namespace ygg::tests
{

struct DatabaseCountedValue
{
    uint_t value;
    static inline size_t comparisons = 0;
    friend bool operator==(const DatabaseCountedValue& lhs, const DatabaseCountedValue& rhs)
    {
        ++comparisons;
        return lhs.value == rhs.value;
    }
};

struct DatabaseCollisionValue
{
    uint_t value;
    friend bool operator==(const DatabaseCollisionValue&, const DatabaseCollisionValue&) = default;
};

}  // namespace ygg::tests

namespace ygg
{

template<>
struct Hash<tests::DatabaseCountedValue>
{
    hash_t operator()(const tests::DatabaseCountedValue& value) const noexcept { return value.value; }
};

template<>
struct Hash<tests::DatabaseCollisionValue>
{
    hash_t operator()(const tests::DatabaseCollisionValue&) const noexcept { return 0; }
};

template<>
struct EqualTo<tests::DatabaseCollisionValue>
{
    bool operator()(const tests::DatabaseCollisionValue& lhs, const tests::DatabaseCollisionValue& rhs) const noexcept
    {
        return lhs.value % 10 == rhs.value % 10;
    }
};

}  // namespace ygg

namespace ygg::tests
{

using namespace database;

static_assert(!std::is_copy_constructible_v<Relation<>>);
static_assert(std::is_move_constructible_v<Relation<>>);
static_assert(std::is_copy_constructible_v<RelationView<>>);

// Borrowing must be explicit: a temporary container must not become a span.
using TestColumns = std::array<Column, 2>;
using TestRows = RawArraySet<uint_t>;
static_assert(!std::is_constructible_v<RelationView<>, const TestRows&, TestColumns>);
static_assert(!std::is_constructible_v<RelationView<>, const TestRows&, const TestColumns>);
static_assert(!std::is_constructible_v<RelationView<>, const TestRows&, TestColumns&>);
static_assert(!std::is_constructible_v<RelationView<>, const TestRows&, std::vector<Column>>);
static_assert(!std::is_constructible_v<RelationView<>, const TestRows&, std::initializer_list<Column>>);
static_assert(std::is_constructible_v<RelationView<>, const TestRows&, const Columns&>);
static_assert(!std::is_constructible_v<RelationView<>, const TestRows&, Columns&&>);
static_assert(!std::is_constructible_v<RelationView<>, const TestRows&, const Columns&&>);
static_assert(std::is_constructible_v<RelationView<>, const TestRows&, std::span<Column, 2>>);
static_assert(std::is_constructible_v<RelationView<>, const TestRows&, std::span<const Column>>);
static_assert(!std::is_constructible_v<RelationView<>, TestRows&&, std::span<const Column>>);
static_assert(!std::is_constructible_v<RelationView<>, const TestRows&&, std::span<const Column>>);

static_assert(std::is_constructible_v<ColumnsView, const Columns&>);
static_assert(!std::is_constructible_v<ColumnsView, Columns&&>);
static_assert(!std::is_constructible_v<ColumnsView, const Columns&&>);
static_assert(std::is_constructible_v<ColumnsView, std::span<Column, 2>>);
static_assert(!std::is_constructible_v<ColumnsView, TestColumns&>);
static_assert(!std::is_constructible_v<ColumnsView, std::vector<Column>>);

template<typename C>
concept CanBorrowColumns = requires(C&& columns) { std::forward<C>(columns).view(); };

static_assert(CanBorrowColumns<const Columns&>);
static_assert(!CanBorrowColumns<Columns>);
static_assert(!CanBorrowColumns<const Columns>);

template<typename Owner>
concept CanBorrowRelationColumns = requires(Owner&& owner) { std::forward<Owner>(owner).columns(); };

template<typename Plan>
concept CanBorrowPlanColumns = requires(Plan&& plan) { std::forward<Plan>(plan).output_columns(); };

static_assert(CanBorrowRelationColumns<const Relation<>&>);
static_assert(!CanBorrowRelationColumns<Relation<>>);
static_assert(CanBorrowRelationColumns<const RelationView<>&>);
static_assert(CanBorrowRelationColumns<RelationView<>>);
static_assert(CanBorrowPlanColumns<const ProjectionPlan&>);
static_assert(!CanBorrowPlanColumns<ProjectionPlan>);
static_assert(CanBorrowPlanColumns<const JoinPlan&>);
static_assert(!CanBorrowPlanColumns<JoinPlan>);

template<typename Columns>
concept CanRename = requires(RelationView<> view, Columns&& columns) { rename(view, std::forward<Columns>(columns)); };

static_assert(!CanRename<TestColumns>);
static_assert(!CanRename<const TestColumns>);
static_assert(!CanRename<TestColumns&>);
static_assert(!CanRename<std::vector<Column>>);
static_assert(!CanRename<std::initializer_list<Column>>);
static_assert(CanRename<const Columns&>);
static_assert(!CanRename<Columns>);
static_assert(!CanRename<const Columns>);
static_assert(CanRename<std::span<Column, 2>>);
static_assert(CanRename<std::span<const Column>>);

namespace
{

template<typename T>
auto relocated_serialization(T value)
{
    const auto original = cista::serialize(value);
    auto relocated = original;
    EXPECT_NE(relocated.data(), original.data());
    return relocated;
}

template<typename T>
void expect_relation(const Relation<T>& relation,
                     std::initializer_list<Column> columns,
                     std::initializer_list<std::initializer_list<std::type_identity_t<T>>> rows)
{
    EXPECT_EQ(std::vector<Column>(relation.columns().begin(), relation.columns().end()), std::vector<Column>(columns));
    ASSERT_EQ(relation.size(), rows.size());
    for (const auto row : rows)
        EXPECT_TRUE(relation.contains(row));
}

std::set<std::vector<uint_t>> reference_join(RelationView<> lhs, RelationView<> rhs)
{
    std::vector<std::pair<size_t, size_t>> keys;
    std::vector<size_t> extra;
    for (size_t right = 0; right < rhs.arity(); ++right)
    {
        bool shared = false;
        for (size_t left = 0; left < lhs.arity(); ++left)
        {
            if (lhs.columns()[left] == rhs.columns()[right])
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
                matches = matches && lhs[left][l] == rhs[right][r];
            if (!matches)
                continue;
            auto row = std::vector<uint_t>(lhs[left].begin(), lhs[left].end());
            for (const auto column : extra)
                row.push_back(rhs[right][column]);
            result.insert(std::move(row));
        }
    }
    return result;
}

}  // namespace

TEST(YggdrasilTests, DatabaseColumnsOwnLabelsAndBorrowValidatedSchemas)
{
    std::vector<Column> labels { 4, 9 };
    Columns columns(labels);
    labels[0] = 8;
    EXPECT_EQ(columns.view()[0], 4);
    EXPECT_EQ(columns.size(), 2);
    EXPECT_FALSE(columns.empty());
    EXPECT_TRUE(Columns().empty());
    EXPECT_TRUE(ColumnsView().empty());
    const ColumnsView borrowed = columns;
    EXPECT_EQ(borrowed.data(), columns.view().data());
    EXPECT_EQ(borrowed.column_index(9), 1);
    EXPECT_EQ(columns.column_index(4), 0);
    EXPECT_THROW(columns.column_index(8), std::out_of_range);
    EXPECT_THROW(borrowed.column_index(8), std::out_of_range);
    auto copied = columns;
    Columns copied_view(borrowed);
    EXPECT_NE(copied.view().data(), borrowed.data());
    EXPECT_NE(copied_view.view().data(), borrowed.data());
    EXPECT_EQ(copied_view.column_index(9), 1);
    const Columns replacement { 5, 6 };
    columns.assign(replacement);
    EXPECT_EQ(copied.column_index(4), 0);
    EXPECT_EQ(copied_view.column_index(4), 0);
    EXPECT_THROW((Columns { 4, 4 }), std::invalid_argument);
    const std::array<Column, 2> duplicates { 4, 4 };
    EXPECT_THROW((ColumnsView(std::span(duplicates))), std::invalid_argument);
}

TEST(YggdrasilTests, DatabaseColumnsAssignmentRetainsStorageForSelfSubviews)
{
    Columns columns { 1, 2, 3, 4 };
    const auto* storage = columns.view().data();
    const auto memory = columns.memory_usage();
    columns.assign(columns);
    EXPECT_EQ(columns.view().data(), storage);
    EXPECT_EQ(columns.size(), 4);
    columns.assign(ColumnsView(columns.view().span().subspan(1, 2)));
    EXPECT_EQ(columns.view()[0], 2);
    EXPECT_EQ(columns.view()[1], 3);
    EXPECT_EQ(columns.size(), 2);
    EXPECT_EQ(columns.view().data(), storage);
    EXPECT_EQ(columns.memory_usage(), memory);
    columns.assign(ColumnsView());
    EXPECT_TRUE(columns.empty());
    EXPECT_EQ(columns.memory_usage(), memory);
    const Columns replacement { 5, 6, 7, 8 };
    columns.assign(replacement);
    EXPECT_EQ(columns.view().data(), storage);
    EXPECT_EQ(columns.memory_usage(), memory);
    EXPECT_EQ(columns.column_index(8), 3);
}

TEST(YggdrasilTests, DatabaseRelationRenamePreservesRowsAndStorage)
{
    Relation<> relation { 1, 2 };
    relation.insert({ 10, 20 });
    relation.insert({ 30, 40 });
    const auto* columns = relation.columns().data();
    const auto* first_row = relation[0].data();
    const auto* second_row = relation[1].data();
    const auto memory = relation.memory_usage();
    {
        const Columns labels { 4, 9 };
        relation.rename(labels);
        relation.rename(labels);
    }
    relation.rename(relation.columns());
    expect_relation(relation, { 4, 9 }, { { 10, 20 }, { 30, 40 } });
    EXPECT_EQ(relation.columns().data(), columns);
    EXPECT_EQ(relation[0].data(), first_row);
    EXPECT_EQ(relation[1].data(), second_row);
    EXPECT_EQ(relation.memory_usage(), memory);
    EXPECT_EQ(relation.insert({ 10, 20 }), 0);

    const Columns wrong_arity { 4 };
    EXPECT_THROW(relation.rename(wrong_arity), std::invalid_argument);
    expect_relation(relation, { 4, 9 }, { { 10, 20 }, { 30, 40 } });
    EXPECT_EQ(relation.columns().data(), columns);
    EXPECT_EQ(relation[0].data(), first_row);
    EXPECT_EQ(relation[1].data(), second_row);

    Relation<> nullary;
    nullary.rename(ColumnsView());
    EXPECT_TRUE(nullary.empty());
    nullary.insert({});
    nullary.rename(nullary.columns());
    expect_relation(nullary, {}, { {} });
}

TEST(YggdrasilTests, DatabaseRelationMaintainsSetAndSchemaInvariants)
{
    Relation<> relation(Columns { 4, 9 });
    EXPECT_EQ(relation.arity(), 2);
    EXPECT_TRUE(relation.empty());
    EXPECT_EQ(relation.insert({ 1, 2 }), 0);
    EXPECT_EQ(relation.insert({ 1, 2 }), 0);
    EXPECT_EQ(relation.insert({ 3, 4 }), 1);
    EXPECT_EQ(relation.size(), 2);
    EXPECT_EQ(relation.at(1)[0], 3);
    EXPECT_FALSE(relation.contains({ 1, 4 }));
    EXPECT_THROW(relation.insert({ 1 }), std::invalid_argument);
    EXPECT_THROW(relation.contains({ 1 }), std::invalid_argument);
    EXPECT_THROW(relation.at(2), std::out_of_range);
    EXPECT_THROW((Relation<>({ 4, 4 })), std::invalid_argument);
    auto view = relation.view();
    EXPECT_EQ(view.column_index(9), 1);
    EXPECT_THROW(view.column_index(5), std::out_of_range);
    EXPECT_TRUE(view.contains({ 3, 4 }));
    EXPECT_THROW(view.at(2), std::out_of_range);
    relation.clear();
    expect_relation(relation, { 4, 9 }, {});
    EXPECT_EQ(relation.insert({ 5, 6 }), 0);
}

TEST(YggdrasilTests, DatabaseRenameOnlyChangesBorrowedViewMetadata)
{
    Relation<> relation({ 1, 2 });
    relation.insert({ 7, 8 });
    const Columns first_labels { 3, 4 };
    const Columns second_labels { 5, 6 };
    auto renamed = rename(relation.view(), first_labels);
    auto copied = renamed;
    renamed = rename(relation.view(), second_labels);
    EXPECT_EQ(copied.column_index(3), 0);
    EXPECT_EQ(copied.column_index(4), 1);
    EXPECT_EQ(copied[0].data(), relation[0].data());
    EXPECT_EQ(renamed[0].data(), relation[0].data());
    EXPECT_EQ(relation.columns()[0], 1);
    const Columns wrong_arity { 3 };
    const std::array<Column, 2> duplicates { 3, 3 };
    EXPECT_THROW(rename(relation.view(), wrong_arity), std::invalid_argument);
    EXPECT_THROW(rename(relation.view(), std::span(duplicates)), std::invalid_argument);
    auto projected = project(copied, { 4, 3 });
    expect_relation(projected, { 4, 3 }, { { 8, 7 } });
    auto projected_view = project(copied, projected.columns());
    expect_relation(projected_view, { 4, 3 }, { { 8, 7 } });
}

TEST(YggdrasilTests, DatabaseBorrowedViewsShareSchemasAndObserveRefills)
{
    Relation<> relation({ 1, 2 });
    relation.insert({ 7, 8 });
    auto view = relation.view();
    auto copy = view;
    EXPECT_EQ(view.columns().data(), relation.columns().data());
    EXPECT_EQ(copy.columns().data(), relation.columns().data());
    const std::array<Column, 2> labels { 3, 4 };
    auto renamed = rename(view, std::span<const Column>(labels));
    EXPECT_EQ(renamed.columns().data(), labels.data());
    auto moved = std::move(renamed);
    EXPECT_EQ(moved.column_index(4), 1);
    relation.clear();
    EXPECT_TRUE(view.empty());
    relation.insert({ 9, 10 });
    EXPECT_TRUE(moved.contains({ 9, 10 }));
    std::array<Column, 2> mutable_labels { 7, 8 };
    auto direct_borrowed = RelationView<>(relation.storage(), std::span(mutable_labels));
    EXPECT_EQ(direct_borrowed.columns().data(), mutable_labels.data());
    auto renamed_mutable = rename(view, std::span(mutable_labels));
    EXPECT_EQ(renamed_mutable.column_index(8), 1);
    EXPECT_EQ(renamed_mutable.columns().data(), mutable_labels.data());
    const Columns validated { 11, 12 };
    auto validated_borrowed = RelationView<>(relation.storage(), validated.view());
    EXPECT_EQ(validated_borrowed.columns().data(), validated.view().data());
    auto direct_borrowed_schema = RelationView<>(relation.storage(), validated);
    auto validated_copy = direct_borrowed_schema;
    EXPECT_EQ(direct_borrowed_schema.columns().data(), validated.view().data());
    EXPECT_EQ(validated_copy.columns().data(), validated.view().data());
    EXPECT_EQ(validated_copy.column_index(12), 1);
}

TEST(YggdrasilTests, DatabaseRelationReinitializationRetainsCompatibleStorage)
{
    Relation<> relation({ 1, 2 });
    relation.insert({ 3, 4 });
    const auto memory = relation.memory_usage();
    EXPECT_THROW(relation.initialize({ 5, 5 }), std::invalid_argument);
    expect_relation(relation, { 1, 2 }, { { 3, 4 } });
    relation.initialize(relation.columns());
    EXPECT_EQ(relation.memory_usage(), memory);
    expect_relation(relation, { 1, 2 }, {});
    relation.insert({ 3, 4 });
    const Columns validated { 5, 6 };
    relation.initialize(validated);
    EXPECT_EQ(relation.memory_usage(), memory);
    expect_relation(relation, { 5, 6 }, {});
    relation.initialize({ 7 });
    relation.insert({ 9 });
    expect_relation(relation, { 7 }, { { 9 } });
    relation.initialize({});
    relation.insert({});
    expect_relation(relation, {}, { {} });
}

TEST(YggdrasilTests, DatabasePoolKeepsLiveRelationsDistinctAndReusesByArity)
{
    RelationPool<> pool;
    auto first = pool.get_or_allocate({ 1, 2 });
    first->insert({ 3, 4 });
    auto second = pool.get_or_allocate({ 1, 2 });
    second->insert({ 5, 6 });
    EXPECT_NE(first.get(), second.get());
    const auto* released = second.get();
    const auto memory = second->memory_usage();
    second = {};
    auto unary = pool.get_or_allocate({ 1 });
    unary->insert({ 8 });
    const Columns validated { 7, 8 };
    auto reused = pool.get_or_allocate(validated);
    EXPECT_EQ(reused.get(), released);
    EXPECT_EQ(reused->memory_usage(), memory);
    expect_relation(*reused, { 7, 8 }, {});
    expect_relation(*first, { 1, 2 }, { { 3, 4 } });
    EXPECT_THROW(pool.get_or_allocate({ 9, 9 }), std::invalid_argument);
    auto boolean = pool.get_or_allocate({});
    boolean->insert({});
    expect_relation(*boolean, {}, { {} });
}

TEST(YggdrasilTests, DatabaseProjectionReordersDeduplicatesAndOwnsResults)
{
    Relation<> relation({ 1, 2, 3 });
    relation.insert({ 7, 8, 1 });
    relation.insert({ 7, 8, 2 });
    relation.insert({ 9, 8, 3 });
    auto projected = project(relation.view(), { 2, 1 });
    expect_relation(projected, { 2, 1 }, { { 8, 7 }, { 8, 9 } });
    EXPECT_THROW(project(relation.view(), { 1, 1 }), std::invalid_argument);
    EXPECT_THROW(project(relation.view(), { 4 }), std::out_of_range);
    Relation<> output({ 2, 1 });
    output.insert({ 99, 99 });
    project(relation.view(), { 2, 1 }, output);
    expect_relation(output, { 2, 1 }, { { 8, 7 }, { 8, 9 } });
    relation.clear();
    expect_relation(projected, { 2, 1 }, { { 8, 7 }, { 8, 9 } });
    project(relation.view(), { 2, 1 }, output);
    expect_relation(output, { 2, 1 }, {});
}

TEST(YggdrasilTests, DatabaseRawColumnInputsAcceptSpansArraysAndVectors)
{
    std::array<Column, 3> labels { 1, 2, 3 };
    Columns columns { std::span<const Column>(labels) };
    Relation<> relation(labels);
    relation.insert({ 7, 8, 9 });
    labels.fill(0);
    EXPECT_EQ(columns.column_index(3), 2);
    expect_relation(relation, { 1, 2, 3 }, { { 7, 8, 9 } });

    std::array<Column, 2> output_labels { 3, 1 };
    auto from_span = project(relation.view(), std::span(output_labels));
    auto from_array = project(relation.view(), output_labels);
    auto from_vector = project(relation.view(), std::vector<Column> { 3, 1 });
    output_labels.fill(0);
    expect_relation(from_span, { 3, 1 }, { { 9, 7 } });
    expect_relation(from_array, { 3, 1 }, { { 9, 7 } });
    expect_relation(from_vector, { 3, 1 }, { { 9, 7 } });
}

TEST(YggdrasilTests, DatabasePlansOwnSchemasAndReuseResolvedPositions)
{
    constexpr auto large = std::numeric_limits<Column>::max();
    auto plans = []
    {
        std::array<Column, 3> lhs { large, 1, 2 };
        std::array<Column, 3> rhs { 2, 3, large };
        auto projection = ProjectionPlan(lhs, { 2, large });
        auto join = JoinPlan(lhs, rhs);
        lhs.fill(0);
        rhs.fill(0);
        return std::pair(std::move(projection), std::move(join));
    }();
    auto copied = plans;
    auto [projection, joining] = std::move(copied);
    Relation<> left({ large, 1, 2 });
    Relation<> right({ 2, 3, large });
    Relation<> projected({ 2, large });
    Relation<> joined({ large, 1, 2, 3 });
    Workspace<> workspace;
    for (uint_t generation = 0; generation < 3; ++generation)
    {
        left.clear();
        right.clear();
        left.insert({ 7 + generation, 8, 9 });
        left.insert({ 7 + generation, 10, 9 });
        right.insert({ 9, 11, 7 + generation });
        project(left.view(), projection, projected, workspace);
        expect_relation(projected, { 2, large }, { { 9, 7 + generation } });
        join(left.view(), right.view(), joining, joined, workspace);
        expect_relation(joined, { large, 1, 2, 3 }, { { 7 + generation, 8, 9, 11 }, { 7 + generation, 10, 9, 11 } });
    }
    const ProjectionPlan guard(left.columns(), {});
    Relation<> exists;
    project(left.view(), guard, exists, workspace);
    expect_relation(exists, {}, { {} });
    left.clear();
    project(left.view(), guard, exists, workspace);
    expect_relation(exists, {}, {});
}

TEST(YggdrasilTests, DatabaseColumnsAndPlansSurviveCistaRelocation)
{
    // The owning objects and original byte buffers are gone before decoding.
    auto columns_bytes = relocated_serialization(Columns { 3, 1, 2 });
    auto projection_bytes = relocated_serialization(ProjectionPlan({ 3, 1, 2, 4 }, { 4, 3 }));
    auto join_bytes = relocated_serialization(JoinPlan({ 3, 1, 2 }, { 2, 4, 1 }));
    const auto* columns = cista::deserialize<Columns>(columns_bytes);
    const auto* projection = cista::deserialize<ProjectionPlan>(projection_bytes);
    const auto* joining = cista::deserialize<JoinPlan>(join_bytes);
    const auto values = [](auto range) { return std::vector(range.begin(), range.end()); };
    EXPECT_EQ(values(columns->view()), (std::vector<Column> { 3, 1, 2 }));
    EXPECT_EQ(columns->column_index(1), 1);
    EXPECT_EQ(values(projection->input_columns()), (std::vector<Column> { 3, 1, 2, 4 }));
    EXPECT_EQ(values(projection->output_columns()), (std::vector<Column> { 4, 3 }));
    EXPECT_EQ(values(projection->positions()), (std::vector<size_t> { 3, 0 }));
    EXPECT_EQ(values(joining->lhs_columns()), (std::vector<Column> { 3, 1, 2 }));
    EXPECT_EQ(values(joining->rhs_columns()), (std::vector<Column> { 2, 4, 1 }));
    EXPECT_EQ(values(joining->output_columns()), (std::vector<Column> { 3, 1, 2, 4 }));
    EXPECT_EQ(values(joining->lhs_keys()), (std::vector<size_t> { 2, 1 }));
    EXPECT_EQ(values(joining->rhs_keys()), (std::vector<size_t> { 0, 2 }));
    EXPECT_EQ(values(joining->rhs_payload()), (std::vector<size_t> { 1 }));

    // Keep the relocated buffers unchanged and alive while using decoded plans.
    Relation<> left(columns->view());
    Relation<> right(joining->rhs_columns());
    left.insert({ 10, 1, 2 });
    left.insert({ 11, 1, 3 });
    right.insert({ 2, 20, 1 });
    right.insert({ 3, 30, 1 });
    right.insert({ 2, 99, 9 });
    Relation<> joined(joining->output_columns());
    Relation<> projected(projection->output_columns());
    Workspace<> workspace;
    join(left.view(), right.view(), *joining, joined, workspace);
    expect_relation(joined, { 3, 1, 2, 4 }, { { 10, 1, 2, 20 }, { 11, 1, 3, 30 } });
    project(joined.view(), *projection, projected, workspace);
    expect_relation(projected, { 4, 3 }, { { 20, 10 }, { 30, 11 } });
}

TEST(YggdrasilTests, DatabaseDefaultPlansRoundTripAndEvaluateNullaryRelations)
{
    auto columns_bytes = relocated_serialization(Columns());
    auto projection_bytes = relocated_serialization(ProjectionPlan());
    auto join_bytes = relocated_serialization(JoinPlan());
    const auto* columns = cista::deserialize<Columns>(columns_bytes);
    const auto* projection = cista::deserialize<ProjectionPlan>(projection_bytes);
    const auto* joining = cista::deserialize<JoinPlan>(join_bytes);
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

    Relation<> left, right, projected, joined;
    Workspace<> workspace;
    for (const bool lhs_nonempty : { false, true })
        for (const bool rhs_nonempty : { false, true })
        {
            left.clear();
            right.clear();
            if (lhs_nonempty)
                left.insert({});
            if (rhs_nonempty)
                right.insert({});
            project(left.view(), *projection, projected, workspace);
            EXPECT_EQ(projected.arity(), 0);
            EXPECT_EQ(projected.size(), lhs_nonempty ? 1 : 0);
            join(left.view(), right.view(), *joining, joined, workspace);
            EXPECT_EQ(joined.arity(), 0);
            EXPECT_EQ(joined.size(), lhs_nonempty && rhs_nonempty ? 1 : 0);
        }
}

TEST(YggdrasilTests, DatabasePreparedPlansValidateSchemasAndAliasesBeforeClearingOutputs)
{
    Relation<> left({ 1, 2 });
    Relation<> right({ 2, 3 });
    const ProjectionPlan projection(left.columns(), { 1 });
    const JoinPlan joining(left.columns(), right.columns());
    Relation<> projected({ 1 });
    projected.insert({ 99 });
    Relation<> joined({ 1, 2, 3 });
    joined.insert({ 99, 99, 99 });
    Workspace<> workspace;

    // A stale plan must be rejected even when its input contains no rows.
    left.initialize({ 2, 1 });
    EXPECT_THROW(project(left.view(), projection, projected, workspace), std::invalid_argument);
    EXPECT_THROW(join(left.view(), right.view(), joining, joined, workspace), std::invalid_argument);
    left.initialize({ 1, 4 });
    EXPECT_THROW(project(left.view(), projection, projected, workspace), std::invalid_argument);
    EXPECT_THROW(join(left.view(), right.view(), joining, joined, workspace), std::invalid_argument);
    left.initialize({ 1, 2 });
    right.initialize({ 3, 2 });
    EXPECT_THROW(join(left.view(), right.view(), joining, joined, workspace), std::invalid_argument);
    right.initialize({ 2, 4 });
    EXPECT_THROW(join(left.view(), right.view(), joining, joined, workspace), std::invalid_argument);
    expect_relation(projected, { 1 }, { { 99 } });
    expect_relation(joined, { 1, 2, 3 }, { { 99, 99, 99 } });

    right.initialize({ 2, 3 });
    projected.initialize({ 2 });
    projected.insert({ 88 });
    joined.initialize({ 1, 3, 2 });
    joined.insert({ 88, 88, 88 });
    EXPECT_THROW(project(left.view(), projection, projected, workspace), std::invalid_argument);
    EXPECT_THROW(join(left.view(), right.view(), joining, joined, workspace), std::invalid_argument);
    expect_relation(projected, { 2 }, { { 88 } });
    expect_relation(joined, { 1, 3, 2 }, { { 88, 88, 88 } });

    left.insert({ 7, 8 });
    const ProjectionPlan identity(left.columns(), left.columns());
    const JoinPlan same(left.columns(), left.columns());
    EXPECT_THROW(project(left.view(), identity, left, workspace), std::invalid_argument);
    EXPECT_THROW(join(left.view(), left.view(), same, left, workspace), std::invalid_argument);
    expect_relation(left, { 1, 2 }, { { 7, 8 } });

    EXPECT_THROW((ProjectionPlan({ 1, 1 }, { 1 })), std::invalid_argument);
    EXPECT_THROW((ProjectionPlan({ 1, 2 }, { 1, 1 })), std::invalid_argument);
    EXPECT_THROW((ProjectionPlan({ 1 }, { 2 })), std::out_of_range);
    EXPECT_THROW((JoinPlan({ 1, 1 }, { 2 })), std::invalid_argument);
    EXPECT_THROW((JoinPlan({ 1 }, { 2, 2 })), std::invalid_argument);
}

TEST(YggdrasilTests, DatabaseSelectionsSupportColumnsConstantsAndPredicates)
{
    Relation<> relation({ 1, 2 });
    relation.insert({ 1, 1 });
    relation.insert({ 1, 2 });
    relation.insert({ 2, 2 });
    expect_relation(select_equal_columns(relation.view(), 1, 2), { 1, 2 }, { { 1, 1 }, { 2, 2 } });
    expect_relation(select_equal_value(relation.view(), 1, uint_t(1)), { 1, 2 }, { { 1, 1 }, { 1, 2 } });
    const auto less = [](std::span<const uint_t> row) { return row[0] < row[1]; };
    expect_relation(select(relation.view(), less), { 1, 2 }, { { 1, 2 } });
    Relation<> output({ 1, 2 });
    output.insert({ 99, 99 });
    select_equal_columns(relation.view(), 1, 2, output);
    expect_relation(output, { 1, 2 }, { { 1, 1 }, { 2, 2 } });
    select_equal_value(relation.view(), 2, uint_t(2), output);
    expect_relation(output, { 1, 2 }, { { 1, 2 }, { 2, 2 } });
    select(relation.view(), less, output);
    expect_relation(output, { 1, 2 }, { { 1, 2 } });
    EXPECT_THROW(select_equal_columns(relation.view(), 1, 3), std::out_of_range);
    EXPECT_THROW(select_equal_value(relation.view(), 3, uint_t(1)), std::out_of_range);
}

TEST(YggdrasilTests, DatabaseJoinPreservesAllMatchingRowsAndLogicalColumnOrder)
{
    Relation<> left({ 3, 1, 2 });
    Relation<> right({ 2, 4, 1 });
    left.insert({ 10, 1, 2 });
    left.insert({ 11, 1, 2 });
    left.insert({ 12, 1, 3 });
    right.insert({ 2, 20, 1 });
    right.insert({ 2, 21, 1 });
    right.insert({ 2, 22, 9 });
    right.insert({ 3, 23, 1 });
    right.insert({ 8, 24, 8 });
    expect_relation(join(left.view(), right.view()),
                    { 3, 1, 2, 4 },
                    { { 10, 1, 2, 20 }, { 10, 1, 2, 21 }, { 11, 1, 2, 20 }, { 11, 1, 2, 21 }, { 12, 1, 3, 23 } });
    expect_relation(join(right.view(), left.view()),
                    { 2, 4, 1, 3 },
                    { { 2, 20, 1, 10 }, { 2, 20, 1, 11 }, { 2, 21, 1, 10 }, { 2, 21, 1, 11 }, { 3, 23, 1, 12 } });
    Relation<> output({ 3, 1, 2, 4 });
    output.insert({ 99, 99, 99, 99 });
    join(left.view(), right.view(), output);
    EXPECT_EQ(output.size(), 5);
    right.clear();
    join(left.view(), right.view(), output);
    expect_relation(output, { 3, 1, 2, 4 }, {});
}

TEST(YggdrasilTests, DatabaseJoinHandlesCartesianProductsAndAllSharedColumns)
{
    Relation<> left({ 1 });
    Relation<> right({ 2 });
    left.insert({ 3 });
    left.insert({ 4 });
    right.insert({ 5 });
    right.insert({ 6 });
    expect_relation(join(left.view(), right.view()), { 1, 2 }, { { 3, 5 }, { 3, 6 }, { 4, 5 }, { 4, 6 } });
    auto pairs = join(left.view(), right.view());
    Relation<> reversed({ 2, 1 });
    reversed.insert({ 5, 3 });
    reversed.insert({ 8, 3 });
    expect_relation(join(pairs.view(), reversed.view()), { 1, 2 }, { { 3, 5 } });
}

TEST(YggdrasilTests, DatabaseSetOperationsRequireAlignedSignatures)
{
    Relation<> left({ 1, 2 });
    Relation<> right({ 1, 2 });
    left.insert({ 1, 2 });
    left.insert({ 3, 4 });
    right.insert({ 3, 4 });
    right.insert({ 5, 6 });
    expect_relation(union_(left.view(), right.view()), { 1, 2 }, { { 1, 2 }, { 3, 4 }, { 5, 6 } });
    expect_relation(difference(left.view(), right.view()), { 1, 2 }, { { 1, 2 } });
    expect_relation(difference(left.view(), left.view()), { 1, 2 }, {});
    Relation<> output({ 1, 2 });
    output.insert({ 99, 99 });
    union_(left.view(), right.view(), output);
    EXPECT_EQ(output.size(), 3);
    difference(left.view(), right.view(), output);
    expect_relation(output, { 1, 2 }, { { 1, 2 } });
    auto reversed = project(right.view(), { 2, 1 });
    EXPECT_THROW(union_(left.view(), reversed.view()), std::invalid_argument);
    EXPECT_THROW(difference(left.view(), reversed.view()), std::invalid_argument);
    auto aligned = project(reversed.view(), { 1, 2 });
    expect_relation(difference(left.view(), aligned.view()), { 1, 2 }, { { 1, 2 } });
}

TEST(YggdrasilTests, DatabaseNullaryRelationsObeyBooleanLaws)
{
    Relation<> false_;
    Relation<> true_;
    EXPECT_EQ(true_.insert({}), 0);
    EXPECT_EQ(true_.insert({}), 0);
    EXPECT_EQ(true_.size(), 1);
    EXPECT_EQ(true_.arity(), 0);
    EXPECT_TRUE(true_.contains({}));
    EXPECT_FALSE(false_.contains({}));
    expect_relation(join(true_.view(), true_.view()), {}, { {} });
    expect_relation(join(true_.view(), false_.view()), {}, {});
    expect_relation(union_(true_.view(), false_.view()), {}, { {} });
    expect_relation(difference(true_.view(), false_.view()), {}, { {} });
    expect_relation(difference(true_.view(), true_.view()), {}, {});
    Relation<> objects({ 1 });
    objects.insert({ 7 });
    expect_relation(join(objects.view(), true_.view()), { 1 }, { { 7 } });
    expect_relation(join(true_.view(), objects.view()), { 1 }, { { 7 } });
    expect_relation(join(false_.view(), objects.view()), { 1 }, {});
    expect_relation(project(objects.view(), {}), {}, { {} });
    Relation<> output;
    project(objects.view(), {}, output);
    expect_relation(output, {}, { {} });
    objects.clear();
    project(objects.view(), {}, output);
    expect_relation(output, {}, {});
    expect_relation(select(true_.view(), [](std::span<const uint_t> row) { return row.empty(); }), {}, { {} });
    expect_relation(select(true_.view(), [](std::span<const uint_t>) { return false; }), {}, {});
}

TEST(YggdrasilTests, DatabaseOutputGuardsPreserveExistingResults)
{
    Relation<> input({ 1, 2 });
    input.insert({ 3, 4 });
    Relation<> wrong({ 2, 1 });
    wrong.insert({ 8, 9 });
    const auto keep = [](std::span<const uint_t>) { return true; };
    EXPECT_THROW(select(input.view(), keep, wrong), std::invalid_argument);
    EXPECT_THROW(select_equal_columns(input.view(), 1, 2, wrong), std::invalid_argument);
    EXPECT_THROW(select_equal_value(input.view(), 1, uint_t(3), wrong), std::invalid_argument);
    EXPECT_THROW(project(input.view(), { 1, 2 }, wrong), std::invalid_argument);
    EXPECT_THROW(join(input.view(), input.view(), wrong), std::invalid_argument);
    EXPECT_THROW(union_(input.view(), input.view(), wrong), std::invalid_argument);
    EXPECT_THROW(difference(input.view(), input.view(), wrong), std::invalid_argument);
    expect_relation(wrong, { 2, 1 }, { { 8, 9 } });
    EXPECT_THROW(select(input.view(), keep, input), std::invalid_argument);
    EXPECT_THROW(select_equal_columns(input.view(), 1, 2, input), std::invalid_argument);
    EXPECT_THROW(select_equal_value(input.view(), 1, uint_t(3), input), std::invalid_argument);
    EXPECT_THROW(project(input.view(), { 1, 2 }, input), std::invalid_argument);
    EXPECT_THROW(join(input.view(), input.view(), input), std::invalid_argument);
    EXPECT_THROW(union_(input.view(), input.view(), input), std::invalid_argument);
    EXPECT_THROW(difference(input.view(), input.view(), input), std::invalid_argument);
    EXPECT_THROW(join(input.view(), wrong.view(), input), std::invalid_argument);
    Relation<> rhs_only({ 1, 2 });
    rhs_only.insert({ 9, 10 });
    EXPECT_THROW(join(input.view(), rhs_only.view(), rhs_only), std::invalid_argument);
    EXPECT_THROW(union_(input.view(), rhs_only.view(), rhs_only), std::invalid_argument);
    EXPECT_THROW(difference(input.view(), rhs_only.view(), rhs_only), std::invalid_argument);
    expect_relation(rhs_only, { 1, 2 }, { { 9, 10 } });
    auto alias = rename(input.view(), input.columns());
    EXPECT_THROW(select(alias, keep, input), std::invalid_argument);
    expect_relation(input, { 1, 2 }, { { 3, 4 } });
    Relation<> output({ 1, 2 });
    output.insert({ 8, 9 });
    EXPECT_THROW(project(input.view(), { 1, 9 }, output), std::out_of_range);
    EXPECT_THROW(select_equal_columns(input.view(), 1, 9, output), std::out_of_range);
    EXPECT_THROW(select_equal_value(input.view(), 9, uint_t(3), output), std::out_of_range);
    EXPECT_THROW(union_(input.view(), wrong.view(), output), std::invalid_argument);
    EXPECT_THROW(difference(input.view(), wrong.view(), output), std::invalid_argument);
    expect_relation(output, { 1, 2 }, { { 8, 9 } });
}

TEST(YggdrasilTests, DatabaseOperatorsRespectCustomEqualityDespiteHashCollisions)
{
    using Value = DatabaseCollisionValue;
    Relation<Value> left({ 1, 2 });
    Relation<Value> right({ 1, 3 });
    left.insert({ Value { 1 }, Value { 2 } });
    left.insert({ Value { 3 }, Value { 4 } });
    EXPECT_EQ(left.insert({ Value { 11 }, Value { 12 } }), 0);
    right.insert({ Value { 11 }, Value { 5 } });
    right.insert({ Value { 6 }, Value { 7 } });
    auto joined = join(left.view(), right.view());
    EXPECT_EQ(joined.size(), 1);
    EXPECT_TRUE(joined.contains({ Value { 1 }, Value { 2 }, Value { 5 } }));
    auto selected = select_equal_value(left.view(), 1, Value { 11 });
    EXPECT_EQ(selected.size(), 1);
    EXPECT_TRUE(selected.contains({ Value { 1 }, Value { 2 } }));
    Relation<Value> pairs({ 1, 2 });
    pairs.insert({ Value { 1 }, Value { 11 } });
    pairs.insert({ Value { 2 }, Value { 3 } });
    auto equal = select_equal_columns(pairs.view(), 1, 2);
    EXPECT_EQ(equal.size(), 1);
    EXPECT_TRUE(equal.contains({ Value { 1 }, Value { 11 } }));
}

TEST(YggdrasilTests, DatabaseKeyedJoinDoesNotCompareEveryPairOfRows)
{
    using Value = DatabaseCountedValue;
    constexpr uint_t count = 2048;
    Relation<Value> left({ 1, 2 });
    Relation<Value> right({ 1, 3 });
    for (uint_t i = 0; i < count; ++i)
    {
        left.insert({ Value { i }, Value { i + 1 } });
        right.insert({ Value { i }, Value { i + 2 } });
    }
    Value::comparisons = 0;
    auto result = join(left.view(), right.view());
    EXPECT_EQ(result.size(), count);
    // Generous collision allowance, but far below the quadratic cross product.
    EXPECT_LT(Value::comparisons, size_t(count) * 64);
}

TEST(YggdrasilTests, DatabaseNaturalJoinMatchesSmallReferenceAcrossSchemas)
{
    using SchemaPair = std::pair<std::vector<Column>, std::vector<Column>>;
    const std::array<SchemaPair, 6> schemas = { {
        { { 1, 2, 3 }, { 3, 4, 1 } },
        { { 1, 2 }, { 2, 1 } },
        { { 1 }, { 2, 3 } },
        { {}, { 1 } },
        { { 1 }, {} },
        { {}, {} },
    } };
    std::mt19937 random(1701);
    Workspace<> workspace;
    for (const auto& [left_columns, right_columns] : schemas)
    {
        const JoinPlan join_plan(left_columns, right_columns);
        const ProjectionPlan project_plan(join_plan.output_columns(), left_columns);
        for (size_t trial = 0; trial < 25; ++trial)
        {
            SCOPED_TRACE(trial);
            Relation<> left(left_columns);
            Relation<> right(right_columns);
            for (auto* relation : { &left, &right })
            {
                const auto count = random() % 9;
                std::vector<uint_t> row(relation->arity());
                for (size_t i = 0; i < count; ++i)
                {
                    for (auto& value : row)
                        value = random() % 4;
                    relation->insert(row);
                }
            }
            const auto expected = reference_join(left.view(), right.view());
            auto result = join(left.view(), right.view());
            ASSERT_EQ(result.size(), expected.size());
            for (const auto& row : expected)
                EXPECT_TRUE(result.contains(row));
            join(left.view(), right.view(), join_plan, result, workspace);
            ASSERT_EQ(result.size(), expected.size());
            for (const auto& row : expected)
                EXPECT_TRUE(result.contains(row));
            Relation<> projected(left_columns);
            project(result.view(), project_plan, projected, workspace);
            std::set<std::vector<uint_t>> expected_projection;
            for (const auto& row : expected)
                expected_projection.emplace(row.begin(), row.begin() + left_columns.size());
            ASSERT_EQ(projected.size(), expected_projection.size());
            for (const auto& row : expected_projection)
                EXPECT_TRUE(projected.contains(row));
            std::vector<Column> expected_columns = left_columns;
            for (const auto column : right_columns)
            {
                bool shared = false;
                for (const auto left_column : left_columns)
                    shared = shared || column == left_column;
                if (!shared)
                    expected_columns.push_back(column);
            }
            EXPECT_EQ(std::vector<Column>(result.columns().begin(), result.columns().end()), expected_columns);
            // Reuse one workspace across changing cardinalities and schemas.
            join(left.view(), right.view(), result, workspace);
            ASSERT_EQ(result.size(), expected.size());
            for (const auto& row : expected)
                EXPECT_TRUE(result.contains(row));
        }
    }
}

}  // namespace ygg::tests
