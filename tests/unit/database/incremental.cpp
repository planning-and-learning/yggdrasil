/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/incremental/join.hpp"
#include "yggdrasil/database/incremental/projection.hpp"
#include "yggdrasil/database/relation_repository.hpp"

#include <algorithm>
#include <array>
#include <compare>
#include <gtest/gtest.h>
#include <set>
#include <span>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace ygg::tests
{
struct IncrementalValue
{
    uint_t value;
    friend auto operator<=>(const IncrementalValue&, const IncrementalValue&) = default;
};
}  // namespace ygg::tests

namespace ygg::database
{
template<>
struct ColumnCodec<tests::IncrementalValue>
{
    static constexpr size_t size = ColumnCodec<uint_t>::size;
    static void encode(tests::IncrementalValue value, std::span<std::byte> bytes) { ColumnCodec<uint_t>::encode(value.value, bytes); }
    static tests::IncrementalValue decode(std::span<const std::byte> bytes) { return { ColumnCodec<uint_t>::decode(bytes) }; }
};
}  // namespace ygg::database

namespace ygg::tests
{
using namespace database;
using Values = TypeList<uint_t>;
using namespace database::incremental;
using ColumnIndex = Index<Column>;

namespace
{
template<typename T = uint_t, typename... Args>
auto cells(Args... values)
{
    return std::tuple { T(values)... };
}

template<typename T, size_t N>
auto packed(const std::array<T, N>& values)
{
    std::array<std::byte, N * ColumnCodec<T>::size> result;
    for (size_t i = 0; i < N; ++i)
        ColumnCodec<T>::encode(values[i], std::span(result).subspan(i * ColumnCodec<T>::size, ColumnCodec<T>::size));
    return result;
}

template<typename T, RelationViewConcept<TypeList<T>> V>
std::set<std::vector<T>> rows(const V& relation)
{
    std::set<std::vector<T>> result;
    for (size_t i = 0; i < relation.size(); ++i)
    {
        const auto row = Row<TypeList<T>>(relation.row(i), relation.columns().span());
        std::vector<T> values;
        for (size_t column = 0; column < row.size(); ++column)
            values.push_back(row.template get<T>(column));
        result.insert(std::move(values));
    }
    return result;
}

template<typename T, typename Evaluator>
void expect_update(const Evaluator& evaluator, const std::set<std::vector<T>>& before, const Builder<Relation<TypeList<T>>>& reference)
{
    const auto after = rows<T>(reference);
    EXPECT_TRUE(std::ranges::equal(evaluator.get_result().columns(), reference.columns()));
    EXPECT_TRUE(std::ranges::equal(evaluator.get_delta().added.columns(), reference.columns()));
    EXPECT_TRUE(std::ranges::equal(evaluator.get_delta().removed.columns(), reference.columns()));
    EXPECT_EQ(rows<T>(evaluator.get_result()), after);
    std::set<std::vector<T>> added;
    std::set<std::vector<T>> removed;
    for (const auto& row : after)
        if (!before.contains(row))
            added.insert(row);
    for (const auto& row : before)
        if (!after.contains(row))
            removed.insert(row);
    EXPECT_EQ(rows<T>(evaluator.get_delta().added), added);
    EXPECT_EQ(rows<T>(evaluator.get_delta().removed), removed);
}

template<typename T>
void apply_batch(Builder<Relation<TypeList<T>>>& input, const Builder<Relation<TypeList<T>>>& added, const Builder<Relation<TypeList<T>>>& removed)
{
    for (size_t i = 0; i < removed.size(); ++i)
        input.erase(input.find(removed.row(i)).value());
    for (size_t i = 0; i < added.size(); ++i)
        input.insert(added.row(i));
}
}  // namespace

TEST(YggdrasilTests, DatabaseRelationErasureInvalidatesIdentityOnlyAfterSuccess)
{
    Builder<Relation<Values>> relation({ ColumnIndex(7), ColumnIndex(3) });
    const auto first = relation.insert(cells(1, 2));
    relation.insert(cells(3, 4));
    relation.insert(cells(5, 6));
    const std::array<uint_t, 2> sought { 1, 2 };
    EXPECT_EQ(relation.find(packed(sought)), first);
    EXPECT_EQ(relation.find(packed(sought)), first);
    EXPECT_EQ(relation.find(cells(1, 2)), first);
    EXPECT_FALSE(relation.find(cells(9, 9)));
    EXPECT_THROW((void) relation.find(cells(1)), std::invalid_argument);
    const auto canonical = Index<Relation<Values>>(9);
    relation.set_index(canonical);
    EXPECT_THROW(relation.erase(relation.size()), std::out_of_range);
    EXPECT_EQ(relation.get_index(), canonical);
    relation.erase(first);
    EXPECT_EQ(relation.get_index(), Index<Relation<Values>>());
    EXPECT_FALSE(relation.contains(cells(1, 2)));
    EXPECT_TRUE(relation.contains(cells(3, 4)));
    EXPECT_TRUE(relation.contains(cells(5, 6)));
    EXPECT_EQ(relation.find(cells(5, 6)), first);
}

TEST(YggdrasilTests, DatabaseIncrementalProjectionTracksSupportAcrossCompactionAndUndo)
{
    const auto large = ColumnIndex::max();
    Builder<Relation<Values>> input({ large, ColumnIndex(4), ColumnIndex(11) });
    input.insert(cells(1, 10, 7));
    input.insert(cells(1, 11, 7));
    input.insert(cells(2, 10, 8));
    input.insert(cells(3, 10, 9));
    const ProjectionPlan<Values> plan(input.columns(), { ColumnIndex(11), large });
    ProjectionEvaluator<Values> evaluator(plan);
    Workspace<Values> workspace;
    Builder<Relation<Values>> reference(plan.output_columns());
    evaluator.initialize(input, workspace);
    project(input, plan, reference, workspace);
    EXPECT_EQ(rows<uint_t>(evaluator.get_result()), rows<uint_t>(reference));
    EXPECT_TRUE(evaluator.get_delta().added.empty());
    EXPECT_TRUE(evaluator.get_delta().removed.empty());

    Delta<Values> replacement(input.columns());
    replacement.removed.insert(cells(1, 10, 7));
    replacement.removed.insert(cells(1, 11, 7));
    replacement.added.insert(cells(1, 12, 7));
    Delta<Values> erase_first(input.columns());
    erase_first.removed.insert(cells(1, 12, 7));
    erase_first.added.insert(cells(4, 1, 10));
    Delta<Values> erase_moved(input.columns());
    erase_moved.removed.insert(cells(3, 10, 9));
    erase_moved.added.insert(cells(2, 11, 8));
    const auto update = [&](const auto& added, const auto& removed)
    {
        const auto before = rows<uint_t>(reference);
        apply_batch(input, added, removed);
        evaluator.update(added, removed, workspace);
        project(input, plan, reference, workspace);
        expect_update(evaluator, before, reference);
    };
    const auto replaced_index = evaluator.get_result().find(cells(7, 1)).value();
    const auto untouched_index = evaluator.get_result().find(cells(9, 3)).value();
    const auto replaced_row = evaluator.get_result().row(replaced_index);
    const auto untouched_row = evaluator.get_result().row(untouched_index);
    update(replacement.added, replacement.removed);
    // Consolidating witnesses must not erase/reinsert an unchanged output or
    // move the last output into its slot as a temporary consequence.
    EXPECT_EQ(evaluator.get_result().find(cells(7, 1)).value(), replaced_index);
    EXPECT_EQ(evaluator.get_result().find(cells(9, 3)).value(), untouched_index);
    EXPECT_EQ(evaluator.get_result().row(replaced_index).data(), replaced_row.data());
    EXPECT_EQ(evaluator.get_result().row(untouched_index).data(), untouched_row.data());
    EXPECT_EQ(ColumnCodec<uint_t>::decode(replaced_row.subspan(0 * sizeof(uint_t), sizeof(uint_t))), 7);
    EXPECT_EQ(ColumnCodec<uint_t>::decode(replaced_row.subspan(1 * sizeof(uint_t), sizeof(uint_t))), 1);
    EXPECT_EQ(ColumnCodec<uint_t>::decode(untouched_row.subspan(0 * sizeof(uint_t), sizeof(uint_t))), 9);
    EXPECT_EQ(ColumnCodec<uint_t>::decode(untouched_row.subspan(1 * sizeof(uint_t), sizeof(uint_t))), 3);
    EXPECT_TRUE(evaluator.get_delta().added.empty());
    EXPECT_TRUE(evaluator.get_delta().removed.empty());
    update(erase_first.added, erase_first.removed);
    update(erase_moved.added, erase_moved.removed);
    update(erase_moved.removed, erase_moved.added);
    update(erase_first.removed, erase_first.added);
    update(replacement.removed, replacement.added);
    const auto retained = evaluator.memory_usage();
    evaluator.initialize(input, workspace);
    EXPECT_EQ(evaluator.memory_usage(), retained);
    EXPECT_TRUE(evaluator.get_delta().added.empty());
    EXPECT_TRUE(evaluator.get_delta().removed.empty());
}

TEST(YggdrasilTests, DatabaseIncrementalProjectionConsolidatesMixedChangesAndUndo)
{
    Builder<Relation<Values>> input({ ColumnIndex(1), ColumnIndex(2) });
    input.insert(cells(1, 10));
    input.insert(cells(1, 11));
    input.insert(cells(1, 12));
    input.insert(cells(2, 20));
    input.insert(cells(2, 21));
    input.insert(cells(3, 30));
    input.insert(cells(4, 40));
    input.insert(cells(5, 50));
    const auto initial = rows<uint_t>(input);
    const ProjectionPlan<Values> plan(input.columns(), { ColumnIndex(1) });
    ProjectionEvaluator<Values> evaluator(plan);
    Workspace<Values> workspace;
    Builder<Relation<Values>> reference(plan.output_columns());
    evaluator.initialize(input, workspace);
    project(input, plan, reference, workspace);
    Delta<Values> delta(input.columns());
    delta.removed.insert(cells(1, 10));
    delta.removed.insert(cells(1, 11));
    delta.added.insert(cells(1, 13));  // Fewer witnesses, same output.
    delta.removed.insert(cells(2, 20));
    delta.removed.insert(cells(2, 21));
    delta.added.insert(cells(2, 22));  // Replace every witness, same output.
    delta.added.insert(cells(2, 23));
    delta.removed.insert(cells(3, 30));  // Last witness removes the output.
    delta.added.insert(cells(5, 51));    // More witnesses, same output.
    delta.added.insert(cells(6, 60));    // New output has multiple witnesses.
    delta.added.insert(cells(6, 61));
    const auto update = [&](const auto& added, const auto& removed)
    {
        const auto before = rows<uint_t>(reference);
        apply_batch(input, added, removed);
        evaluator.update(added, removed, workspace);
        project(input, plan, reference, workspace);
        expect_update(evaluator, before, reference);
    };
    update(delta.added, delta.removed);
    EXPECT_EQ(evaluator.get_delta().added.size(), 1);
    EXPECT_TRUE(evaluator.get_delta().added.contains(cells(6)));
    EXPECT_EQ(evaluator.get_delta().removed.size(), 1);
    EXPECT_TRUE(evaluator.get_delta().removed.contains(cells(3)));
    update(delta.removed, delta.added);
    EXPECT_EQ(rows<uint_t>(input), initial);
    // Removing the restored input checks every consolidated support count,
    // including unchanged membership and rows moved by compact erasure.
    Builder<Relation<Values>> empty(input.columns());
    Builder<Relation<Values>> all(input.columns());
    for (size_t i = 0; i < input.size(); ++i)
        all.insert(input.row(i));
    update(empty, all);
    EXPECT_TRUE(evaluator.get_result().empty());
    update(all, empty);
    EXPECT_EQ(rows<uint_t>(input), initial);
}

TEST(YggdrasilTests, DatabaseIncrementalProjectionDoesNotHideRemovalUnderflowWithAdditions)
{
    Builder<Relation<Values>> input({ ColumnIndex(1), ColumnIndex(2) });
    input.insert(cells(7, 10));
    ProjectionEvaluator<Values> evaluator(ProjectionPlan<Values>(input.columns(), { ColumnIndex(1) }));
    Workspace<Values> workspace;
    evaluator.initialize(input, workspace);
    Delta<Values> invalid(input.columns());
    invalid.removed.insert(cells(7, 10));
    invalid.removed.insert(cells(7, 11));  // Only one existing witness is available.
    invalid.added.insert(cells(7, 12));
    invalid.added.insert(cells(7, 13));  // Net support change is zero, but removal is invalid.
    EXPECT_THROW(evaluator.update(invalid.added, invalid.removed, workspace), std::invalid_argument);
    Builder<Relation<Values>> empty(input.columns());
    EXPECT_THROW(evaluator.update(empty, empty, workspace), std::logic_error);
    evaluator.initialize(input, workspace);
    Delta<Values> valid(input.columns());
    valid.removed.insert(cells(7, 10));
    valid.added.insert(cells(7, 12));
    evaluator.update(valid.added, valid.removed, workspace);
    EXPECT_TRUE(evaluator.get_result().contains(cells(7)));
    EXPECT_TRUE(evaluator.get_delta().added.empty());
    EXPECT_TRUE(evaluator.get_delta().removed.empty());
    evaluator.update(empty, valid.added, workspace);
    EXPECT_TRUE(evaluator.get_result().empty());
    EXPECT_TRUE(evaluator.get_delta().removed.contains(cells(7)));
}

TEST(YggdrasilTests, DatabaseIncrementalProjectionHandlesEmptyAndNullaryRelations)
{
    Builder<Relation<Values>> input({ ColumnIndex(4) });
    const ProjectionPlan<Values> plan(input.columns(), {});
    ProjectionEvaluator<Values> evaluator(plan);
    Workspace<Values> workspace;
    Builder<Relation<Values>> reference;
    evaluator.initialize(input, workspace);
    EXPECT_TRUE(evaluator.get_result().empty());
    Delta<Values> delta(input.columns());
    delta.added.insert(cells(1));
    delta.added.insert(cells(2));
    const auto update = [&]
    {
        const auto before = rows<uint_t>(reference);
        apply_batch(input, delta.added, delta.removed);
        evaluator.update(delta.added, delta.removed, workspace);
        project(input, plan, reference, workspace);
        expect_update(evaluator, before, reference);
    };
    update();
    EXPECT_TRUE(evaluator.get_delta().added.contains(cells()));
    delta.clear();
    delta.removed.insert(cells(1));
    update();
    EXPECT_TRUE(evaluator.get_delta().removed.empty());
    delta.clear();
    delta.removed.insert(cells(2));
    update();
    EXPECT_TRUE(evaluator.get_delta().removed.contains(cells()));
    delta.clear();
    update();
    EXPECT_TRUE(evaluator.get_delta().added.empty());
    EXPECT_TRUE(evaluator.get_delta().removed.empty());

    Builder<Relation<Values>> nullary;
    Delta<Values> nullary_delta({});
    ProjectionEvaluator<Values> identity(ProjectionPlan<Values>({}, {}));
    identity.initialize(nullary, workspace);
    nullary_delta.added.insert(cells());
    identity.update(nullary_delta.added, nullary_delta.removed, workspace);
    EXPECT_TRUE(identity.get_result().contains(cells()));
    EXPECT_TRUE(identity.get_delta().added.contains(cells()));
    identity.update(nullary_delta.removed, nullary_delta.added, workspace);
    EXPECT_TRUE(identity.get_result().empty());
    EXPECT_TRUE(identity.get_delta().removed.contains(cells()));
}

TEST(YggdrasilTests, DatabaseIncrementalJoinUpdatesEitherSideIndependently)
{
    Builder<Relation<Values>> lhs({ ColumnIndex(7), ColumnIndex(2), ColumnIndex(9) });
    Builder<Relation<Values>> rhs({ ColumnIndex(9), ColumnIndex(11), ColumnIndex(2) });
    lhs.insert(cells(1, 2, 3));
    lhs.insert(cells(4, 2, 3));
    lhs.insert(cells(5, 8, 3));
    rhs.insert(cells(3, 20, 2));
    rhs.insert(cells(3, 21, 2));
    rhs.insert(cells(3, 22, 8));
    rhs.insert(cells(9, 23, 2));
    const JoinPlan<Values> plan(lhs.columns(), rhs.columns());
    auto repository = RelationRepositoryFactory<Values>().create();
    Workspace<Values> workspace;
    for (const bool change_left : { false, true })
    {
        auto& dynamic = change_left ? lhs : rhs;
        Builder<Relation<Values>> unchanged(change_left ? rhs.columns() : lhs.columns());
        JoinEvaluator<Values> evaluator(plan);
        Builder<Relation<Values>> reference(plan.output_columns());
        evaluator.initialize(make_view(lhs, repository), make_view(rhs, repository), workspace);
        join(lhs, rhs, plan, reference, workspace);
        EXPECT_EQ(rows<uint_t>(evaluator.get_result()), rows<uint_t>(reference));
        EXPECT_TRUE(evaluator.get_delta().added.empty());
        EXPECT_TRUE(evaluator.get_delta().removed.empty());
        Delta<Values> delta(dynamic.columns());
        delta.removed.insert(dynamic.row(0));
        if (!change_left)
            delta.added.insert(cells(3, 30, 2));
        else
            delta.added.insert(cells(6, 2, 3));
        const auto update = [&](const auto& added, const auto& removed)
        {
            const auto before = rows<uint_t>(reference);
            apply_batch(dynamic, added, removed);
            if (change_left)
                evaluator.update(added, removed, unchanged, unchanged, workspace);
            else
                evaluator.update(unchanged, unchanged, added, removed, workspace);
            join(lhs, rhs, plan, reference, workspace);
            expect_update(evaluator, before, reference);
        };
        update(delta.added, delta.removed);
        update(delta.removed, delta.added);
        delta.clear();
        update(delta.added, delta.removed);
    }
}

TEST(YggdrasilTests, DatabaseIncrementalJoinAcceptsInternedCustomCodecInputs)
{
    using Value = IncrementalValue;
    Builder<Relation<TypeList<Value>>> dynamic({ ColumnIndex(1), ColumnIndex(2) });
    Builder<Relation<TypeList<Value>>> immutable({ ColumnIndex(2), ColumnIndex(3) });
    dynamic.insert(std::tuple { Value { 1 }, Value { 10 } });
    immutable.insert(std::tuple { Value { 10 }, Value { 100 } });
    immutable.insert(std::tuple { Value { 20 }, Value { 200 } });
    immutable.insert(std::tuple { Value { 30 }, Value { 300 } });
    auto repository = RelationRepositoryFactory<TypeList<Value>>().create();
    const auto view = database::insert(repository, immutable).first;
    const JoinPlan<TypeList<Value>> plan(dynamic.columns(), immutable.columns());
    JoinEvaluator<TypeList<Value>> evaluator(plan);
    Builder<Relation<TypeList<Value>>> unchanged(immutable.columns());
    Workspace<TypeList<Value>> workspace;
    Builder<Relation<TypeList<Value>>> reference(plan.output_columns());
    evaluator.initialize(dynamic, view, workspace);
    join(dynamic, immutable, plan, reference, workspace);
    EXPECT_EQ(rows<Value>(evaluator.get_result()), rows<Value>(reference));
    Delta<TypeList<Value>> delta(dynamic.columns());
    delta.removed.insert(std::tuple { Value { 1 }, Value { 10 } });
    delta.added.insert(std::tuple { Value { 2 }, Value { 20 } });
    delta.added.insert(std::tuple { Value { 3 }, Value { 40 } });
    const auto before = rows<Value>(reference);
    apply_batch(dynamic, delta.added, delta.removed);
    evaluator.update(delta.added, delta.removed, unchanged, unchanged, workspace);
    join(dynamic, immutable, plan, reference, workspace);
    expect_update(evaluator, before, reference);
}

TEST(YggdrasilTests, DatabaseIncrementalProjectionValidationPrecedesMutation)
{
    Builder<Relation<Values>> input({ ColumnIndex(1) });
    input.insert(cells(1));
    Builder<Relation<Values>> empty(input.columns());
    Builder<Relation<Values>> wrong({ ColumnIndex(2) });
    Builder<Relation<Values>> overlap(input.columns());
    overlap.insert(cells(1));
    ProjectionEvaluator<Values> evaluator(ProjectionPlan<Values>(input.columns(), { ColumnIndex(1) }));
    Workspace<Values> workspace;
    EXPECT_THROW(evaluator.update(empty, empty, workspace), std::logic_error);
    evaluator.initialize(input, workspace);
    const auto before = rows<uint_t>(evaluator.get_result());
    EXPECT_THROW(evaluator.initialize(wrong, workspace), std::invalid_argument);
    EXPECT_THROW(evaluator.update(wrong, empty, workspace), std::invalid_argument);
    EXPECT_THROW(evaluator.update(empty, wrong, workspace), std::invalid_argument);
    EXPECT_THROW(evaluator.update(overlap, overlap, workspace), std::invalid_argument);
    EXPECT_EQ(rows<uint_t>(evaluator.get_result()), before);
    evaluator.update(empty, empty, workspace);
    EXPECT_EQ(rows<uint_t>(evaluator.get_result()), before);
    EXPECT_TRUE(evaluator.get_delta().added.empty());
    EXPECT_TRUE(evaluator.get_delta().removed.empty());
    Delta<Values> delta(input.columns());
    delta.added.insert(cells(1));
    delta.removed.insert(cells(2));
    const auto memory = delta.added.memory_usage() + delta.removed.memory_usage();
    delta.clear();
    EXPECT_TRUE(delta.added.empty());
    EXPECT_TRUE(delta.removed.empty());
    EXPECT_EQ(delta.added.memory_usage() + delta.removed.memory_usage(), memory);
}

TEST(YggdrasilTests, DatabaseIncrementalJoinUpdatesBothInputsAndRestoresBranches)
{
    Builder<Relation<Values>> lhs({ ColumnIndex(9), ColumnIndex(2), ColumnIndex(7) });
    Builder<Relation<Values>> rhs({ ColumnIndex(7), ColumnIndex(11), ColumnIndex(2) });
    lhs.insert(cells(1, 10, 100));
    lhs.insert(cells(2, 10, 100));
    lhs.insert(cells(3, 20, 200));
    lhs.insert(cells(4, 30, 300));
    rhs.insert(cells(100, 7, 10));
    rhs.insert(cells(100, 8, 10));
    rhs.insert(cells(200, 7, 20));
    rhs.insert(cells(300, 9, 30));
    const auto initial_lhs = rows<uint_t>(lhs);
    const auto initial_rhs = rows<uint_t>(rhs);
    const JoinPlan<Values> plan(lhs.columns(), rhs.columns());
    const ProjectionPlan<Values> projection_plan(plan.output_columns(), { ColumnIndex(11) });
    JoinEvaluator<Values> evaluator(plan);
    ProjectionEvaluator<Values> projected(projection_plan);
    Workspace<Values> workspace;
    auto repository = RelationRepositoryFactory<Values>().create();
    evaluator.initialize(make_view(lhs, repository), make_view(rhs, repository), workspace);
    projected.initialize(evaluator.get_result(), workspace);
    Builder<Relation<Values>> reference(plan.output_columns());
    Builder<Relation<Values>> reference_projection(projection_plan.output_columns());
    join(lhs, rhs, plan, reference, workspace);
    project(reference, projection_plan, reference_projection, workspace);
    EXPECT_EQ(rows<uint_t>(evaluator.get_result()), rows<uint_t>(reference));
    EXPECT_TRUE(evaluator.get_delta().added.empty());
    EXPECT_TRUE(evaluator.get_delta().removed.empty());
    const auto update = [&](const auto& lhs_added, const auto& lhs_removed, const auto& rhs_added, const auto& rhs_removed)
    {
        const auto before = rows<uint_t>(reference);
        const auto projection_before = rows<uint_t>(reference_projection);
        apply_batch(lhs, lhs_added, lhs_removed);
        apply_batch(rhs, rhs_added, rhs_removed);
        evaluator.update(make_view(lhs_added, repository),
                         make_view(lhs_removed, repository),
                         make_view(rhs_added, repository),
                         make_view(rhs_removed, repository),
                         workspace);
        projected.update(evaluator.get_delta().added, evaluator.get_delta().removed, workspace);
        join(lhs, rhs, plan, reference, workspace);
        project(reference, projection_plan, reference_projection, workspace);
        expect_update(evaluator, before, reference);
        expect_update(projected, projection_before, reference_projection);
    };
    Delta<Values> lhs_a(lhs.columns());
    Delta<Values> rhs_a(rhs.columns());
    lhs_a.removed.insert(cells(1, 10, 100));
    lhs_a.removed.insert(cells(3, 20, 200));
    lhs_a.added.insert(cells(5, 40, 400));
    lhs_a.added.insert(cells(6, 10, 100));
    rhs_a.removed.insert(cells(100, 8, 10));
    rhs_a.removed.insert(cells(200, 7, 20));
    rhs_a.added.insert(cells(400, 10, 40));
    rhs_a.added.insert(cells(100, 11, 10));
    update(lhs_a.added, lhs_a.removed, rhs_a.added, rhs_a.removed);
    // New/new pairs must appear; old/new and new/old crossed pairs must not.
    EXPECT_TRUE(evaluator.get_delta().added.contains(cells(5, 40, 400, 10)));
    EXPECT_FALSE(evaluator.get_delta().added.contains(cells(1, 10, 100, 11)));
    EXPECT_FALSE(evaluator.get_delta().added.contains(cells(6, 10, 100, 8)));
    Delta<Values> lhs_b(lhs.columns());
    Delta<Values> rhs_b(rhs.columns());
    lhs_b.removed.insert(cells(4, 30, 300));
    lhs_b.added.insert(cells(7, 40, 400));
    rhs_b.removed.insert(cells(300, 9, 30));
    rhs_b.added.insert(cells(400, 12, 40));
    update(lhs_b.added, lhs_b.removed, rhs_b.added, rhs_b.removed);
    update(lhs_b.removed, lhs_b.added, rhs_b.removed, rhs_b.added);
    update(lhs_a.removed, lhs_a.added, rhs_a.removed, rhs_a.added);
    EXPECT_EQ(rows<uint_t>(lhs), initial_lhs);
    EXPECT_EQ(rows<uint_t>(rhs), initial_rhs);
    Delta<Values> sibling(lhs.columns());
    Delta<Values> unchanged(rhs.columns());
    sibling.removed.insert(cells(2, 10, 100));
    sibling.added.insert(cells(8, 30, 300));
    update(sibling.added, sibling.removed, unchanged.added, unchanged.removed);
    update(sibling.removed, sibling.added, unchanged.added, unchanged.removed);
    EXPECT_EQ(rows<uint_t>(lhs), initial_lhs);
    EXPECT_EQ(rows<uint_t>(rhs), initial_rhs);
    evaluator.initialize(lhs, rhs, workspace);
    EXPECT_EQ(rows<uint_t>(evaluator.get_result()), rows<uint_t>(reference));
    EXPECT_TRUE(evaluator.get_delta().added.empty());
    EXPECT_TRUE(evaluator.get_delta().removed.empty());
}

TEST(YggdrasilTests, DatabaseIncrementalJoinMaintainsCustomCodecIndexesAcrossRowCompaction)
{
    using Value = IncrementalValue;
    Builder<Relation<TypeList<Value>>> lhs({ ColumnIndex(1), ColumnIndex(2) });
    Builder<Relation<TypeList<Value>>> rhs({ ColumnIndex(2), ColumnIndex(3) });
    for (uint_t i = 0; i < 5; ++i)
    {
        lhs.insert(std::tuple { Value { i }, Value { i % 3 } });
        rhs.insert(std::tuple { Value { i % 3 }, Value { i } });
    }
    const JoinPlan<TypeList<Value>> plan(lhs.columns(), rhs.columns());
    JoinEvaluator<TypeList<Value>> evaluator(plan);
    Workspace<TypeList<Value>> workspace;
    Builder<Relation<TypeList<Value>>> reference(plan.output_columns());
    evaluator.initialize(lhs, rhs, workspace);
    join(lhs, rhs, plan, reference, workspace);
    EXPECT_EQ(rows<Value>(evaluator.get_result()), rows<Value>(reference));
    Delta<TypeList<Value>> lhs_delta(lhs.columns());
    Delta<TypeList<Value>> rhs_delta(rhs.columns());
    for (uint_t generation = 0; generation < 24; ++generation)
    {
        lhs_delta.clear();
        rhs_delta.clear();
        if (generation % 3 != 1)
        {
            lhs_delta.removed.insert(lhs.row(generation % lhs.size()));
            lhs_delta.added.insert(std::tuple { Value { 100 + generation }, Value { generation % 3 } });
        }
        if (generation % 3 != 2)
        {
            rhs_delta.removed.insert(rhs.row((generation + 1) % rhs.size()));
            rhs_delta.added.insert(std::tuple { Value { generation % 3 }, Value { 100 + generation } });
        }
        const auto before = rows<Value>(reference);
        apply_batch(lhs, lhs_delta.added, lhs_delta.removed);
        apply_batch(rhs, rhs_delta.added, rhs_delta.removed);
        evaluator.update(lhs_delta.added, lhs_delta.removed, rhs_delta.added, rhs_delta.removed, workspace);
        join(lhs, rhs, plan, reference, workspace);
        expect_update(evaluator, before, reference);
    }
}

TEST(YggdrasilTests, DatabaseIncrementalJoinHandlesCartesianAndNullaryChangesOnBothSides)
{
    Workspace<Values> workspace;
    for (unsigned nullary_sides = 0; nullary_sides < 4; ++nullary_sides)
    {
        Builder<Relation<Values>> lhs;
        Builder<Relation<Values>> rhs;
        if (!(nullary_sides & 1))
            lhs.initialize({ ColumnIndex(1) });
        if (!(nullary_sides & 2))
            rhs.initialize({ ColumnIndex(2) });
        const JoinPlan<Values> plan(lhs.columns(), rhs.columns());
        JoinEvaluator<Values> evaluator(plan);
        Builder<Relation<Values>> reference(plan.output_columns());
        evaluator.initialize(lhs, rhs, workspace);
        EXPECT_TRUE(evaluator.get_result().empty());
        Delta<Values> lhs_delta(lhs.columns());
        Delta<Values> rhs_delta(rhs.columns());
        if (lhs.arity() == 0)
            lhs_delta.added.insert(cells());
        else
        {
            lhs_delta.added.insert(cells(1));
            lhs_delta.added.insert(cells(2));
        }
        if (rhs.arity() == 0)
            rhs_delta.added.insert(cells());
        else
        {
            rhs_delta.added.insert(cells(10));
            rhs_delta.added.insert(cells(20));
        }
        const auto update = [&](const auto& lhs_added, const auto& lhs_removed, const auto& rhs_added, const auto& rhs_removed)
        {
            const auto before = rows<uint_t>(reference);
            apply_batch(lhs, lhs_added, lhs_removed);
            apply_batch(rhs, rhs_added, rhs_removed);
            evaluator.update(lhs_added, lhs_removed, rhs_added, rhs_removed, workspace);
            join(lhs, rhs, plan, reference, workspace);
            expect_update(evaluator, before, reference);
        };
        update(lhs_delta.added, lhs_delta.removed, rhs_delta.added, rhs_delta.removed);
        Delta<Values> lhs_remove(lhs.columns());
        Delta<Values> rhs_remove(rhs.columns());
        lhs_remove.removed.insert(lhs.row(0));
        rhs_remove.removed.insert(rhs.row(0));
        update(lhs_remove.added, lhs_remove.removed, rhs_remove.added, rhs_remove.removed);
        update(lhs_remove.removed, lhs_remove.added, rhs_remove.removed, rhs_remove.added);
        update(lhs_delta.removed, lhs_delta.added, rhs_delta.removed, rhs_delta.added);
        EXPECT_TRUE(lhs.empty());
        EXPECT_TRUE(rhs.empty());
        update(lhs_remove.added, lhs_remove.added, rhs_remove.added, rhs_remove.added);
        EXPECT_TRUE(evaluator.get_delta().added.empty());
        EXPECT_TRUE(evaluator.get_delta().removed.empty());
        // Changing one input while the other remains empty emits no rows.
        update(lhs_delta.added, lhs_delta.removed, rhs_delta.removed, rhs_delta.removed);
        EXPECT_TRUE(evaluator.get_result().empty());
        EXPECT_TRUE(evaluator.get_delta().added.empty());
        update(lhs_delta.removed, lhs_delta.removed, rhs_delta.added, rhs_delta.removed);
        update(lhs_delta.removed, lhs_delta.added, rhs_delta.removed, rhs_delta.removed);
        EXPECT_TRUE(evaluator.get_result().empty());
        update(lhs_delta.removed, lhs_delta.removed, rhs_delta.removed, rhs_delta.added);
    }
}

TEST(YggdrasilTests, DatabaseIncrementalJoinValidatesAllInputsBeforeMutation)
{
    Builder<Relation<Values>> input({ ColumnIndex(1) });
    input.insert(cells(1));
    Builder<Relation<Values>> empty(input.columns());
    Builder<Relation<Values>> addition(input.columns());
    addition.insert(cells(2));
    Builder<Relation<Values>> wrong({ ColumnIndex(2) });
    JoinEvaluator<Values> evaluator(JoinPlan<Values>(input.columns(), input.columns()));
    Workspace<Values> workspace;
    EXPECT_THROW(evaluator.update(empty, empty, empty, empty, workspace), std::logic_error);
    evaluator.initialize(input, input, workspace);
    evaluator.update(addition, empty, addition, empty, workspace);
    EXPECT_TRUE(evaluator.get_delta().added.contains(cells(2)));
    const auto before = rows<uint_t>(evaluator.get_result());
    const auto added_before = rows<uint_t>(evaluator.get_delta().added);
    EXPECT_THROW(evaluator.initialize(wrong, input, workspace), std::invalid_argument);
    EXPECT_THROW(evaluator.initialize(input, wrong, workspace), std::invalid_argument);
    EXPECT_THROW(evaluator.update(wrong, empty, empty, empty, workspace), std::invalid_argument);
    EXPECT_THROW(evaluator.update(empty, wrong, empty, empty, workspace), std::invalid_argument);
    EXPECT_THROW(evaluator.update(empty, empty, wrong, empty, workspace), std::invalid_argument);
    EXPECT_THROW(evaluator.update(empty, empty, empty, wrong, workspace), std::invalid_argument);
    EXPECT_THROW(evaluator.update(addition, addition, empty, empty, workspace), std::invalid_argument);
    EXPECT_THROW(evaluator.update(empty, empty, addition, addition, workspace), std::invalid_argument);
    EXPECT_EQ(rows<uint_t>(evaluator.get_result()), before);
    EXPECT_EQ(rows<uint_t>(evaluator.get_delta().added), added_before);
    evaluator.update(empty, empty, empty, empty, workspace);
    EXPECT_EQ(rows<uint_t>(evaluator.get_result()), before);
    EXPECT_TRUE(evaluator.get_delta().added.empty());
    EXPECT_TRUE(evaluator.get_delta().removed.empty());
}

TEST(YggdrasilTests, DatabaseIncrementalJoinRequiresReinitializationAfterMutationFailure)
{
    Builder<Relation<Values>> input({ ColumnIndex(1) });
    input.insert(cells(1));
    Builder<Relation<Values>> empty(input.columns());
    Builder<Relation<Values>> addition(input.columns());
    addition.insert(cells(2));
    Builder<Relation<Values>> absent(input.columns());
    absent.insert(cells(99));
    JoinEvaluator<Values> evaluator(JoinPlan<Values>(input.columns(), input.columns()));
    Workspace<Values> workspace;
    for (const bool invalid_removal : { true, false })
    {
        evaluator.initialize(input, input, workspace);
        // Mutate the left input successfully before the right input fails.
        if (invalid_removal)
            EXPECT_THROW(evaluator.update(empty, input, empty, absent, workspace), std::invalid_argument);
        else
            EXPECT_THROW(evaluator.update(addition, empty, input, empty, workspace), std::invalid_argument);
        EXPECT_THROW(evaluator.update(empty, empty, empty, empty, workspace), std::logic_error);
        evaluator.initialize(input, input, workspace);
        EXPECT_EQ(rows<uint_t>(evaluator.get_result()), rows<uint_t>(input));
        EXPECT_TRUE(evaluator.get_delta().added.empty());
        EXPECT_TRUE(evaluator.get_delta().removed.empty());
        evaluator.update(addition, empty, addition, empty, workspace);
        EXPECT_EQ(evaluator.get_result().size(), 2);
        EXPECT_TRUE(evaluator.get_result().contains(cells(1)));
        EXPECT_TRUE(evaluator.get_result().contains(cells(2)));
        EXPECT_EQ(rows<uint_t>(evaluator.get_delta().added), rows<uint_t>(addition));
        EXPECT_TRUE(evaluator.get_delta().removed.empty());
    }
}

}  // namespace ygg::tests
