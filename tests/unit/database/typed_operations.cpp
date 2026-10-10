/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/semantics/incremental/join.hpp"
#include "yggdrasil/database/semantics/incremental/projection.hpp"
#include "yggdrasil/database/semantics/relation_repository.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <gtest/gtest.h>
#include <tuple>

namespace ygg::tests
{
namespace
{
using namespace database;
using ColumnIndex = Index<Column>;
using RelationBuilder = Builder<Relation<>>;

void expect_same_rows(const RelationBuilder& actual, const RelationBuilder& expected)
{
    ASSERT_EQ(actual.size(), expected.size());
    for (size_t i = 0; i < expected.size(); ++i)
        EXPECT_TRUE(actual.contains(expected[i]));
}

void apply_changes(RelationBuilder& input, const incremental::Delta<>& delta, bool undo = false)
{
    const auto& removed = undo ? delta.added : delta.removed;
    const auto& added = undo ? delta.removed : delta.added;
    for (size_t i = 0; i < removed.size(); ++i)
        input.erase(input.find(removed[i]).value());
    for (size_t i = 0; i < added.size(); ++i)
        input.insert(added[i]);
}

void expect_delta(const incremental::Delta<>& actual, const RelationBuilder& before, const RelationBuilder& after)
{
    RelationBuilder added(after.columns().span());
    RelationBuilder removed(before.columns().span());
    difference(after, before, added);
    difference(before, after, removed);
    expect_same_rows(actual.added, added);
    expect_same_rows(actual.removed, removed);
}
}  // namespace

TEST(YggdrasilTests, DatabaseTypedProjectionConsolidatesWitnessesAndUndoesMixedChanges)
{
    Builder<Columns<>> columns;
    columns.push_back<uint_t>(ColumnIndex(1));
    columns.push_back<double>(ColumnIndex(2));
    columns.push_back<bool>(ColumnIndex(3));
    RelationBuilder input(columns.span());
    input.insert(std::tuple { uint_t(1), 1.5, true });
    input.insert(std::tuple { uint_t(2), 1.5, true });
    input.insert(std::tuple { uint_t(3), 2.5, false });

    const ProjectionPlan<> plan(columns.span(), { ColumnIndex(3), ColumnIndex(2) });
    incremental::ProjectionEvaluator<> evaluator(plan);
    Workspace<> workspace;
    evaluator.initialize(input, workspace);
    RelationBuilder initial(plan.output_columns().span());
    project(input, plan, initial, workspace);
    expect_same_rows(evaluator.get_result(), initial);
    ASSERT_EQ(initial.size(), 2);
    EXPECT_EQ(initial.columns()[1].offset, ColumnCodec<bool>::size);
    EXPECT_TRUE(initial.contains(std::tuple { true, 1.5 }));

    incremental::Delta<> changes(columns.span());
    changes.removed.insert(std::tuple { uint_t(1), 1.5, true });
    changes.removed.insert(std::tuple { uint_t(2), 1.5, true });
    changes.added.insert(std::tuple { uint_t(4), 1.5, true });
    changes.added.insert(std::tuple { uint_t(5), 3.5, false });
    evaluator.update(changes.change(), workspace);
    apply_changes(input, changes);
    RelationBuilder updated(plan.output_columns().span());
    project(input, plan, updated, workspace);
    expect_same_rows(evaluator.get_result(), updated);
    expect_delta(evaluator.get_delta(), initial, updated);
    EXPECT_TRUE(evaluator.get_delta().removed.empty());
    ASSERT_EQ(evaluator.get_delta().added.size(), 1);
    EXPECT_TRUE(evaluator.get_delta().added.contains(std::tuple { false, 3.5 }));

    evaluator.update(std::tie(changes.removed, changes.added), workspace);
    apply_changes(input, changes, true);
    expect_same_rows(evaluator.get_result(), initial);
    expect_delta(evaluator.get_delta(), updated, initial);
}

TEST(YggdrasilTests, DatabaseTypedJoinUpdatesBothInputsAndRepairsCompactedIndexes)
{
    Builder<Columns<>> left_columns;
    left_columns.push_back<uint_t>(ColumnIndex(1));
    left_columns.push_back<double>(ColumnIndex(2));
    left_columns.push_back<bool>(ColumnIndex(3));
    Builder<Columns<>> right_columns;
    right_columns.push_back<bool>(ColumnIndex(3));
    right_columns.push_back<uint_t>(ColumnIndex(1));
    right_columns.push_back<double>(ColumnIndex(4));
    RelationBuilder left(left_columns.span());
    RelationBuilder right(right_columns.span());
    left.insert(std::tuple { uint_t(1), 1.5, true });
    left.insert(std::tuple { uint_t(2), 2.5, false });
    left.insert(std::tuple { uint_t(3), 3.5, true });
    right.insert(std::tuple { true, uint_t(1), 10.0 });
    right.insert(std::tuple { false, uint_t(2), 20.0 });
    right.insert(std::tuple { true, uint_t(3), 30.0 });
    const JoinPlan<> plan(left_columns.span(), right_columns.span());
    incremental::JoinEvaluator<> evaluator(plan);
    Workspace<> workspace;
    evaluator.initialize(left, right, workspace);
    RelationBuilder initial(plan.output_columns().span());
    join(left, right, plan, initial, workspace);
    expect_same_rows(evaluator.get_result(), initial);
    ASSERT_EQ(initial.size(), 3);

    incremental::Delta<> left_changes(left_columns.span());
    incremental::Delta<> right_changes(right_columns.span());
    left_changes.removed.insert(std::tuple { uint_t(1), 1.5, true });
    left_changes.added.insert(std::tuple { uint_t(1), 1.75, true });
    left_changes.added.insert(std::tuple { uint_t(4), 4.5, false });
    right_changes.removed.insert(std::tuple { false, uint_t(2), 20.0 });
    right_changes.added.insert(std::tuple { false, uint_t(2), 22.0 });
    right_changes.added.insert(std::tuple { false, uint_t(4), 40.0 });
    evaluator.update(left_changes.change(), right_changes.change(), workspace);
    apply_changes(left, left_changes);
    apply_changes(right, right_changes);
    RelationBuilder updated(plan.output_columns().span());
    join(left, right, plan, updated, workspace);
    expect_same_rows(evaluator.get_result(), updated);
    expect_delta(evaluator.get_delta(), initial, updated);
    ASSERT_EQ(updated.size(), 4);
    EXPECT_TRUE(updated.contains(std::tuple { uint_t(1), 1.75, true, 10.0 }));
    EXPECT_TRUE(updated.contains(std::tuple { uint_t(2), 2.5, false, 22.0 }));
    EXPECT_TRUE(updated.contains(std::tuple { uint_t(4), 4.5, false, 40.0 }));
    EXPECT_EQ(evaluator.get_delta().added.size(), 3);
    EXPECT_EQ(evaluator.get_delta().removed.size(), 2);

    evaluator.update(std::tie(left_changes.removed, left_changes.added), std::tie(right_changes.removed, right_changes.added), workspace);
    apply_changes(left, left_changes, true);
    apply_changes(right, right_changes, true);
    expect_same_rows(evaluator.get_result(), initial);
    expect_delta(evaluator.get_delta(), updated, initial);
}

TEST(YggdrasilTests, DatabaseTypedConstantSelectionUsesCanonicalFloatEquality)
{
    Builder<Columns<>> columns;
    columns.push_back<double>(ColumnIndex(1));
    columns.push_back<uint_t>(ColumnIndex(2));
    RelationBuilder input(columns.span());
    const auto first_nan = std::bit_cast<double>(std::uint64_t { 0x7ff8000000000001 });
    const auto second_nan = std::bit_cast<double>(std::uint64_t { 0x7ff8000000000002 });
    input.insert(std::tuple { 0.0, uint_t(1) });
    input.insert(std::tuple { -0.0, uint_t(1) });
    input.insert(std::tuple { first_nan, uint_t(2) });
    input.insert(std::tuple { second_nan, uint_t(2) });
    ASSERT_EQ(input.size(), 2);
    RelationBuilder result(columns.span());
    select_equal_value(input, ColumnIndex(1), -0.0, result);
    ASSERT_EQ(result.size(), 1);
    EXPECT_TRUE(result.contains(std::tuple { 0.0, uint_t(1) }));
    select_equal_value(input, ColumnIndex(1), second_nan, result);
    ASSERT_EQ(result.size(), 1);
    EXPECT_TRUE(result.contains(std::tuple { first_nan, uint_t(2) }));
}

TEST(YggdrasilTests, DatabaseTypedSchemaErrorsPreserveOperatorAndIncrementalResults)
{
    Builder<Columns<>> columns;
    columns.push_back<uint_t>(ColumnIndex(1));
    columns.push_back<double>(ColumnIndex(2));
    Builder<Columns<>> incompatible;
    incompatible.push_back<double>(ColumnIndex(1));
    incompatible.push_back<double>(ColumnIndex(2));
    RelationBuilder input(columns.span());
    RelationBuilder wrong(incompatible.span());
    RelationBuilder output(columns.span());
    input.insert(std::tuple { uint_t(1), 1.5 });
    wrong.insert(std::tuple { 1.0, 1.5 });
    output.insert(std::tuple { uint_t(99), 9.5 });
    Workspace<> workspace;
    EXPECT_THROW(join(input, wrong, output, workspace), std::invalid_argument);
    EXPECT_THROW(union_(input, wrong, output), std::invalid_argument);
    EXPECT_THROW(difference(input, wrong, output), std::invalid_argument);
    EXPECT_THROW(select_equal_columns(input, ColumnIndex(1), ColumnIndex(2), output), std::invalid_argument);
    EXPECT_THROW(select_equal_value(input, ColumnIndex(1), 1.0, output), std::invalid_argument);
    ASSERT_EQ(output.size(), 1);
    EXPECT_TRUE(output.contains(std::tuple { uint_t(99), 9.5 }));

    const ProjectionPlan<> plan(columns.span(), { ColumnIndex(1) });
    incremental::ProjectionEvaluator<> evaluator(plan);
    evaluator.initialize(input, workspace);
    RelationBuilder empty_wrong(incompatible.span());
    EXPECT_THROW(evaluator.update(std::tie(wrong, empty_wrong), workspace), std::invalid_argument);
    EXPECT_TRUE(evaluator.get_result().contains(std::tuple { uint_t(1) }));
    EXPECT_TRUE(evaluator.get_delta().added.empty());
    EXPECT_TRUE(evaluator.get_delta().removed.empty());
    RelationBuilder empty(columns.span());
    EXPECT_NO_THROW(evaluator.update(std::tie(empty, empty), workspace));
}

TEST(YggdrasilTests, DatabaseTypedJoinCacheSeparatesSharedBytesByTypeAndLayout)
{
    RelationRepositoryFactory<> factory;
    auto repository = factory.create();
    JoinIndexCache<> cache;
    Builder<Columns<>> unsigned_columns;
    unsigned_columns.push_back<std::uint64_t>(ColumnIndex(1));
    Builder<Columns<>> float_columns;
    float_columns.push_back<double>(ColumnIndex(1));
    RelationBuilder unsigned_input(unsigned_columns.span());
    RelationBuilder float_input(float_columns.span());
    unsigned_input.insert(std::tuple { std::uint64_t(0) });
    float_input.insert(std::tuple { 0.0 });
    const auto unsigned_view = database::insert(repository, unsigned_input).first;
    const auto float_view = database::insert(repository, float_input).first;
    ASSERT_EQ(unsigned_view.get_storage_index(), float_view.get_storage_index());
    const ProjectionPlan<> unsigned_key(unsigned_columns.span(), { ColumnIndex(1) });
    const ProjectionPlan<> float_key(float_columns.span(), { ColumnIndex(1) });
    cache.get_or_create(unsigned_view, unsigned_key.positions());
    cache.get_or_create(float_view, float_key.positions());
    EXPECT_EQ(cache.size(), 2);
    const std::array renamed_label { ColumnIndex(9) };
    const auto renamed = repository.rename(unsigned_view, renamed_label);
    cache.get_or_create(renamed, unsigned_key.positions());
    EXPECT_EQ(cache.size(), 2);

    Builder<Columns<>> before_columns;
    before_columns.push_back<bool>(ColumnIndex(2));
    before_columns.push_back<uint_t>(ColumnIndex(1));
    Builder<Columns<>> after_columns;
    after_columns.push_back<uint_t>(ColumnIndex(1));
    after_columns.push_back<bool>(ColumnIndex(2));
    RelationBuilder before(before_columns.span());
    RelationBuilder after(after_columns.span());
    before.insert(std::tuple { false, uint_t(0) });
    after.insert(std::tuple { uint_t(0), false });
    const auto before_view = database::insert(repository, before).first;
    const auto after_view = database::insert(repository, after).first;
    ASSERT_EQ(before_view.get_storage_index(), after_view.get_storage_index());
    const ProjectionPlan<> before_key(before_columns.span(), { ColumnIndex(1) });
    const ProjectionPlan<> after_key(after_columns.span(), { ColumnIndex(1) });
    cache.get_or_create(before_view, before_key.positions());
    cache.get_or_create(after_view, after_key.positions());
    EXPECT_EQ(cache.size(), 4);
}
}  // namespace ygg::tests
