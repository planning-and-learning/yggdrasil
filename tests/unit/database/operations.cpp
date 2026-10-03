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
#include <yggdrasil/database/relation_repository.hpp>

namespace ygg::tests
{

struct DatabaseCountedValue
{
    uint_t value;
    static inline size_t comparisons = 0;
    static inline size_t hashes = 0;
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
    hash_t operator()(const tests::DatabaseCountedValue& value) const noexcept
    {
        ++tests::DatabaseCountedValue::hashes;
        return value.value;
    }
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
using ColumnIndex = Index<database::Column>;

static_assert(!std::is_copy_constructible_v<Builder<Relation<>>>);
static_assert(std::is_move_constructible_v<Builder<Relation<>>>);
static_assert(RelationViewConcept<Builder<Relation<>>>);

static_assert(!std::same_as<ColumnIndex, uint_t>);
static_assert(!std::is_convertible_v<uint_t, ColumnIndex> && !std::is_convertible_v<ColumnIndex, uint_t>);
static_assert(sizeof(ColumnIndex) == sizeof(uint_t));
static_assert(std::is_trivially_copyable_v<ColumnIndex>);

template<typename C>
concept CanBorrowColumns = requires(C&& columns) { std::forward<C>(columns).span(); };

static_assert(CanBorrowColumns<const Builder<Columns>&>);
static_assert(!CanBorrowColumns<Builder<Columns>>);
static_assert(!CanBorrowColumns<const Builder<Columns>>);

template<typename Owner>
concept CanBorrowRelationColumns = requires(Owner&& owner) { std::forward<Owner>(owner).columns(); };

template<typename Plan>
concept CanBorrowPlanColumns = requires(Plan&& plan) { std::forward<Plan>(plan).output_columns(); };

static_assert(CanBorrowRelationColumns<const Builder<Relation<>>&>);
static_assert(!CanBorrowRelationColumns<Builder<Relation<>>>);
static_assert(CanBorrowPlanColumns<const ProjectionPlan&>);
static_assert(!CanBorrowPlanColumns<ProjectionPlan>);
static_assert(CanBorrowPlanColumns<const JoinPlan&>);
static_assert(!CanBorrowPlanColumns<JoinPlan>);
static_assert(std::same_as<decltype(std::declval<const Builder<Relation<>>&>().columns()), const Builder<Columns>&>);
static_assert(std::same_as<decltype(std::declval<const ProjectionPlan&>().output_columns()), View<Builder<Columns>, ProjectionPlan>>);
static_assert(std::same_as<decltype(std::declval<const JoinPlan&>().output_columns()), View<Builder<Columns>, JoinPlan>>);

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
void expect_relation(const Builder<Relation<T>>& relation,
                     std::initializer_list<ColumnIndex> columns,
                     std::initializer_list<std::initializer_list<std::type_identity_t<T>>> rows)
{
    EXPECT_EQ(std::vector<ColumnIndex>(relation.columns().begin(), relation.columns().end()), std::vector<ColumnIndex>(columns));
    ASSERT_EQ(relation.size(), rows.size());
    for (const auto row : rows)
        EXPECT_TRUE(relation.contains(row));
}

std::set<std::vector<uint_t>> reference_join(const Builder<Relation<>>& lhs, const Builder<Relation<>>& rhs)
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

TEST(YggdrasilTests, DatabaseColumnsOwnValidSchemasAndViewsBorrowLabels)
{
    std::vector<ColumnIndex> labels { ColumnIndex(4), ColumnIndex(9) };
    Builder<Columns> columns(labels);
    labels[0] = ColumnIndex(8);
    EXPECT_EQ(columns.span()[0], ColumnIndex(4));
    EXPECT_EQ(columns.size(), 2);
    EXPECT_FALSE(columns.empty());
    EXPECT_TRUE(Builder<Columns>().empty());
    auto repository = RelationRepositoryFactory<>().create();
    const auto borrowed = make_view(columns, repository);
    EXPECT_EQ(borrowed.data(), columns.span().data());
    EXPECT_EQ(borrowed.column_index(ColumnIndex(9)), 1);
    EXPECT_EQ(columns.column_index(ColumnIndex(4)), 0);
    EXPECT_THROW(columns.column_index(ColumnIndex(8)), std::out_of_range);
    EXPECT_THROW(borrowed.column_index(ColumnIndex(8)), std::out_of_range);
    auto copied = columns;
    Builder<Columns> copied_view(borrowed);
    EXPECT_NE(copied.span().data(), borrowed.data());
    EXPECT_NE(copied_view.span().data(), borrowed.data());
    EXPECT_EQ(copied_view.column_index(ColumnIndex(9)), 1);
    const Builder<Columns> replacement { ColumnIndex(5), ColumnIndex(6) };
    columns.assign(replacement);
    EXPECT_EQ(copied.column_index(ColumnIndex(4)), 0);
    EXPECT_EQ(copied_view.column_index(ColumnIndex(4)), 0);
    EXPECT_THROW((Builder<Columns> { ColumnIndex(4), ColumnIndex(4) }), std::invalid_argument);
    const std::array<ColumnIndex, 2> duplicates { ColumnIndex(4), ColumnIndex(4) };
    const std::span<const ColumnIndex> duplicate_view { std::span(duplicates) };
    EXPECT_EQ(duplicate_view.data(), duplicates.data());
    EXPECT_THROW((Builder<Columns>(duplicate_view)), std::invalid_argument);
    const auto* storage = columns.span().data();
    EXPECT_THROW(columns.assign(duplicate_view), std::invalid_argument);
    EXPECT_EQ(columns.span().data(), storage);
    EXPECT_TRUE(std::ranges::equal(columns.span(), replacement.span()));
    EXPECT_THROW((Builder<Relation<>>(duplicate_view)), std::invalid_argument);
}

TEST(YggdrasilTests, DatabaseColumnsAssignmentRetainsStorageForSelfSubviews)
{
    Builder<Columns> columns { ColumnIndex(1), ColumnIndex(2), ColumnIndex(3), ColumnIndex(4) };
    const auto* storage = columns.span().data();
    const auto memory = columns.memory_usage();
    columns.assign(columns);
    EXPECT_EQ(columns.span().data(), storage);
    EXPECT_EQ(columns.size(), 4);
    columns.assign(columns.span().subspan(1, 2));
    EXPECT_EQ(columns.span()[0], ColumnIndex(2));
    EXPECT_EQ(columns.span()[1], ColumnIndex(3));
    EXPECT_EQ(columns.size(), 2);
    EXPECT_EQ(columns.span().data(), storage);
    EXPECT_EQ(columns.memory_usage(), memory);
    columns.assign(std::span<const ColumnIndex>());
    EXPECT_TRUE(columns.empty());
    EXPECT_EQ(columns.memory_usage(), memory);
    const Builder<Columns> replacement { ColumnIndex(5), ColumnIndex(6), ColumnIndex(7), ColumnIndex(8) };
    columns.assign(replacement);
    EXPECT_EQ(columns.span().data(), storage);
    EXPECT_EQ(columns.memory_usage(), memory);
    EXPECT_EQ(columns.column_index(ColumnIndex(8)), 3);
}

TEST(YggdrasilTests, DatabaseRelationRenamePreservesRowsAndStorage)
{
    Builder<Relation<>> relation { ColumnIndex(1), ColumnIndex(2) };
    relation.insert({ 10, 20 });
    relation.insert({ 30, 40 });
    const auto* columns = relation.columns().data();
    const auto* first_row = relation[0].data();
    const auto* second_row = relation[1].data();
    const auto memory = relation.memory_usage();
    {
        const Builder<Columns> labels { ColumnIndex(4), ColumnIndex(9) };
        relation.rename(labels);
        relation.rename(labels);
    }
    relation.rename(relation.columns());
    expect_relation(relation, { ColumnIndex(4), ColumnIndex(9) }, { { 10, 20 }, { 30, 40 } });
    EXPECT_EQ(relation.columns().data(), columns);
    EXPECT_EQ(relation[0].data(), first_row);
    EXPECT_EQ(relation[1].data(), second_row);
    EXPECT_EQ(relation.memory_usage(), memory);
    EXPECT_EQ(relation.insert({ 10, 20 }), 0);

    const Builder<Columns> wrong_arity { ColumnIndex(4) };
    EXPECT_THROW(relation.rename(wrong_arity), std::invalid_argument);
    expect_relation(relation, { ColumnIndex(4), ColumnIndex(9) }, { { 10, 20 }, { 30, 40 } });
    EXPECT_EQ(relation.columns().data(), columns);
    EXPECT_EQ(relation[0].data(), first_row);
    EXPECT_EQ(relation[1].data(), second_row);

    Builder<Relation<>> nullary;
    nullary.rename(std::span<const ColumnIndex>());
    EXPECT_TRUE(nullary.empty());
    nullary.insert({});
    nullary.rename(nullary.columns());
    expect_relation(nullary, {}, { {} });
}

TEST(YggdrasilTests, DatabaseRelationMaintainsSetAndSchemaInvariants)
{
    Builder<Relation<>> relation(Builder<Columns> { ColumnIndex(4), ColumnIndex(9) });
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
    EXPECT_THROW((Builder<Relation<>>({ ColumnIndex(4), ColumnIndex(4) })), std::invalid_argument);
    const auto& view = relation;
    EXPECT_EQ(view.column_index(ColumnIndex(9)), 1);
    EXPECT_THROW(view.column_index(ColumnIndex(5)), std::out_of_range);
    EXPECT_TRUE(view.contains({ 3, 4 }));
    EXPECT_THROW(view.at(2), std::out_of_range);
    relation.clear();
    expect_relation(relation, { ColumnIndex(4), ColumnIndex(9) }, {});
    EXPECT_EQ(relation.insert({ 5, 6 }), 0);
}

TEST(YggdrasilTests, DatabaseBuilderViewsShareSchemasAndObserveRefills)
{
    RelationRepositoryFactory<> factory;
    auto repository = factory.create();
    Builder<Relation<>> relation({ ColumnIndex(1), ColumnIndex(2) });
    relation.insert({ 7, 8 });
    const auto view = make_view(relation, repository);
    const auto copy = view;
    EXPECT_EQ(view.columns().data(), relation.columns().data());
    EXPECT_EQ(copy.columns().data(), relation.columns().data());
    const auto* first_row = view[0].data();
    const Builder<Columns> labels { ColumnIndex(3), ColumnIndex(4) };
    relation.rename(labels);
    EXPECT_EQ(view.column_index(ColumnIndex(4)), 1);
    EXPECT_EQ(copy.column_index(ColumnIndex(3)), 0);
    EXPECT_EQ(view[0].data(), first_row);
    relation.clear();
    EXPECT_TRUE(view.empty());
    relation.insert({ 9, 10 });
    EXPECT_TRUE(copy.contains({ 9, 10 }));
    auto projected = project<uint_t>(view, { ColumnIndex(4), ColumnIndex(3) });
    expect_relation(projected, { ColumnIndex(4), ColumnIndex(3) }, { { 10, 9 } });
    auto projected_columns = project<uint_t>(view, projected.columns());
    expect_relation(projected_columns, { ColumnIndex(4), ColumnIndex(3) }, { { 10, 9 } });
}

TEST(YggdrasilTests, DatabaseRelationReinitializationRetainsCompatibleStorage)
{
    Builder<Relation<>> relation({ ColumnIndex(1), ColumnIndex(2) });
    relation.insert({ 3, 4 });
    const auto memory = relation.memory_usage();
    EXPECT_THROW(relation.initialize({ ColumnIndex(5), ColumnIndex(5) }), std::invalid_argument);
    expect_relation(relation, { ColumnIndex(1), ColumnIndex(2) }, { { 3, 4 } });
    relation.initialize(relation.columns());
    EXPECT_EQ(relation.memory_usage(), memory);
    expect_relation(relation, { ColumnIndex(1), ColumnIndex(2) }, {});
    relation.insert({ 3, 4 });
    const Builder<Columns> validated { ColumnIndex(5), ColumnIndex(6) };
    relation.initialize(validated);
    EXPECT_EQ(relation.memory_usage(), memory);
    expect_relation(relation, { ColumnIndex(5), ColumnIndex(6) }, {});
    relation.initialize({ ColumnIndex(7) });
    relation.insert({ 9 });
    expect_relation(relation, { ColumnIndex(7) }, { { 9 } });
    relation.initialize({});
    relation.insert({});
    expect_relation(relation, {}, { {} });
}

TEST(YggdrasilTests, DatabasePoolKeepsLiveRelationsDistinctAndReusesByArity)
{
    RelationPool<> pool;
    auto first = pool.get_or_allocate({ ColumnIndex(1), ColumnIndex(2) });
    first->insert({ 3, 4 });
    auto second = pool.get_or_allocate({ ColumnIndex(1), ColumnIndex(2) });
    second->insert({ 5, 6 });
    EXPECT_NE(first.get(), second.get());
    const auto* released = second.get();
    const auto memory = second->memory_usage();
    second = {};
    auto unary = pool.get_or_allocate({ ColumnIndex(1) });
    unary->insert({ 8 });
    const Builder<Columns> validated { ColumnIndex(7), ColumnIndex(8) };
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
    boolean->insert({});
    expect_relation(*boolean, {}, { {} });
}

TEST(YggdrasilTests, DatabaseProjectionReordersDeduplicatesAndOwnsResults)
{
    Builder<Relation<>> relation({ ColumnIndex(1), ColumnIndex(2), ColumnIndex(3) });
    relation.insert({ 7, 8, 1 });
    relation.insert({ 7, 8, 2 });
    relation.insert({ 9, 8, 3 });
    auto projected = project<uint_t>(relation, { ColumnIndex(2), ColumnIndex(1) });
    expect_relation(projected, { ColumnIndex(2), ColumnIndex(1) }, { { 8, 7 }, { 8, 9 } });
    EXPECT_THROW(project<uint_t>(relation, { ColumnIndex(1), ColumnIndex(1) }), std::invalid_argument);
    EXPECT_THROW(project<uint_t>(relation, { ColumnIndex(4) }), std::out_of_range);
    Builder<Relation<>> output({ ColumnIndex(2), ColumnIndex(1) });
    output.insert({ 99, 99 });
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
    Builder<Columns> columns { std::span<const ColumnIndex>(labels) };
    Builder<Relation<>> relation(labels);
    relation.insert({ 7, 8, 9 });
    labels.fill(ColumnIndex(0));
    EXPECT_EQ(columns.column_index(ColumnIndex(3)), 2);
    expect_relation(relation, { ColumnIndex(1), ColumnIndex(2), ColumnIndex(3) }, { { 7, 8, 9 } });

    std::array<ColumnIndex, 2> output_labels { ColumnIndex(3), ColumnIndex(1) };
    auto from_span = project<uint_t>(relation, std::span(output_labels));
    auto from_array = project<uint_t>(relation, output_labels);
    auto from_vector = project<uint_t>(relation, std::vector<ColumnIndex> { ColumnIndex(3), ColumnIndex(1) });
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
        auto projection = ProjectionPlan(lhs, { ColumnIndex(2), large });
        auto join = JoinPlan(lhs, rhs);
        lhs.fill(ColumnIndex(0));
        rhs.fill(ColumnIndex(0));
        return std::pair(std::move(projection), std::move(join));
    }();
    auto copied = plans;
    auto [projection, joining] = std::move(copied);
    Builder<Relation<>> left({ large, ColumnIndex(1), ColumnIndex(2) });
    Builder<Relation<>> right({ ColumnIndex(2), ColumnIndex(3), large });
    Builder<Relation<>> projected({ ColumnIndex(2), large });
    Builder<Relation<>> joined({ large, ColumnIndex(1), ColumnIndex(2), ColumnIndex(3) });
    Workspace<> workspace;
    for (uint_t generation = 0; generation < 3; ++generation)
    {
        left.clear();
        right.clear();
        left.insert({ 7 + generation, 8, 9 });
        left.insert({ 7 + generation, 10, 9 });
        right.insert({ 9, 11, 7 + generation });
        project(left, projection, projected, workspace);
        expect_relation(projected, { ColumnIndex(2), large }, { { 9, 7 + generation } });
        join(left, right, joining, joined, workspace);
        expect_relation(joined, { large, ColumnIndex(1), ColumnIndex(2), ColumnIndex(3) }, { { 7 + generation, 8, 9, 11 }, { 7 + generation, 10, 9, 11 } });
    }
    const ProjectionPlan guard(left.columns(), {});
    Builder<Relation<>> exists;
    project(left, guard, exists, workspace);
    expect_relation(exists, {}, { {} });
    left.clear();
    project(left, guard, exists, workspace);
    expect_relation(exists, {}, {});
}

TEST(YggdrasilTests, DatabaseColumnsAndPlansSurviveCistaRelocation)
{
    // The owning objects and original byte buffers are gone before decoding.
    auto columns_bytes = relocated_serialization(Builder<Columns> { ColumnIndex(3), ColumnIndex(1), ColumnIndex(2) });
    auto projection_bytes =
        relocated_serialization(ProjectionPlan({ ColumnIndex(3), ColumnIndex(1), ColumnIndex(2), ColumnIndex(4) }, { ColumnIndex(4), ColumnIndex(3) }));
    auto join_bytes = relocated_serialization(JoinPlan({ ColumnIndex(3), ColumnIndex(1), ColumnIndex(2) }, { ColumnIndex(2), ColumnIndex(4), ColumnIndex(1) }));
    const auto* columns = cista::deserialize<Builder<Columns>>(columns_bytes);
    const auto* projection = cista::deserialize<ProjectionPlan>(projection_bytes);
    const auto* joining = cista::deserialize<JoinPlan>(join_bytes);
    const auto values = [](auto range) { return std::vector(range.begin(), range.end()); };
    EXPECT_EQ(values(columns->span()), (std::vector<ColumnIndex> { ColumnIndex(3), ColumnIndex(1), ColumnIndex(2) }));
    EXPECT_EQ(columns->column_index(ColumnIndex(1)), 1);
    EXPECT_EQ(values(projection->input_columns()), (std::vector<ColumnIndex> { ColumnIndex(3), ColumnIndex(1), ColumnIndex(2), ColumnIndex(4) }));
    EXPECT_EQ(values(projection->output_columns()), (std::vector<ColumnIndex> { ColumnIndex(4), ColumnIndex(3) }));
    EXPECT_EQ(values(projection->positions()), (std::vector<size_t> { 3, 0 }));
    EXPECT_EQ(values(joining->lhs_columns()), (std::vector<ColumnIndex> { ColumnIndex(3), ColumnIndex(1), ColumnIndex(2) }));
    EXPECT_EQ(values(joining->rhs_columns()), (std::vector<ColumnIndex> { ColumnIndex(2), ColumnIndex(4), ColumnIndex(1) }));
    EXPECT_EQ(values(joining->output_columns()), (std::vector<ColumnIndex> { ColumnIndex(3), ColumnIndex(1), ColumnIndex(2), ColumnIndex(4) }));
    EXPECT_EQ(values(joining->lhs_keys()), (std::vector<size_t> { 2, 1 }));
    EXPECT_EQ(values(joining->rhs_keys()), (std::vector<size_t> { 0, 2 }));
    EXPECT_EQ(values(joining->rhs_payload()), (std::vector<size_t> { 1 }));

    // Keep the relocated buffers unchanged and alive while using decoded plans.
    Builder<Relation<>> left(columns->span());
    Builder<Relation<>> right(joining->rhs_columns());
    left.insert({ 10, 1, 2 });
    left.insert({ 11, 1, 3 });
    right.insert({ 2, 20, 1 });
    right.insert({ 3, 30, 1 });
    right.insert({ 2, 99, 9 });
    Builder<Relation<>> joined(joining->output_columns());
    Builder<Relation<>> projected(projection->output_columns());
    Workspace<> workspace;
    join(left, right, *joining, joined, workspace);
    expect_relation(joined, { ColumnIndex(3), ColumnIndex(1), ColumnIndex(2), ColumnIndex(4) }, { { 10, 1, 2, 20 }, { 11, 1, 3, 30 } });
    project(joined, *projection, projected, workspace);
    expect_relation(projected, { ColumnIndex(4), ColumnIndex(3) }, { { 20, 10 }, { 30, 11 } });
}

TEST(YggdrasilTests, DatabaseDefaultPlansRoundTripAndEvaluateNullaryRelations)
{
    auto columns_bytes = relocated_serialization(Builder<Columns>());
    auto projection_bytes = relocated_serialization(ProjectionPlan());
    auto join_bytes = relocated_serialization(JoinPlan());
    const auto* columns = cista::deserialize<Builder<Columns>>(columns_bytes);
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

    Builder<Relation<>> left, right, projected, joined;
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
    Builder<Relation<>> left({ ColumnIndex(1), ColumnIndex(2) });
    Builder<Relation<>> right({ ColumnIndex(2), ColumnIndex(3) });
    const ProjectionPlan projection(left.columns(), { ColumnIndex(1) });
    const JoinPlan joining(left.columns(), right.columns());
    Builder<Relation<>> projected({ ColumnIndex(1) });
    projected.insert({ 99 });
    Builder<Relation<>> joined({ ColumnIndex(1), ColumnIndex(2), ColumnIndex(3) });
    joined.insert({ 99, 99, 99 });
    Workspace<> workspace;
    JoinIndexCache<> cache;
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
    left.insert({ 7, 8 });
    right.insert({ 8, 9 });
    projected.initialize({ ColumnIndex(2) });
    projected.insert({ 88 });
    joined.initialize({ ColumnIndex(1), ColumnIndex(3), ColumnIndex(2) });
    joined.insert({ 88, 88, 88 });
    EXPECT_THROW(project(left, projection, projected, workspace), std::invalid_argument);
    EXPECT_THROW(join(left, right, joining, joined, workspace), std::invalid_argument);
    EXPECT_THROW(join(left, right, joining, cache, reuse, joined, workspace), std::invalid_argument);
    expect_relation(projected, { ColumnIndex(2) }, { { 88 } });
    expect_relation(joined, { ColumnIndex(1), ColumnIndex(3), ColumnIndex(2) }, { { 88, 88, 88 } });

    const ProjectionPlan identity(left.columns(), left.columns());
    const JoinPlan same(left.columns(), left.columns());
    EXPECT_THROW(project(left, identity, left, workspace), std::invalid_argument);
    EXPECT_THROW(join(left, left, same, left, workspace), std::invalid_argument);
    EXPECT_THROW(join(left, left, same, cache, reuse, left, workspace), std::invalid_argument);
    expect_relation(left, { ColumnIndex(1), ColumnIndex(2) }, { { 7, 8 } });
    EXPECT_EQ(cache.size(), 0);

    const std::array<ColumnIndex, 2> duplicates { ColumnIndex(1), ColumnIndex(1) };
    const std::span<const ColumnIndex> duplicate_view { std::span(duplicates) };
    EXPECT_THROW((ProjectionPlan(duplicate_view, left.columns())), std::invalid_argument);
    EXPECT_THROW((ProjectionPlan(left.columns(), duplicate_view)), std::invalid_argument);
    EXPECT_THROW((ProjectionPlan({ ColumnIndex(1) }, { ColumnIndex(2) })), std::out_of_range);
    EXPECT_THROW((JoinPlan(duplicate_view, right.columns())), std::invalid_argument);
    EXPECT_THROW((JoinPlan(left.columns(), duplicate_view)), std::invalid_argument);
}

TEST(YggdrasilTests, DatabaseSelectionsSupportColumnsConstantsAndPredicates)
{
    Builder<Relation<>> relation({ ColumnIndex(1), ColumnIndex(2) });
    relation.insert({ 1, 1 });
    relation.insert({ 1, 2 });
    relation.insert({ 2, 2 });
    expect_relation(select_equal_columns<uint_t>(relation, ColumnIndex(1), ColumnIndex(2)), { ColumnIndex(1), ColumnIndex(2) }, { { 1, 1 }, { 2, 2 } });
    expect_relation(select_equal_value<uint_t>(relation, ColumnIndex(1), uint_t(1)), { ColumnIndex(1), ColumnIndex(2) }, { { 1, 1 }, { 1, 2 } });
    const auto less = [](std::span<const uint_t> row) { return row[0] < row[1]; };
    expect_relation(select<uint_t>(relation, less), { ColumnIndex(1), ColumnIndex(2) }, { { 1, 2 } });
    Builder<Relation<>> output({ ColumnIndex(1), ColumnIndex(2) });
    output.insert({ 99, 99 });
    select_equal_columns(relation, ColumnIndex(1), ColumnIndex(2), output);
    expect_relation(output, { ColumnIndex(1), ColumnIndex(2) }, { { 1, 1 }, { 2, 2 } });
    select_equal_value(relation, ColumnIndex(2), uint_t(2), output);
    expect_relation(output, { ColumnIndex(1), ColumnIndex(2) }, { { 1, 2 }, { 2, 2 } });
    select(relation, less, output);
    expect_relation(output, { ColumnIndex(1), ColumnIndex(2) }, { { 1, 2 } });
    EXPECT_THROW(select_equal_columns<uint_t>(relation, ColumnIndex(1), ColumnIndex(3)), std::out_of_range);
    EXPECT_THROW(select_equal_value<uint_t>(relation, ColumnIndex(3), uint_t(1)), std::out_of_range);
}

TEST(YggdrasilTests, DatabaseJoinPreservesAllMatchingRowsAndLogicalColumnOrder)
{
    Builder<Relation<>> left({ ColumnIndex(3), ColumnIndex(1), ColumnIndex(2) });
    Builder<Relation<>> right({ ColumnIndex(2), ColumnIndex(4), ColumnIndex(1) });
    left.insert({ 10, 1, 2 });
    left.insert({ 11, 1, 2 });
    left.insert({ 12, 1, 3 });
    right.insert({ 2, 20, 1 });
    right.insert({ 2, 21, 1 });
    right.insert({ 2, 22, 9 });
    right.insert({ 3, 23, 1 });
    right.insert({ 8, 24, 8 });
    expect_relation(join<uint_t>(left, right),
                    { ColumnIndex(3), ColumnIndex(1), ColumnIndex(2), ColumnIndex(4) },
                    { { 10, 1, 2, 20 }, { 10, 1, 2, 21 }, { 11, 1, 2, 20 }, { 11, 1, 2, 21 }, { 12, 1, 3, 23 } });
    expect_relation(join<uint_t>(right, left),
                    { ColumnIndex(2), ColumnIndex(4), ColumnIndex(1), ColumnIndex(3) },
                    { { 2, 20, 1, 10 }, { 2, 20, 1, 11 }, { 2, 21, 1, 10 }, { 2, 21, 1, 11 }, { 3, 23, 1, 12 } });
    Builder<Relation<>> output({ ColumnIndex(3), ColumnIndex(1), ColumnIndex(2), ColumnIndex(4) });
    output.insert({ 99, 99, 99, 99 });
    join(left, right, output);
    EXPECT_EQ(output.size(), 5);
    right.clear();
    join(left, right, output);
    expect_relation(output, { ColumnIndex(3), ColumnIndex(1), ColumnIndex(2), ColumnIndex(4) }, {});
}

TEST(YggdrasilTests, DatabasePoolFactoryIdentifiesStorageAcrossPoolsAndRenames)
{
    RelationPoolFactory<> factory;
    auto copy = factory;
    auto first_pool = factory.create_pool();
    auto second_pool = copy.create_pool();
    auto first = first_pool.get_or_allocate({ ColumnIndex(1) });
    auto second = second_pool.get_or_allocate({ ColumnIndex(1) });
    EXPECT_NE(first->get_storage_index(), second->get_storage_index());
    first->insert({ 4 });
    second->insert({ 5 });
    JoinIndexCache<> cache;
    const std::array<size_t, 1> keys { 0 };
    cache.get_or_create(*first, keys);
    cache.get_or_create(*second, keys);
    EXPECT_EQ(cache.size(), 2);
    const Builder<Columns> labels { ColumnIndex(2) };
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
    reused->insert({ 8 });
    cache.get_or_create(*reused, keys);
    EXPECT_EQ(cache.size(), 1);
    auto pair = first_pool.get_or_allocate({ ColumnIndex(1), ColumnIndex(2) });
    EXPECT_NE(pair->get_storage_index(), index);
}

TEST(YggdrasilTests, DatabaseJoinIndexRejectsUnidentifiedStorage)
{
    Builder<Relation<>> left({ ColumnIndex(1) });
    Builder<Relation<>> right({ ColumnIndex(1) });
    left.insert({ 1 });
    right.insert({ 1 });
    const std::array<size_t, 1> keys { 0 };
    EXPECT_THROW((JoinIndex<>(left, keys)), std::invalid_argument);
    JoinIndexCache<> cache;
    EXPECT_THROW(cache.get_or_create(left, keys), std::invalid_argument);
    EXPECT_EQ(cache.size(), 0);
    Workspace<> workspace;
    const JoinPlan plan(left.columns(), right.columns());
    Builder<Relation<>> result({ ColumnIndex(1) });
    result.insert({ 99 });
    EXPECT_THROW(join(left, right, plan, cache, JoinReuse { true, false }, result, workspace), std::invalid_argument);
    expect_relation(result, { ColumnIndex(1) }, { { 99 } });
    // Uncached operations continue to accept directly constructed relations.
    join(left, right, plan, result, workspace);
    expect_relation(result, { ColumnIndex(1) }, { { 1 } });
}

TEST(YggdrasilTests, DatabaseJoinIndexReusesImmutableRowsWithChangingProbes)
{
    RelationPool<> pool;
    auto build = pool.get_or_allocate({ ColumnIndex(3), ColumnIndex(1), ColumnIndex(2) });
    Builder<Relation<>> probe({ ColumnIndex(2), ColumnIndex(4), ColumnIndex(1) });
    build->insert({ 10, 1, 2 });
    build->insert({ 11, 1, 2 });
    build->insert({ 12, 1, 3 });
    const JoinPlan left_plan(build->columns(), probe.columns());
    const JoinPlan right_plan(probe.columns(), build->columns());
    const JoinIndex<> left_index(*build, left_plan.lhs_keys());
    const JoinIndex<> right_index(*build, right_plan.rhs_keys());
    Builder<Relation<>> left_result(left_plan.output_columns());
    Builder<Relation<>> right_result(right_plan.output_columns());
    Workspace<> workspace;
    JoinIndexCache<> cache;
    for (uint_t generation = 0; generation < 3; ++generation)
    {
        probe.clear();
        probe.insert({ 2, 20 + generation, 1 });
        probe.insert({ 3, 30 + generation, 1 });
        probe.insert({ 2, 40 + generation, 9 });
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
    RelationPool<> pool;
    auto build = pool.get_or_allocate({ ColumnIndex(1), ColumnIndex(2), ColumnIndex(3) });
    Builder<Relation<>> probe({ ColumnIndex(1), ColumnIndex(2), ColumnIndex(4) });
    build->insert({ 7, 8, 90 });
    build->insert({ 8, 7, 91 });
    probe.insert({ 7, 8, 10 });
    const JoinPlan plan(build->columns(), probe.columns());
    const JoinPlan reversed(probe.columns(), build->columns());
    Builder<Relation<>> result(plan.output_columns());
    Builder<Relation<>> reverse_result(reversed.output_columns());
    Workspace<> workspace;
    JoinIndexCache<> cache;

    join(*build, probe, plan, cache, JoinReuse { true, false }, result, workspace);
    expect_relation(result, { ColumnIndex(1), ColumnIndex(2), ColumnIndex(3), ColumnIndex(4) }, { { 7, 8, 90, 10 } });
    EXPECT_EQ(cache.size(), 1);
    const std::array<size_t, 2> keys { 0, 1 };
    EXPECT_EQ(&cache.get_or_create(*build, plan.lhs_keys()), &cache.get_or_create(*build, keys));
    join(probe, *build, reversed, cache, JoinReuse { false, true }, reverse_result, workspace);
    expect_relation(reverse_result, { ColumnIndex(1), ColumnIndex(2), ColumnIndex(4), ColumnIndex(3) }, { { 7, 8, 10, 90 } });
    EXPECT_EQ(cache.size(), 1);

    const Builder<Columns> build_labels { ColumnIndex(11), ColumnIndex(12), ColumnIndex(13) };
    const Builder<Columns> probe_labels { ColumnIndex(11), ColumnIndex(12), ColumnIndex(14) };
    build->rename(build_labels);
    probe.rename(probe_labels);
    const JoinPlan renamed_plan(build->columns(), probe.columns());
    result.initialize(renamed_plan.output_columns());
    join(*build, probe, renamed_plan, cache, JoinReuse { true, false }, result, workspace);
    expect_relation(result, { ColumnIndex(11), ColumnIndex(12), ColumnIndex(13), ColumnIndex(14) }, { { 7, 8, 90, 10 } });
    EXPECT_EQ(cache.size(), 1);

    build->rename(plan.lhs_columns());
    const Builder<Columns> reordered_labels { ColumnIndex(2), ColumnIndex(1), ColumnIndex(4) };
    probe.rename(reordered_labels);
    const JoinPlan reordered_plan(build->columns(), probe.columns());
    result.initialize(reordered_plan.output_columns());
    join(*build, probe, reordered_plan, cache, JoinReuse { true, false }, result, workspace);
    expect_relation(result, { ColumnIndex(1), ColumnIndex(2), ColumnIndex(3), ColumnIndex(4) }, { { 8, 7, 91, 10 } });
    EXPECT_EQ(cache.size(), 2);

    probe.rename(plan.rhs_columns());
    auto other = pool.get_or_allocate(build->columns());
    other->insert({ 7, 8, 92 });
    join(*other, probe, plan, cache, JoinReuse { true, false }, result, workspace);
    expect_relation(result, { ColumnIndex(1), ColumnIndex(2), ColumnIndex(3), ColumnIndex(4) }, { { 7, 8, 92, 10 } });
    EXPECT_EQ(cache.size(), 3);

    cache.clear();
    EXPECT_EQ(cache.size(), 0);
    build->clear();
    build->insert({ 7, 8, 93 });
    join(*build, probe, plan, cache, JoinReuse { true, false }, result, workspace);
    expect_relation(result, { ColumnIndex(1), ColumnIndex(2), ColumnIndex(3), ColumnIndex(4) }, { { 7, 8, 93, 10 } });
    EXPECT_EQ(cache.size(), 1);
}

TEST(YggdrasilTests, DatabaseJoinIndexCacheSkipsEmptyCartesianAndNonreusableInputs)
{
    Builder<Relation<>> left({ ColumnIndex(1) });
    Builder<Relation<>> right({ ColumnIndex(1) });
    right.insert({ 7 });
    const JoinPlan plan(left.columns(), right.columns());
    Builder<Relation<>> result(plan.output_columns());
    Workspace<> workspace;
    JoinIndexCache<> cache;
    for (const auto reuse : { JoinReuse {}, JoinReuse { true, false }, JoinReuse { false, true }, JoinReuse { true, true } })
    {
        result.insert({ 99 });
        join(left, right, plan, cache, reuse, result, workspace);
        EXPECT_TRUE(result.empty());
        result.insert({ 99 });
        join(right, left, plan, cache, reuse, result, workspace);
        EXPECT_TRUE(result.empty());
        EXPECT_EQ(cache.size(), 0);
    }
    left.insert({ 7 });
    join(left, right, plan, cache, JoinReuse {}, result, workspace);
    expect_relation(result, { ColumnIndex(1) }, { { 7 } });
    EXPECT_EQ(cache.size(), 0);

    const Builder<Columns> labels { ColumnIndex(2) };
    right.rename(labels);
    const JoinPlan cartesian(left.columns(), right.columns());
    result.initialize(cartesian.output_columns());
    join(left, right, cartesian, cache, JoinReuse { true, true }, result, workspace);
    expect_relation(result, { ColumnIndex(1), ColumnIndex(2) }, { { 7, 7 } });
    EXPECT_EQ(cache.size(), 0);
}

TEST(YggdrasilTests, DatabaseJoinIndexRejectsWrongStorageAndKeysBeforeClearingOutput)
{
    RelationPool<> pool;
    auto left = pool.get_or_allocate({ ColumnIndex(1), ColumnIndex(2) });
    auto right = pool.get_or_allocate({ ColumnIndex(2), ColumnIndex(3) });
    auto other = pool.get_or_allocate({ ColumnIndex(1), ColumnIndex(2) });
    const JoinPlan plan(left->columns(), right->columns());
    const JoinIndex<> unrelated(*other, plan.lhs_keys());
    const std::array<size_t, 1> wrong_position { 0 };
    const JoinIndex<> wrong_keys(*left, wrong_position);
    const std::array<size_t, 1> out_of_bounds { 2 };
    EXPECT_THROW((JoinIndex<>(*left, out_of_bounds)), std::out_of_range);
    Builder<Relation<>> result(plan.output_columns());
    result.insert({ 7, 8, 9 });
    Workspace<> workspace;
    EXPECT_THROW(join(*left, *right, plan, unrelated, result, workspace), std::invalid_argument);
    EXPECT_THROW(join(*left, *right, plan, wrong_keys, result, workspace), std::invalid_argument);
    expect_relation(result, { ColumnIndex(1), ColumnIndex(2), ColumnIndex(3) }, { { 7, 8, 9 } });

    const JoinPlan same(left->columns(), left->columns());
    const JoinIndex<> same_index(*left, same.lhs_keys());
    EXPECT_THROW(join(*left, *left, same, same_index, *left, workspace), std::invalid_argument);
}

TEST(YggdrasilTests, DatabaseJoinIndexDoesNotRehashBuildRowsWhenProbing)
{
    using Value = DatabaseCountedValue;
    RelationPool<Value> pool;
    auto build = pool.get_or_allocate({ ColumnIndex(1), ColumnIndex(2) });
    Builder<Relation<Value>> probe({ ColumnIndex(1) });
    for (uint_t i = 0; i < 2048; ++i)
        build->insert({ Value { i }, Value { i + 1 } });
    const JoinPlan plan(probe.columns(), build->columns());
    const JoinIndex<Value> index(*build, plan.rhs_keys());
    JoinIndexCache<Value> cache;
    cache.get_or_create(*build, plan.rhs_keys());
    Builder<Relation<Value>> result(plan.output_columns());
    Workspace<Value> workspace;
    for (uint_t key : { 5, 1000, 1500 })
    {
        probe.clear();
        probe.insert({ Value { key } });
        for (const bool cached : { false, true })
        {
            Value::hashes = 0;
            if (cached)
                join(probe, *build, plan, cache, JoinReuse { false, true }, result, workspace);
            else
                join(probe, *build, plan, index, result, workspace);
            EXPECT_LT(Value::hashes, 16);
            ASSERT_EQ(result.size(), 1);
            EXPECT_EQ(result[0][1].value, key + 1);
        }
    }
}

TEST(YggdrasilTests, DatabaseJoinHandlesCartesianProductsAndAllSharedColumns)
{
    Builder<Relation<>> left({ ColumnIndex(1) });
    Builder<Relation<>> right({ ColumnIndex(2) });
    left.insert({ 3 });
    left.insert({ 4 });
    right.insert({ 5 });
    right.insert({ 6 });
    expect_relation(join<uint_t>(left, right), { ColumnIndex(1), ColumnIndex(2) }, { { 3, 5 }, { 3, 6 }, { 4, 5 }, { 4, 6 } });
    auto pairs = join<uint_t>(left, right);
    Builder<Relation<>> reversed({ ColumnIndex(2), ColumnIndex(1) });
    reversed.insert({ 5, 3 });
    reversed.insert({ 8, 3 });
    expect_relation(join<uint_t>(pairs, reversed), { ColumnIndex(1), ColumnIndex(2) }, { { 3, 5 } });
}

TEST(YggdrasilTests, DatabaseSetOperationsRequireAlignedSignatures)
{
    Builder<Relation<>> left({ ColumnIndex(1), ColumnIndex(2) });
    Builder<Relation<>> right({ ColumnIndex(1), ColumnIndex(2) });
    left.insert({ 1, 2 });
    left.insert({ 3, 4 });
    right.insert({ 3, 4 });
    right.insert({ 5, 6 });
    expect_relation(union_<uint_t>(left, right), { ColumnIndex(1), ColumnIndex(2) }, { { 1, 2 }, { 3, 4 }, { 5, 6 } });
    expect_relation(difference<uint_t>(left, right), { ColumnIndex(1), ColumnIndex(2) }, { { 1, 2 } });
    expect_relation(difference<uint_t>(left, left), { ColumnIndex(1), ColumnIndex(2) }, {});
    Builder<Relation<>> output({ ColumnIndex(1), ColumnIndex(2) });
    output.insert({ 99, 99 });
    union_(left, right, output);
    EXPECT_EQ(output.size(), 3);
    difference(left, right, output);
    expect_relation(output, { ColumnIndex(1), ColumnIndex(2) }, { { 1, 2 } });
    auto reversed = project<uint_t>(right, { ColumnIndex(2), ColumnIndex(1) });
    EXPECT_THROW(union_<uint_t>(left, reversed), std::invalid_argument);
    EXPECT_THROW(difference<uint_t>(left, reversed), std::invalid_argument);
    auto aligned = project<uint_t>(reversed, { ColumnIndex(1), ColumnIndex(2) });
    expect_relation(difference<uint_t>(left, aligned), { ColumnIndex(1), ColumnIndex(2) }, { { 1, 2 } });
}

TEST(YggdrasilTests, DatabaseNullaryRelationsObeyBooleanLaws)
{
    Builder<Relation<>> false_;
    Builder<Relation<>> true_;
    EXPECT_EQ(true_.insert({}), 0);
    EXPECT_EQ(true_.insert({}), 0);
    EXPECT_EQ(true_.size(), 1);
    EXPECT_EQ(true_.arity(), 0);
    EXPECT_TRUE(true_.contains({}));
    EXPECT_FALSE(false_.contains({}));
    expect_relation(join<uint_t>(true_, true_), {}, { {} });
    expect_relation(join<uint_t>(true_, false_), {}, {});
    expect_relation(union_<uint_t>(true_, false_), {}, { {} });
    expect_relation(difference<uint_t>(true_, false_), {}, { {} });
    expect_relation(difference<uint_t>(true_, true_), {}, {});
    Builder<Relation<>> objects({ ColumnIndex(1) });
    objects.insert({ 7 });
    expect_relation(join<uint_t>(objects, true_), { ColumnIndex(1) }, { { 7 } });
    expect_relation(join<uint_t>(true_, objects), { ColumnIndex(1) }, { { 7 } });
    expect_relation(join<uint_t>(false_, objects), { ColumnIndex(1) }, {});
    expect_relation(project<uint_t>(objects, {}), {}, { {} });
    Builder<Relation<>> output;
    project(objects, {}, output);
    expect_relation(output, {}, { {} });
    objects.clear();
    project(objects, {}, output);
    expect_relation(output, {}, {});
    expect_relation(select<uint_t>(true_, [](std::span<const uint_t> row) { return row.empty(); }), {}, { {} });
    expect_relation(select<uint_t>(true_, [](std::span<const uint_t>) { return false; }), {}, {});
}

TEST(YggdrasilTests, DatabaseOutputGuardsPreserveExistingResults)
{
    Builder<Relation<>> input({ ColumnIndex(1), ColumnIndex(2) });
    input.insert({ 3, 4 });
    Builder<Relation<>> wrong({ ColumnIndex(2), ColumnIndex(1) });
    wrong.insert({ 8, 9 });
    const auto keep = [](std::span<const uint_t>) { return true; };
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
    Builder<Relation<>> rhs_only({ ColumnIndex(1), ColumnIndex(2) });
    rhs_only.insert({ 9, 10 });
    EXPECT_THROW(join(input, rhs_only, rhs_only), std::invalid_argument);
    EXPECT_THROW(union_(input, rhs_only, rhs_only), std::invalid_argument);
    EXPECT_THROW(difference(input, rhs_only, rhs_only), std::invalid_argument);
    expect_relation(rhs_only, { ColumnIndex(1), ColumnIndex(2) }, { { 9, 10 } });
    RelationRepositoryFactory<> factory;
    auto repository = factory.create();
    const auto alias = make_view(input, repository);
    EXPECT_THROW(select(alias, keep, input), std::invalid_argument);
    expect_relation(input, { ColumnIndex(1), ColumnIndex(2) }, { { 3, 4 } });
    Builder<Relation<>> output({ ColumnIndex(1), ColumnIndex(2) });
    output.insert({ 8, 9 });
    EXPECT_THROW(project(input, { ColumnIndex(1), ColumnIndex(9) }, output), std::out_of_range);
    EXPECT_THROW(select_equal_columns(input, ColumnIndex(1), ColumnIndex(9), output), std::out_of_range);
    EXPECT_THROW(select_equal_value(input, ColumnIndex(9), uint_t(3), output), std::out_of_range);
    EXPECT_THROW(union_(input, wrong, output), std::invalid_argument);
    EXPECT_THROW(difference(input, wrong, output), std::invalid_argument);
    expect_relation(output, { ColumnIndex(1), ColumnIndex(2) }, { { 8, 9 } });
}

TEST(YggdrasilTests, DatabaseOperatorsRespectCustomEqualityDespiteHashCollisions)
{
    using Value = DatabaseCollisionValue;
    RelationPool<Value> pool;
    auto left = pool.get_or_allocate({ ColumnIndex(1), ColumnIndex(2) });
    auto right = pool.get_or_allocate({ ColumnIndex(1), ColumnIndex(3) });
    left->insert({ Value { 1 }, Value { 2 } });
    left->insert({ Value { 3 }, Value { 4 } });
    EXPECT_EQ(left->insert({ Value { 11 }, Value { 12 } }), 0);
    right->insert({ Value { 11 }, Value { 5 } });
    right->insert({ Value { 6 }, Value { 7 } });
    auto joined = join<Value>(*left, *right);
    EXPECT_EQ(joined.size(), 1);
    EXPECT_TRUE(joined.contains({ Value { 1 }, Value { 2 }, Value { 5 } }));
    const JoinPlan plan(left->columns(), right->columns());
    const JoinIndex<Value> left_index(*left, plan.lhs_keys());
    const JoinIndex<Value> right_index(*right, plan.rhs_keys());
    Workspace<Value> workspace;
    for (const auto* index : { &left_index, &right_index })
    {
        join(*left, *right, plan, *index, joined, workspace);
        EXPECT_EQ(joined.size(), 1);
        EXPECT_TRUE(joined.contains({ Value { 1 }, Value { 2 }, Value { 5 } }));
    }
    auto selected = select_equal_value<Value>(*left, ColumnIndex(1), Value { 11 });
    EXPECT_EQ(selected.size(), 1);
    EXPECT_TRUE(selected.contains({ Value { 1 }, Value { 2 } }));
    Builder<Relation<Value>> pairs({ ColumnIndex(1), ColumnIndex(2) });
    pairs.insert({ Value { 1 }, Value { 11 } });
    pairs.insert({ Value { 2 }, Value { 3 } });
    auto equal = select_equal_columns<Value>(pairs, ColumnIndex(1), ColumnIndex(2));
    EXPECT_EQ(equal.size(), 1);
    EXPECT_TRUE(equal.contains({ Value { 1 }, Value { 11 } }));
}

TEST(YggdrasilTests, DatabaseKeyedJoinDoesNotCompareEveryPairOfRows)
{
    using Value = DatabaseCountedValue;
    constexpr uint_t count = 2048;
    Builder<Relation<Value>> left({ ColumnIndex(1), ColumnIndex(2) });
    Builder<Relation<Value>> right({ ColumnIndex(1), ColumnIndex(3) });
    for (uint_t i = 0; i < count; ++i)
    {
        left.insert({ Value { i }, Value { i + 1 } });
        right.insert({ Value { i }, Value { i + 2 } });
    }
    Value::comparisons = 0;
    auto result = join<Value>(left, right);
    EXPECT_EQ(result.size(), count);
    // Generous collision allowance, but far below the quadratic cross product.
    EXPECT_LT(Value::comparisons, size_t(count) * 64);
}

TEST(YggdrasilTests, DatabaseNaturalJoinMatchesSmallReferenceAcrossSchemas)
{
    RelationPool<> pool;
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
    Workspace<> workspace;
    for (const auto& [left_columns, right_columns] : schemas)
    {
        const JoinPlan join_plan(left_columns, right_columns);
        const ProjectionPlan project_plan(join_plan.output_columns(), left_columns);
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
                    relation->insert(row);
                }
            }
            const auto expected = reference_join(*left, *right);
            auto result = join<uint_t>(*left, *right);
            ASSERT_EQ(result.size(), expected.size());
            for (const auto& row : expected)
                EXPECT_TRUE(result.contains(row));
            join(*left, *right, join_plan, result, workspace);
            ASSERT_EQ(result.size(), expected.size());
            for (const auto& row : expected)
                EXPECT_TRUE(result.contains(row));
            const JoinIndex<> left_index(*left, join_plan.lhs_keys());
            const JoinIndex<> right_index(*right, join_plan.rhs_keys());
            for (const auto* index : { &left_index, &right_index })
            {
                join(*left, *right, join_plan, *index, result, workspace);
                ASSERT_EQ(result.size(), expected.size());
                for (const auto& row : expected)
                    EXPECT_TRUE(result.contains(row));
            }
            JoinIndexCache<> cache;
            for (const auto reuse : { JoinReuse {}, JoinReuse { true, false }, JoinReuse { false, true }, JoinReuse { true, true } })
            {
                join(*left, *right, join_plan, cache, reuse, result, workspace);
                ASSERT_EQ(result.size(), expected.size());
                for (const auto& row : expected)
                    EXPECT_TRUE(result.contains(row));
            }
            Builder<Relation<>> projected(left_columns);
            project(result, project_plan, projected, workspace);
            std::set<std::vector<uint_t>> expected_projection;
            for (const auto& row : expected)
                expected_projection.emplace(row.begin(), row.begin() + left_columns.size());
            ASSERT_EQ(projected.size(), expected_projection.size());
            for (const auto& row : expected_projection)
                EXPECT_TRUE(projected.contains(row));
            std::vector<ColumnIndex> expected_columns = left_columns;
            for (const auto column : right_columns)
            {
                bool shared = false;
                for (const auto left_column : left_columns)
                    shared = shared || column == left_column;
                if (!shared)
                    expected_columns.push_back(column);
            }
            EXPECT_EQ(std::vector<ColumnIndex>(result.columns().begin(), result.columns().end()), expected_columns);
            // Reuse one workspace across changing cardinalities and schemas.
            join(*left, *right, result, workspace);
            ASSERT_EQ(result.size(), expected.size());
            for (const auto& row : expected)
                EXPECT_TRUE(result.contains(row));
        }
    }
}

}  // namespace ygg::tests
