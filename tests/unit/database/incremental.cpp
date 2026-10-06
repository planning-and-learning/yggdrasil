/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/incremental/projection.hpp"
#include "yggdrasil/database/incremental/join.hpp"
#include "yggdrasil/database/relation_repository.hpp"

#include <algorithm>
#include <array>
#include <compare>
#include <gtest/gtest.h>
#include <set>
#include <span>
#include <stdexcept>
#include <vector>

namespace ygg::tests
{
struct IncrementalCollisionValue
{
    uint_t value;
    friend auto operator<=>(const IncrementalCollisionValue&, const IncrementalCollisionValue&) = default;
};
}  // namespace ygg::tests

namespace ygg
{
template<>
struct Hash<tests::IncrementalCollisionValue>
{
    hash_t operator()(const tests::IncrementalCollisionValue&) const noexcept { return 0; }
};
}  // namespace ygg

namespace ygg::tests
{
using namespace database;
using namespace database::incremental;
using ColumnIndex = Index<Column>;

namespace
{
template<typename T, RelationViewConcept<T> V>
std::set<std::vector<T>> rows(const V& relation)
{
    std::set<std::vector<T>> result;
    for (size_t i = 0; i < relation.size(); ++i)
    {
        const auto row = relation.row(i);
        result.emplace(row.begin(), row.end());
    }
    return result;
}

template<typename T, typename Evaluator>
void expect_update(const Evaluator& evaluator, const std::set<std::vector<T>>& before, const Builder<Relation<T>>& reference)
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
void apply_batch(Builder<Relation<T>>& input, const Builder<Relation<T>>& added, const Builder<Relation<T>>& removed)
{
    for (size_t i = 0; i < removed.size(); ++i)
        input.erase(input.find(removed.row(i)).value());
    for (size_t i = 0; i < added.size(); ++i)
        input.insert(added.row(i));
}
}  // namespace

TEST(YggdrasilTests, DatabaseRelationErasureInvalidatesIdentityOnlyAfterSuccess)
{
    Builder<Relation<>> relation({ ColumnIndex(7), ColumnIndex(3) });
    const auto first = relation.insert({ 1, 2 });
    relation.insert({ 3, 4 });
    relation.insert({ 5, 6 });
    const std::array<uint_t, 2> sought { 1, 2 };
    EXPECT_EQ(relation.find(std::span<const uint_t>(sought)), first);
    EXPECT_EQ(relation.find(sought), first);
    EXPECT_EQ(relation.find({ 1, 2 }), first);
    EXPECT_FALSE(relation.find({ 9, 9 }));
    EXPECT_THROW((void) relation.find({ 1 }), std::invalid_argument);
    const auto canonical = Index<Relation<>>(9);
    relation.set_index(canonical);
    EXPECT_THROW(relation.erase(relation.size()), std::out_of_range);
    EXPECT_EQ(relation.get_index(), canonical);
    relation.erase(first);
    EXPECT_EQ(relation.get_index(), Index<Relation<>>());
    EXPECT_FALSE(relation.contains({ 1, 2 }));
    EXPECT_TRUE(relation.contains({ 3, 4 }));
    EXPECT_TRUE(relation.contains({ 5, 6 }));
    EXPECT_EQ(relation.find({ 5, 6 }), first);
}

TEST(YggdrasilTests, DatabaseIncrementalProjectionTracksSupportAcrossCompactionAndUndo)
{
    const auto large = ColumnIndex::max();
    Builder<Relation<>> input({ large, ColumnIndex(4), ColumnIndex(11) });
    input.insert({ 1, 10, 7 });
    input.insert({ 1, 11, 7 });
    input.insert({ 2, 10, 8 });
    input.insert({ 3, 10, 9 });
    const ProjectionPlan plan(input.columns(), { ColumnIndex(11), large });
    ProjectionEvaluator<uint_t> evaluator(plan);
    Workspace<> workspace;
    Builder<Relation<>> reference(plan.output_columns());
    evaluator.initialize(input, workspace);
    project(input, plan, reference, workspace);
    EXPECT_EQ(rows<uint_t>(evaluator.get_result()), rows<uint_t>(reference));
    EXPECT_TRUE(evaluator.get_delta().added.empty());
    EXPECT_TRUE(evaluator.get_delta().removed.empty());

    Delta<uint_t> replacement(input.columns());
    replacement.removed.insert({ 1, 10, 7 });
    replacement.removed.insert({ 1, 11, 7 });
    replacement.added.insert({ 1, 12, 7 });
    Delta<uint_t> erase_first(input.columns());
    erase_first.removed.insert({ 1, 12, 7 });
    erase_first.added.insert({ 4, 1, 10 });
    Delta<uint_t> erase_moved(input.columns());
    erase_moved.removed.insert({ 3, 10, 9 });
    erase_moved.added.insert({ 2, 11, 8 });
    const auto update = [&](const auto& added, const auto& removed)
    {
        const auto before = rows<uint_t>(reference);
        apply_batch(input, added, removed);
        evaluator.update(added, removed, workspace);
        project(input, plan, reference, workspace);
        expect_update(evaluator, before, reference);
    };
    const auto replaced_index = evaluator.get_result().find({ 7, 1 }).value();
    const auto untouched_index = evaluator.get_result().find({ 9, 3 }).value();
    const auto replaced_row = evaluator.get_result().row(replaced_index);
    const auto untouched_row = evaluator.get_result().row(untouched_index);
    update(replacement.added, replacement.removed);
    // Consolidating witnesses must not erase/reinsert an unchanged output or
    // move the last output into its slot as a temporary consequence.
    EXPECT_EQ(evaluator.get_result().find({ 7, 1 }).value(), replaced_index);
    EXPECT_EQ(evaluator.get_result().find({ 9, 3 }).value(), untouched_index);
    EXPECT_EQ(evaluator.get_result().row(replaced_index).data(), replaced_row.data());
    EXPECT_EQ(evaluator.get_result().row(untouched_index).data(), untouched_row.data());
    EXPECT_EQ(replaced_row[0], 7);
    EXPECT_EQ(replaced_row[1], 1);
    EXPECT_EQ(untouched_row[0], 9);
    EXPECT_EQ(untouched_row[1], 3);
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
    Builder<Relation<>> input({ ColumnIndex(1), ColumnIndex(2) });
    input.insert({ 1, 10 });
    input.insert({ 1, 11 });
    input.insert({ 1, 12 });
    input.insert({ 2, 20 });
    input.insert({ 2, 21 });
    input.insert({ 3, 30 });
    input.insert({ 4, 40 });
    input.insert({ 5, 50 });
    const auto initial = rows<uint_t>(input);
    const ProjectionPlan plan(input.columns(), { ColumnIndex(1) });
    ProjectionEvaluator<uint_t> evaluator(plan);
    Workspace<> workspace;
    Builder<Relation<>> reference(plan.output_columns());
    evaluator.initialize(input, workspace);
    project(input, plan, reference, workspace);
    Delta<uint_t> delta(input.columns());
    delta.removed.insert({ 1, 10 });
    delta.removed.insert({ 1, 11 });
    delta.added.insert({ 1, 13 }); // Fewer witnesses, same output.
    delta.removed.insert({ 2, 20 });
    delta.removed.insert({ 2, 21 });
    delta.added.insert({ 2, 22 }); // Replace every witness, same output.
    delta.added.insert({ 2, 23 });
    delta.removed.insert({ 3, 30 }); // Last witness removes the output.
    delta.added.insert({ 5, 51 }); // More witnesses, same output.
    delta.added.insert({ 6, 60 }); // New output has multiple witnesses.
    delta.added.insert({ 6, 61 });
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
    EXPECT_TRUE(evaluator.get_delta().added.contains({ 6 }));
    EXPECT_EQ(evaluator.get_delta().removed.size(), 1);
    EXPECT_TRUE(evaluator.get_delta().removed.contains({ 3 }));
    update(delta.removed, delta.added);
    EXPECT_EQ(rows<uint_t>(input), initial);
    // Removing the restored input checks every consolidated support count,
    // including unchanged membership and rows moved by compact erasure.
    Builder<Relation<>> empty(input.columns());
    Builder<Relation<>> all(input.columns());
    for (size_t i = 0; i < input.size(); ++i)
        all.insert(input.row(i));
    update(empty, all);
    EXPECT_TRUE(evaluator.get_result().empty());
    update(all, empty);
    EXPECT_EQ(rows<uint_t>(input), initial);
}

TEST(YggdrasilTests, DatabaseIncrementalProjectionDoesNotHideRemovalUnderflowWithAdditions)
{
    Builder<Relation<>> input({ ColumnIndex(1), ColumnIndex(2) });
    input.insert({ 7, 10 });
    ProjectionEvaluator<uint_t> evaluator(ProjectionPlan(input.columns(), { ColumnIndex(1) }));
    Workspace<> workspace;
    evaluator.initialize(input, workspace);
    Delta<uint_t> invalid(input.columns());
    invalid.removed.insert({ 7, 10 });
    invalid.removed.insert({ 7, 11 }); // Only one existing witness is available.
    invalid.added.insert({ 7, 12 });
    invalid.added.insert({ 7, 13 }); // Net support change is zero, but removal is invalid.
    EXPECT_THROW(evaluator.update(invalid.added, invalid.removed, workspace), std::invalid_argument);
    Builder<Relation<>> empty(input.columns());
    EXPECT_THROW(evaluator.update(empty, empty, workspace), std::logic_error);
    evaluator.initialize(input, workspace);
    Delta<uint_t> valid(input.columns());
    valid.removed.insert({ 7, 10 });
    valid.added.insert({ 7, 12 });
    evaluator.update(valid.added, valid.removed, workspace);
    EXPECT_TRUE(evaluator.get_result().contains({ 7 }));
    EXPECT_TRUE(evaluator.get_delta().added.empty());
    EXPECT_TRUE(evaluator.get_delta().removed.empty());
    evaluator.update(empty, valid.added, workspace);
    EXPECT_TRUE(evaluator.get_result().empty());
    EXPECT_TRUE(evaluator.get_delta().removed.contains({ 7 }));
}

TEST(YggdrasilTests, DatabaseIncrementalProjectionHandlesEmptyAndNullaryRelations)
{
    Builder<Relation<>> input({ ColumnIndex(4) });
    const ProjectionPlan plan(input.columns(), {});
    ProjectionEvaluator<uint_t> evaluator(plan);
    Workspace<> workspace;
    Builder<Relation<>> reference;
    evaluator.initialize(input, workspace);
    EXPECT_TRUE(evaluator.get_result().empty());
    Delta<uint_t> delta(input.columns());
    delta.added.insert({ 1 });
    delta.added.insert({ 2 });
    const auto update = [&]
    {
        const auto before = rows<uint_t>(reference);
        apply_batch(input, delta.added, delta.removed);
        evaluator.update(delta.added, delta.removed, workspace);
        project(input, plan, reference, workspace);
        expect_update(evaluator, before, reference);
    };
    update();
    EXPECT_TRUE(evaluator.get_delta().added.contains({}));
    delta.clear();
    delta.removed.insert({ 1 });
    update();
    EXPECT_TRUE(evaluator.get_delta().removed.empty());
    delta.clear();
    delta.removed.insert({ 2 });
    update();
    EXPECT_TRUE(evaluator.get_delta().removed.contains({}));
    delta.clear();
    update();
    EXPECT_TRUE(evaluator.get_delta().added.empty());
    EXPECT_TRUE(evaluator.get_delta().removed.empty());

    Builder<Relation<>> nullary;
    Delta<uint_t> nullary_delta({});
    ProjectionEvaluator<uint_t> identity(ProjectionPlan({}, {}));
    identity.initialize(nullary, workspace);
    nullary_delta.added.insert({});
    identity.update(nullary_delta.added, nullary_delta.removed, workspace);
    EXPECT_TRUE(identity.get_result().contains({}));
    EXPECT_TRUE(identity.get_delta().added.contains({}));
    identity.update(nullary_delta.removed, nullary_delta.added, workspace);
    EXPECT_TRUE(identity.get_result().empty());
    EXPECT_TRUE(identity.get_delta().removed.contains({}));
}

TEST(YggdrasilTests, DatabaseIncrementalJoinUpdatesEitherSideIndependently)
{
    Builder<Relation<>> lhs({ ColumnIndex(7), ColumnIndex(2), ColumnIndex(9) });
    Builder<Relation<>> rhs({ ColumnIndex(9), ColumnIndex(11), ColumnIndex(2) });
    lhs.insert({ 1, 2, 3 });
    lhs.insert({ 4, 2, 3 });
    lhs.insert({ 5, 8, 3 });
    rhs.insert({ 3, 20, 2 });
    rhs.insert({ 3, 21, 2 });
    rhs.insert({ 3, 22, 8 });
    rhs.insert({ 9, 23, 2 });
    const JoinPlan plan(lhs.columns(), rhs.columns());
    auto repository = RelationRepositoryFactory<>().create();
    Workspace<> workspace;
    for (const bool change_left : { false, true })
    {
        auto& dynamic = change_left ? lhs : rhs;
        Builder<Relation<>> unchanged(change_left ? rhs.columns() : lhs.columns());
        JoinEvaluator<uint_t> evaluator(plan);
        Builder<Relation<>> reference(plan.output_columns());
        evaluator.initialize(make_view(lhs, repository), make_view(rhs, repository), workspace);
        join(lhs, rhs, plan, reference, workspace);
        EXPECT_EQ(rows<uint_t>(evaluator.get_result()), rows<uint_t>(reference));
        EXPECT_TRUE(evaluator.get_delta().added.empty());
        EXPECT_TRUE(evaluator.get_delta().removed.empty());
        Delta<uint_t> delta(dynamic.columns());
        delta.removed.insert(dynamic.row(0));
        if (!change_left)
            delta.added.insert({ 3, 30, 2 });
        else
            delta.added.insert({ 6, 2, 3 });
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

TEST(YggdrasilTests, DatabaseIncrementalJoinAcceptsInternedInputsWithCollidingKeys)
{
    using Value = IncrementalCollisionValue;
    Builder<Relation<Value>> dynamic({ ColumnIndex(1), ColumnIndex(2) });
    Builder<Relation<Value>> immutable({ ColumnIndex(2), ColumnIndex(3) });
    dynamic.insert({ Value { 1 }, Value { 10 } });
    immutable.insert({ Value { 10 }, Value { 100 } });
    immutable.insert({ Value { 20 }, Value { 200 } });
    immutable.insert({ Value { 30 }, Value { 300 } });
    auto repository = RelationRepositoryFactory<Value>().create();
    const auto view = database::insert(repository, immutable).first;
    const JoinPlan plan(dynamic.columns(), immutable.columns());
    JoinEvaluator<Value> evaluator(plan);
    Builder<Relation<Value>> unchanged(immutable.columns());
    Workspace<Value> workspace;
    Builder<Relation<Value>> reference(plan.output_columns());
    evaluator.initialize(dynamic, view, workspace);
    join(dynamic, immutable, plan, reference, workspace);
    EXPECT_EQ(rows<Value>(evaluator.get_result()), rows<Value>(reference));
    Delta<Value> delta(dynamic.columns());
    delta.removed.insert({ Value { 1 }, Value { 10 } });
    delta.added.insert({ Value { 2 }, Value { 20 } });
    delta.added.insert({ Value { 3 }, Value { 40 } });
    const auto before = rows<Value>(reference);
    apply_batch(dynamic, delta.added, delta.removed);
    evaluator.update(delta.added, delta.removed, unchanged, unchanged, workspace);
    join(dynamic, immutable, plan, reference, workspace);
    expect_update(evaluator, before, reference);
}

TEST(YggdrasilTests, DatabaseIncrementalProjectionValidationPrecedesMutation)
{
    Builder<Relation<>> input({ ColumnIndex(1) });
    input.insert({ 1 });
    Builder<Relation<>> empty(input.columns());
    Builder<Relation<>> wrong({ ColumnIndex(2) });
    Builder<Relation<>> overlap(input.columns());
    overlap.insert({ 1 });
    ProjectionEvaluator<uint_t> evaluator(ProjectionPlan(input.columns(), { ColumnIndex(1) }));
    Workspace<> workspace;
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
    Delta<uint_t> delta(input.columns());
    delta.added.insert({ 1 });
    delta.removed.insert({ 2 });
    const auto memory = delta.added.memory_usage() + delta.removed.memory_usage();
    delta.clear();
    EXPECT_TRUE(delta.added.empty());
    EXPECT_TRUE(delta.removed.empty());
    EXPECT_EQ(delta.added.memory_usage() + delta.removed.memory_usage(), memory);
}

TEST(YggdrasilTests, DatabaseIncrementalJoinUpdatesBothInputsAndRestoresBranches)
{
    Builder<Relation<>> lhs({ ColumnIndex(9), ColumnIndex(2), ColumnIndex(7) });
    Builder<Relation<>> rhs({ ColumnIndex(7), ColumnIndex(11), ColumnIndex(2) });
    lhs.insert({ 1, 10, 100 });
    lhs.insert({ 2, 10, 100 });
    lhs.insert({ 3, 20, 200 });
    lhs.insert({ 4, 30, 300 });
    rhs.insert({ 100, 7, 10 });
    rhs.insert({ 100, 8, 10 });
    rhs.insert({ 200, 7, 20 });
    rhs.insert({ 300, 9, 30 });
    const auto initial_lhs = rows<uint_t>(lhs);
    const auto initial_rhs = rows<uint_t>(rhs);
    const JoinPlan plan(lhs.columns(), rhs.columns());
    const ProjectionPlan projection_plan(plan.output_columns(), { ColumnIndex(11) });
    JoinEvaluator<uint_t> evaluator(plan);
    ProjectionEvaluator<uint_t> projected(projection_plan);
    Workspace<> workspace;
    auto repository = RelationRepositoryFactory<>().create();
    evaluator.initialize(make_view(lhs, repository), make_view(rhs, repository), workspace);
    projected.initialize(evaluator.get_result(), workspace);
    Builder<Relation<>> reference(plan.output_columns());
    Builder<Relation<>> reference_projection(projection_plan.output_columns());
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
    Delta<uint_t> lhs_a(lhs.columns());
    Delta<uint_t> rhs_a(rhs.columns());
    lhs_a.removed.insert({ 1, 10, 100 });
    lhs_a.removed.insert({ 3, 20, 200 });
    lhs_a.added.insert({ 5, 40, 400 });
    lhs_a.added.insert({ 6, 10, 100 });
    rhs_a.removed.insert({ 100, 8, 10 });
    rhs_a.removed.insert({ 200, 7, 20 });
    rhs_a.added.insert({ 400, 10, 40 });
    rhs_a.added.insert({ 100, 11, 10 });
    update(lhs_a.added, lhs_a.removed, rhs_a.added, rhs_a.removed);
    // New/new pairs must appear; old/new and new/old crossed pairs must not.
    EXPECT_TRUE(evaluator.get_delta().added.contains({ 5, 40, 400, 10 }));
    EXPECT_FALSE(evaluator.get_delta().added.contains({ 1, 10, 100, 11 }));
    EXPECT_FALSE(evaluator.get_delta().added.contains({ 6, 10, 100, 8 }));
    Delta<uint_t> lhs_b(lhs.columns());
    Delta<uint_t> rhs_b(rhs.columns());
    lhs_b.removed.insert({ 4, 30, 300 });
    lhs_b.added.insert({ 7, 40, 400 });
    rhs_b.removed.insert({ 300, 9, 30 });
    rhs_b.added.insert({ 400, 12, 40 });
    update(lhs_b.added, lhs_b.removed, rhs_b.added, rhs_b.removed);
    update(lhs_b.removed, lhs_b.added, rhs_b.removed, rhs_b.added);
    update(lhs_a.removed, lhs_a.added, rhs_a.removed, rhs_a.added);
    EXPECT_EQ(rows<uint_t>(lhs), initial_lhs);
    EXPECT_EQ(rows<uint_t>(rhs), initial_rhs);
    Delta<uint_t> sibling(lhs.columns());
    Delta<uint_t> unchanged(rhs.columns());
    sibling.removed.insert({ 2, 10, 100 });
    sibling.added.insert({ 8, 30, 300 });
    update(sibling.added, sibling.removed, unchanged.added, unchanged.removed);
    update(sibling.removed, sibling.added, unchanged.added, unchanged.removed);
    EXPECT_EQ(rows<uint_t>(lhs), initial_lhs);
    EXPECT_EQ(rows<uint_t>(rhs), initial_rhs);
    evaluator.initialize(lhs, rhs, workspace);
    EXPECT_EQ(rows<uint_t>(evaluator.get_result()), rows<uint_t>(reference));
    EXPECT_TRUE(evaluator.get_delta().added.empty());
    EXPECT_TRUE(evaluator.get_delta().removed.empty());
}

TEST(YggdrasilTests, DatabaseIncrementalJoinMaintainsCollidingIndexesAcrossRowCompaction)
{
    using Value = IncrementalCollisionValue;
    Builder<Relation<Value>> lhs({ ColumnIndex(1), ColumnIndex(2) });
    Builder<Relation<Value>> rhs({ ColumnIndex(2), ColumnIndex(3) });
    for (uint_t i = 0; i < 5; ++i)
    {
        lhs.insert({ Value { i }, Value { i % 3 } });
        rhs.insert({ Value { i % 3 }, Value { i } });
    }
    const JoinPlan plan(lhs.columns(), rhs.columns());
    JoinEvaluator<Value> evaluator(plan);
    Workspace<Value> workspace;
    Builder<Relation<Value>> reference(plan.output_columns());
    evaluator.initialize(lhs, rhs, workspace);
    join(lhs, rhs, plan, reference, workspace);
    EXPECT_EQ(rows<Value>(evaluator.get_result()), rows<Value>(reference));
    Delta<Value> lhs_delta(lhs.columns());
    Delta<Value> rhs_delta(rhs.columns());
    for (uint_t generation = 0; generation < 24; ++generation)
    {
        lhs_delta.clear();
        rhs_delta.clear();
        if (generation % 3 != 1)
        {
            lhs_delta.removed.insert(lhs.row(generation % lhs.size()));
            lhs_delta.added.insert({ Value { 100 + generation }, Value { generation % 3 } });
        }
        if (generation % 3 != 2)
        {
            rhs_delta.removed.insert(rhs.row((generation + 1) % rhs.size()));
            rhs_delta.added.insert({ Value { generation % 3 }, Value { 100 + generation } });
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
    Workspace<> workspace;
    for (unsigned nullary_sides = 0; nullary_sides < 4; ++nullary_sides)
    {
        Builder<Relation<>> lhs;
        Builder<Relation<>> rhs;
        if (!(nullary_sides & 1))
            lhs.initialize({ ColumnIndex(1) });
        if (!(nullary_sides & 2))
            rhs.initialize({ ColumnIndex(2) });
        const JoinPlan plan(lhs.columns(), rhs.columns());
        JoinEvaluator<uint_t> evaluator(plan);
        Builder<Relation<>> reference(plan.output_columns());
        evaluator.initialize(lhs, rhs, workspace);
        EXPECT_TRUE(evaluator.get_result().empty());
        Delta<uint_t> lhs_delta(lhs.columns());
        Delta<uint_t> rhs_delta(rhs.columns());
        if (lhs.arity() == 0)
            lhs_delta.added.insert({});
        else
        {
            lhs_delta.added.insert({ 1 });
            lhs_delta.added.insert({ 2 });
        }
        if (rhs.arity() == 0)
            rhs_delta.added.insert({});
        else
        {
            rhs_delta.added.insert({ 10 });
            rhs_delta.added.insert({ 20 });
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
        Delta<uint_t> lhs_remove(lhs.columns());
        Delta<uint_t> rhs_remove(rhs.columns());
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
    Builder<Relation<>> input({ ColumnIndex(1) });
    input.insert({ 1 });
    Builder<Relation<>> empty(input.columns());
    Builder<Relation<>> addition(input.columns());
    addition.insert({ 2 });
    Builder<Relation<>> wrong({ ColumnIndex(2) });
    JoinEvaluator<uint_t> evaluator(JoinPlan(input.columns(), input.columns()));
    Workspace<> workspace;
    EXPECT_THROW(evaluator.update(empty, empty, empty, empty, workspace), std::logic_error);
    evaluator.initialize(input, input, workspace);
    evaluator.update(addition, empty, addition, empty, workspace);
    EXPECT_TRUE(evaluator.get_delta().added.contains({ 2 }));
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
    Builder<Relation<>> input({ ColumnIndex(1) });
    input.insert({ 1 });
    Builder<Relation<>> empty(input.columns());
    Builder<Relation<>> addition(input.columns());
    addition.insert({ 2 });
    Builder<Relation<>> absent(input.columns());
    absent.insert({ 99 });
    JoinEvaluator<uint_t> evaluator(JoinPlan(input.columns(), input.columns()));
    Workspace<> workspace;
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
        EXPECT_TRUE(evaluator.get_result().contains({ 1 }));
        EXPECT_TRUE(evaluator.get_result().contains({ 2 }));
        EXPECT_EQ(rows<uint_t>(evaluator.get_delta().added), rows<uint_t>(addition));
        EXPECT_TRUE(evaluator.get_delta().removed.empty());
    }
}

}  // namespace ygg::tests
