/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/semantics/incremental/generic_join.hpp"

#include "yggdrasil/database/semantics/incremental/projection.hpp"
#include "yggdrasil/database/semantics/relation_repository.hpp"

#include <array>
#include <bit>
#include <gtest/gtest.h>
#include <random>

namespace ygg::tests
{
struct GenericJoinValue
{
    uint_t value;
};
}  // namespace ygg::tests

namespace ygg::database
{
template<>
struct ColumnCodec<tests::GenericJoinValue>
{
    static constexpr size_t size = ColumnCodec<uint_t>::size;
    static void encode(tests::GenericJoinValue value, std::span<std::byte> bytes) { ColumnCodec<uint_t>::encode(value.value, bytes); }
    static tests::GenericJoinValue decode(std::span<const std::byte> bytes) { return { ColumnCodec<uint_t>::decode(bytes) }; }
};
}  // namespace ygg::database

namespace ygg::tests
{
namespace
{
using namespace database;
using Values = TypeList<uint_t>;
using RelationBuilder = Builder<Relation<Values>>;
using Change = incremental::Delta<Values>;
using ColumnIndex = Index<Column>;

// Borrowed views use a relation repository only as their context.
template<ColumnTypes Types>
BorrowedRelationView<Types> borrow(const Builder<Relation<Types>>& relation)
{
    static const auto context = RelationRepositoryFactory<Types> {}.create();
    return { relation, context };
}

static_assert(RelationViewConcept<BorrowedRelationView<Values>, Values>);

struct ThrowingRelation : BorrowedRelationView<Values>
{
    size_t fail_on_row;
    mutable size_t row_calls = 0;

    ThrowingRelation(const RelationBuilder& input, size_t fail_on) : BorrowedRelationView<Values>(borrow(input)), fail_on_row(fail_on) {}

    std::span<const std::byte> row(size_t position) const
    {
        if (++row_calls == fail_on_row)
            throw std::runtime_error("Injected relation read failure.");
        return BorrowedRelationView<Values>::row(position);
    }
};

template<ColumnTypes Types, RelationViewRange<Types> R>
GenericJoinPlan<Types>
plan_for(const R& inputs, std::initializer_list<ColumnIndex> variables, std::initializer_list<ColumnIndex> output)
{
    std::vector<std::vector<ColumnLayout>> schemas;
    for (const auto& input : inputs)
        schemas.emplace_back(input.columns().span().begin(), input.columns().span().end());
    return GenericJoinPlan<Types>(schemas, { variables.begin(), variables.size() }, { output.begin(), output.size() });
}

template<ColumnTypes Types, RelationViewConcept<Types> A, RelationViewConcept<Types> E>
void expect_same(const A& actual, const E& expected)
{
    ASSERT_TRUE(std::ranges::equal(actual.columns().span(), expected.columns().span()));
    ASSERT_EQ(actual.size(), expected.size());
    for (size_t row = 0; row < expected.size(); ++row)
        EXPECT_TRUE(actual.contains(Row<Types>(expected.row(row), expected.columns().span())));
}

template<ColumnTypes Types, RelationViewRange<Types> R>
Builder<Relation<Types>> binary_reference(const R& inputs, const GenericJoinPlan<Types>& plan)
{
    Builder<Relation<Types>> result;
    result.insert(std::tuple {});
    for (const auto input : inputs)
        result = join<Types>(result, input);
    std::vector<ColumnIndex> output;
    for (const auto column : plan.output_columns().span())
        output.push_back(column.label);
    return project<Types>(result, output);
}

void apply(RelationBuilder& input, const Change& change, bool undo = false)
{
    const auto& removed = undo ? change.added : change.removed;
    const auto& added = undo ? change.removed : change.added;
    for (size_t i = 0; i < removed.size(); ++i)
    {
        const auto position = input.find(removed.row(i));
        ASSERT_TRUE(position);
        input.erase(*position);
    }
    for (size_t i = 0; i < added.size(); ++i)
        input.insert(added.row(i));
}

void expect_update(const incremental::GenericJoinEvaluator<Values>& evaluator, const RelationBuilder& before, const RelationBuilder& after)
{
    expect_same<Values>(evaluator.get_result(), after);
    const auto added = difference<Values>(after, before);
    const auto removed = difference<Values>(before, after);
    expect_same<Values>(evaluator.get_delta().added, added);
    expect_same<Values>(evaluator.get_delta().removed, removed);
}
}  // namespace

TEST(YggdrasilTests, DatabaseGenericJoinTriangleMatchesBinaryForEveryVariableOrder)
{
    RelationBuilder r({ ColumnIndex(1), ColumnIndex(2) });
    RelationBuilder s({ ColumnIndex(2), ColumnIndex(3) });
    RelationBuilder t({ ColumnIndex(3), ColumnIndex(1) });
    for (uint_t i = 0; i < 12; ++i)
    {
        r.insert(std::tuple { uint_t(0), i });
        r.insert(std::tuple { i, uint_t(0) });
        s.insert(std::tuple { uint_t(0), i });
        s.insert(std::tuple { i, uint_t(0) });
        t.insert(std::tuple { uint_t(0), i });
        t.insert(std::tuple { i, uint_t(0) });
    }
    const std::array inputs { borrow(r), borrow(s), borrow(t) };
    std::array variables { ColumnIndex(1), ColumnIndex(2), ColumnIndex(3) };
    const std::array output { ColumnIndex(3), ColumnIndex(1), ColumnIndex(2) };
    std::vector<std::vector<ColumnLayout>> schemas;
    for (const auto input : inputs)
        schemas.emplace_back(input.columns().span().begin(), input.columns().span().end());
    do
    {
        const GenericJoinPlan<Values> plan(schemas, variables, output);
        GenericJoinWorkspace<Values> workspace(plan);
        RelationBuilder result(plan.output_columns().span());
        const auto expected = binary_reference<Values>(inputs, plan);
        generic_join<Values>(inputs, plan, result, workspace);
        expect_same<Values>(result, expected);
        EXPECT_EQ(result.size(), 34);
        generic_join<Values>(inputs, plan, result, workspace);
        expect_same<Values>(result, expected);
        EXPECT_GT(workspace.memory_usage(), 0);
    } while (std::ranges::next_permutation(variables).found);
}

#ifdef YGG_GENERIC_JOIN_INSTRUMENTATION
TEST(YggdrasilTests, DatabaseGenericJoinAvoidsQuadraticTriangleIntermediate)
{
    for (const uint_t count : { uint_t(8), uint_t(32), uint_t(128) })
    {
        SCOPED_TRACE(count);
        RelationBuilder r({ ColumnIndex(1), ColumnIndex(2) });
        RelationBuilder s({ ColumnIndex(2), ColumnIndex(3) });
        RelationBuilder t({ ColumnIndex(1), ColumnIndex(3) });
        for (uint_t i = 0; i < count; ++i)
        {
            r.insert(std::tuple { i, uint_t(0) });
            s.insert(std::tuple { uint_t(0), i });
            t.insert(std::tuple { i, i });
        }
        const auto intermediate = join<Values>(r, s);
        ASSERT_EQ(intermediate.size(), count * count);
        const auto expected = join<Values>(intermediate, t);
        ASSERT_EQ(expected.size(), count);

        const std::array inputs { borrow(r), borrow(s), borrow(t) };
        std::vector<std::vector<ColumnLayout>> schemas;
        for (const auto input : inputs)
            schemas.emplace_back(input.columns().span().begin(), input.columns().span().end());
        std::array variables { ColumnIndex(1), ColumnIndex(2), ColumnIndex(3) };
        const auto output = variables;
        do
        {
            const GenericJoinPlan<Values> plan(schemas, variables, output);
            GenericJoinWorkspace<Values> workspace(plan);
            RelationBuilder result(plan.output_columns().span());
            database::detail::generic_join_work = {};
            generic_join<Values>(inputs, plan, result, workspace);
            const auto work = database::detail::generic_join_work;
            expect_same<Values>(result, expected);
            // This family has linear extension intersections in every order.
            // Counts cover traversal only; building the three tries reads 3n rows.
            EXPECT_GE(work.candidate_keys, 2 * count);
            EXPECT_LE(work.candidate_keys, 3 * count);
            EXPECT_LE(work.probes, 3 * count);
            EXPECT_LE(work.prefixes, 3 * count + 1);
        } while (std::ranges::next_permutation(variables).found);
    }
}
#endif

TEST(YggdrasilTests, DatabaseGenericJoinHandlesDisconnectedAndNullaryInputs)
{
    RelationBuilder a({ ColumnIndex(1) });
    RelationBuilder b({ ColumnIndex(2) });
    RelationBuilder truth;
    a.insert(std::tuple { uint_t(1) });
    a.insert(std::tuple { uint_t(2) });
    b.insert(std::tuple { uint_t(3) });
    b.insert(std::tuple { uint_t(4) });
    truth.insert(std::tuple {});
    const std::array inputs { borrow(a), borrow(b), borrow(truth) };
    const auto plan = plan_for<Values>(inputs, { ColumnIndex(2), ColumnIndex(1) }, { ColumnIndex(1), ColumnIndex(2) });
    GenericJoinWorkspace<Values> workspace(plan);
    RelationBuilder result(plan.output_columns().span());
    generic_join<Values>(inputs, plan, result, workspace);
    EXPECT_EQ(result.size(), 4);
    truth.clear();
    generic_join<Values>(inputs, plan, result, workspace);
    EXPECT_TRUE(result.empty());

    const auto empty_plan = plan_for<Values>(std::array<BorrowedRelationView<Values>, 0> {}, {}, {});
    GenericJoinWorkspace<Values> empty_workspace(empty_plan);
    RelationBuilder empty_result;
    generic_join<Values>(std::array<BorrowedRelationView<Values>, 0> {}, empty_plan, empty_result, empty_workspace);
    EXPECT_EQ(empty_result.size(), 1);
    EXPECT_TRUE(empty_result.contains(std::tuple {}));
    const std::array nullary_inputs { borrow(truth), borrow(truth) };
    const auto nullary_plan = plan_for<Values>(nullary_inputs, {}, {});
    incremental::GenericJoinEvaluator<Values> evaluator(nullary_plan);
    evaluator.initialize(nullary_inputs);
    Change change({});
    change.added.insert(std::tuple {});
    const std::array changes { std::pair { borrow(change.added), borrow(change.removed) }, std::pair { borrow(change.added), borrow(change.removed) } };
    evaluator.update(changes);
    EXPECT_EQ(evaluator.get_delta().added.size(), 1);
    EXPECT_EQ(evaluator.get_result().size(), 1);
    const std::array undo { std::pair { borrow(change.removed), borrow(change.added) }, std::pair { borrow(change.removed), borrow(change.added) } };
    evaluator.update(undo);
    EXPECT_TRUE(evaluator.get_result().empty());
    EXPECT_EQ(evaluator.get_delta().removed.size(), 1);
}

TEST(YggdrasilTests, DatabaseGenericJoinUsesCanonicalTypedBytesWithoutValueComparators)
{
    using Types = TypeList<GenericJoinValue, double, bool>;
    Builder<Columns<Types>> left_schema;
    left_schema.push_back<GenericJoinValue>(ColumnIndex(1));
    left_schema.push_back<double>(ColumnIndex(2));
    Builder<Columns<Types>> right_schema;
    right_schema.push_back<double>(ColumnIndex(2));
    right_schema.push_back<bool>(ColumnIndex(3));
    Builder<Relation<Types>> left(left_schema.span());
    Builder<Relation<Types>> right(right_schema.span());
    const auto nan = std::bit_cast<double>(std::uint64_t { 0x7ff8000000000001 });
    const auto other_nan = std::bit_cast<double>(std::uint64_t { 0x7ff8000000000002 });
    left.insert(std::tuple { GenericJoinValue { 4 }, -0.0 });
    left.insert(std::tuple { GenericJoinValue { 5 }, nan });
    right.insert(std::tuple { 0.0, false });
    right.insert(std::tuple { other_nan, true });
    const std::array inputs { borrow(left), borrow(right) };
    const auto plan = plan_for<Types>(inputs, { ColumnIndex(2), ColumnIndex(3), ColumnIndex(1) }, { ColumnIndex(3), ColumnIndex(1), ColumnIndex(2) });
    GenericJoinWorkspace<Types> workspace(plan);
    Builder<Relation<Types>> result(plan.output_columns().span());
    generic_join<Types>(inputs, plan, result, workspace);
    const auto expected = binary_reference<Types>(inputs, plan);
    expect_same<Types>(result, expected);
    ASSERT_EQ(result.size(), 2);
    EXPECT_TRUE(result.contains(std::tuple { false, GenericJoinValue { 4 }, 0.0 }));
    incremental::GenericJoinEvaluator<Types> evaluator(plan);
    evaluator.initialize(inputs);
    incremental::Delta<Types> left_change(left_schema.span());
    incremental::Delta<Types> right_change(right_schema.span());
    left_change.removed.insert(std::tuple { GenericJoinValue { 4 }, 0.0 });
    left_change.added.insert(std::tuple { GenericJoinValue { 6 }, -0.0 });
    right_change.removed.insert(std::tuple { nan, true });
    const std::array changes { std::pair { borrow(left_change.added), borrow(left_change.removed) }, std::pair { borrow(right_change.added), borrow(right_change.removed) } };
    evaluator.update(changes);
    ASSERT_EQ(evaluator.get_result().size(), 1);
    EXPECT_TRUE(evaluator.get_result().contains(std::tuple { false, GenericJoinValue { 6 }, 0.0 }));
    const std::array undo { std::pair { borrow(left_change.removed), borrow(left_change.added) }, std::pair { borrow(right_change.removed), borrow(right_change.added) } };
    evaluator.update(undo);
    expect_same<Types>(evaluator.get_result(), expected);
}

TEST(YggdrasilTests, DatabaseGenericJoinIncrementalConsolidatesMixedAliasChangesAndUndo)
{
    RelationRepositoryFactory<Values> factory;
    auto repository = factory.create();
    RelationBuilder edge({ ColumnIndex(1), ColumnIndex(2) });
    edge.insert(std::tuple { uint_t(1), uint_t(2) });
    edge.insert(std::tuple { uint_t(2), uint_t(3) });
    edge.insert(std::tuple { uint_t(3), uint_t(1) });
    const auto first = database::insert(repository, edge).first;
    const std::array labels { ColumnIndex(2), ColumnIndex(3) };
    const auto second = repository.rename(first, labels);
    ASSERT_EQ(first.get_storage_address(), second.get_storage_address());
    const std::array inputs { first, second, first };
    const auto plan = plan_for<Values>(inputs, { ColumnIndex(2), ColumnIndex(1), ColumnIndex(3) }, { ColumnIndex(1), ColumnIndex(2), ColumnIndex(3) });
    incremental::GenericJoinEvaluator<Values> evaluator(plan);
    evaluator.initialize(inputs);
    const auto before = binary_reference<Values>(inputs, plan);
    Change change(edge.columns().span());
    change.removed.insert(std::tuple { uint_t(1), uint_t(2) });
    change.added.insert(std::tuple { uint_t(4), uint_t(2) });
    change.added.insert(std::tuple { uint_t(2), uint_t(4) });
    Change renamed_change(second.columns().span());
    for (size_t i = 0; i < change.added.size(); ++i)
        renamed_change.added.insert(change.added.row(i));
    for (size_t i = 0; i < change.removed.size(); ++i)
        renamed_change.removed.insert(change.removed.row(i));
    const std::array changes { std::pair { borrow(change.added), borrow(change.removed) },
                               std::pair { borrow(renamed_change.added), borrow(renamed_change.removed) },
                               std::pair { borrow(change.added), borrow(change.removed) } };
    evaluator.update(changes);
    apply(edge, change);
    RelationBuilder renamed(labels);
    for (size_t i = 0; i < edge.size(); ++i)
        renamed.insert(edge.row(i));
    const std::array updated_inputs { borrow(edge), borrow(renamed), borrow(edge) };
    const auto after = binary_reference<Values>(updated_inputs, plan);
    expect_update(evaluator, before, after);
    const std::array undo { std::pair { borrow(change.removed), borrow(change.added) },
                            std::pair { borrow(renamed_change.removed), borrow(renamed_change.added) },
                            std::pair { borrow(change.removed), borrow(change.added) } };
    evaluator.update(undo);
    expect_update(evaluator, after, before);
    EXPECT_GT(evaluator.memory_usage(), 0);
}

TEST(YggdrasilTests, DatabaseGenericJoinIncrementalAvoidsTransientCrossedMatches)
{
    RelationBuilder left({ ColumnIndex(1) });
    RelationBuilder right({ ColumnIndex(1) });
    left.insert(std::tuple { uint_t(1) });
    right.insert(std::tuple { uint_t(2) });
    const std::array inputs { borrow(left), borrow(right) };
    const auto plan = plan_for<Values>(inputs, { ColumnIndex(1) }, { ColumnIndex(1) });
    incremental::GenericJoinEvaluator<Values> evaluator(plan);
    evaluator.initialize(inputs);
    Change a(left.columns().span());
    Change b(right.columns().span());
    a.removed.insert(std::tuple { uint_t(1) });
    a.added.insert(std::tuple { uint_t(2) });
    b.removed.insert(std::tuple { uint_t(2) });
    b.added.insert(std::tuple { uint_t(3) });
    const std::array changes { std::pair { borrow(a.added), borrow(a.removed) }, std::pair { borrow(b.added), borrow(b.removed) } };
    evaluator.update(changes);
    EXPECT_TRUE(evaluator.get_result().empty());
    EXPECT_TRUE(evaluator.get_delta().added.empty());
    EXPECT_TRUE(evaluator.get_delta().removed.empty());
}

TEST(YggdrasilTests, DatabaseGenericJoinIncrementalProjectionPreservesReplacedWitnesses)
{
    RelationBuilder left({ ColumnIndex(1), ColumnIndex(2) });
    RelationBuilder right({ ColumnIndex(2), ColumnIndex(3) });
    left.insert(std::tuple { uint_t(1), uint_t(10) });
    right.insert(std::tuple { uint_t(10), uint_t(9) });
    const std::array inputs { borrow(left), borrow(right) };
    const auto plan = plan_for<Values>(inputs, { ColumnIndex(2), ColumnIndex(1), ColumnIndex(3) }, { ColumnIndex(1), ColumnIndex(2), ColumnIndex(3) });
    incremental::GenericJoinEvaluator<Values> evaluator(plan);
    evaluator.initialize(inputs);
    incremental::ProjectionEvaluator<Values> projected(ProjectionPlan<Values>(plan.output_columns().span(), { ColumnIndex(3) }));
    Workspace<Values> workspace;
    projected.initialize(evaluator.get_result(), workspace);
    Change a(left.columns().span());
    Change b(right.columns().span());
    a.removed.insert(std::tuple { uint_t(1), uint_t(10) });
    a.added.insert(std::tuple { uint_t(2), uint_t(10) });
    const std::array changes { std::pair { borrow(a.added), borrow(a.removed) }, std::pair { borrow(b.added), borrow(b.removed) } };
    evaluator.update(changes);
    projected.update(evaluator.get_delta().added, evaluator.get_delta().removed, workspace);
    EXPECT_EQ(projected.get_result().size(), 1);
    EXPECT_TRUE(projected.get_delta().added.empty());
    EXPECT_TRUE(projected.get_delta().removed.empty());
}

TEST(YggdrasilTests, DatabaseGenericJoinIncrementalRandomBatchesMatchFullReevaluation)
{
    std::array<RelationBuilder, 3> relations { RelationBuilder({ ColumnIndex(1), ColumnIndex(2) }),
                                               RelationBuilder({ ColumnIndex(2), ColumnIndex(3) }),
                                               RelationBuilder({ ColumnIndex(3), ColumnIndex(1) }) };
    const std::array inputs { borrow(relations[0]), borrow(relations[1]), borrow(relations[2]) };
    const auto plan = plan_for<Values>(inputs, { ColumnIndex(2), ColumnIndex(3), ColumnIndex(1) }, { ColumnIndex(3), ColumnIndex(2), ColumnIndex(1) });
    incremental::GenericJoinEvaluator<Values> evaluator(plan);
    evaluator.initialize(inputs);
    std::mt19937 random(42);
    for (size_t batch = 0; batch < 50; ++batch)
    {
        const auto before = binary_reference<Values>(inputs, plan);
        std::array<Change, 3> changes { Change(relations[0].columns().span()), Change(relations[1].columns().span()), Change(relations[2].columns().span()) };
        for (size_t input = 0; input < relations.size(); ++input)
            for (uint_t a = 0; a < 4; ++a)
                for (uint_t b = 0; b < 4; ++b)
                    if (random() % 5 == 0)
                    {
                        const auto row = std::tuple { a, b };
                        if (relations[input].contains(row))
                            changes[input].removed.insert(row);
                        else
                            changes[input].added.insert(row);
                    }
        const std::array refs { std::pair { borrow(changes[0].added), borrow(changes[0].removed) },
                                std::pair { borrow(changes[1].added), borrow(changes[1].removed) },
                                std::pair { borrow(changes[2].added), borrow(changes[2].removed) } };
        evaluator.update(refs);
        for (size_t input = 0; input < relations.size(); ++input)
            apply(relations[input], changes[input]);
        const auto after = binary_reference<Values>(inputs, plan);
        expect_update(evaluator, before, after);
        if (batch % 3 == 0)
        {
            const std::array undo { std::pair { borrow(changes[0].removed), borrow(changes[0].added) },
                                    std::pair { borrow(changes[1].removed), borrow(changes[1].added) },
                                    std::pair { borrow(changes[2].removed), borrow(changes[2].added) } };
            evaluator.update(undo);
            for (size_t input = 0; input < relations.size(); ++input)
                apply(relations[input], changes[input], true);
            expect_update(evaluator, after, before);
        }
    }
}

TEST(YggdrasilTests, DatabaseGenericJoinValidatesPlansInputsAndFailureRecovery)
{
    RelationBuilder input({ ColumnIndex(1) });
    input.insert(std::tuple { uint_t(1) });
    const std::array inputs { borrow(input) };
    EXPECT_THROW(plan_for<Values>(inputs, {}, {}), std::invalid_argument);
    EXPECT_THROW(plan_for<Values>(inputs, { ColumnIndex(1) }, {}), std::invalid_argument);
    EXPECT_THROW(plan_for<Values>(inputs, { ColumnIndex(2) }, { ColumnIndex(1) }), std::invalid_argument);
    RelationBuilder second({ ColumnIndex(2) });
    const std::array two_inputs { borrow(input), borrow(second) };
    EXPECT_THROW((plan_for<Values>(two_inputs, { ColumnIndex(1), ColumnIndex(1) }, { ColumnIndex(1), ColumnIndex(2) })), std::invalid_argument);
    const auto plan = plan_for<Values>(inputs, { ColumnIndex(1) }, { ColumnIndex(1) });
    GenericJoinWorkspace<Values> workspace(plan);
    RelationBuilder output(plan.output_columns().span());
    output.insert(std::tuple { uint_t(99) });
    const std::array wrong { borrow(second) };
    EXPECT_THROW(generic_join<Values>(wrong, plan, output, workspace), std::invalid_argument);
    EXPECT_TRUE(output.contains(std::tuple { uint_t(99) }));
    EXPECT_THROW(generic_join<Values>(inputs, plan, input, workspace), std::invalid_argument);
    EXPECT_EQ(input.size(), 1);
    incremental::GenericJoinEvaluator<Values> evaluator(plan);
    Change delta(input.columns().span());
    const std::array change { std::pair { borrow(delta.added), borrow(delta.removed) } };
    EXPECT_THROW(evaluator.update(change), std::logic_error);
    EXPECT_THROW(evaluator.get_result(), std::logic_error);
    EXPECT_THROW(evaluator.get_delta(), std::logic_error);
    evaluator.initialize(inputs);
    const std::array wrong_changes { std::pair { borrow(second), borrow(second) } };
    EXPECT_THROW(evaluator.update(wrong_changes), std::invalid_argument);
    expect_same<Values>(evaluator.get_result(), input);
    delta.added.insert(std::tuple { uint_t(2) });
    delta.removed.insert(std::tuple { uint_t(2) });
    EXPECT_THROW(evaluator.update(change), std::invalid_argument);
    expect_same<Values>(evaluator.get_result(), input);
    delta.added.clear();
    EXPECT_THROW(evaluator.update(change), std::invalid_argument);
    expect_same<Values>(evaluator.get_result(), input);
    EXPECT_THROW(evaluator.update(change), std::invalid_argument);
    delta.clear();
    EXPECT_NO_THROW(evaluator.update(change));
    expect_same<Values>(evaluator.get_result(), input);
    const std::array aliased { std::pair { borrow(evaluator.get_result()), borrow(delta.removed) } };
    EXPECT_THROW(evaluator.update(aliased), std::invalid_argument);
    expect_same<Values>(evaluator.get_result(), input);
    auto moved = std::move(evaluator);
    EXPECT_NO_THROW(moved.update(change));
    expect_same<Values>(moved.get_result(), input);
}

TEST(YggdrasilTests, DatabaseGenericJoinRejectsInvalidBatchesBeforeMutationAndGuardsFailedUpdates)
{
    RelationBuilder input({ ColumnIndex(1) });
    input.insert(std::tuple { uint_t(1) });
    const std::array inputs { borrow(input), borrow(input) };
    const auto plan = plan_for<Values>(inputs, { ColumnIndex(1) }, { ColumnIndex(1) });
    incremental::GenericJoinEvaluator<Values> evaluator(plan);
    evaluator.initialize(inputs);
    Change first(input.columns().span()), second(input.columns().span());
    first.removed.insert(std::tuple { uint_t(1) });
    second.removed.insert(std::tuple { uint_t(99) });
    const std::array invalid { std::pair { borrow(first.added), borrow(first.removed) }, std::pair { borrow(second.added), borrow(second.removed) } };
    EXPECT_THROW(evaluator.update(invalid), std::invalid_argument);
    expect_same<Values>(evaluator.get_result(), input);
    EXPECT_TRUE(evaluator.get_delta().added.empty());
    EXPECT_TRUE(evaluator.get_delta().removed.empty());

    // The late invalid slot must not have removed the first slot's row.
    second.clear();
    EXPECT_NO_THROW(evaluator.update(invalid));
    EXPECT_TRUE(evaluator.get_result().empty());
    expect_same<Values>(evaluator.get_delta().removed, input);

    const std::array single_input { borrow(input) };
    const auto single_plan = plan_for<Values>(single_input, { ColumnIndex(1) }, { ColumnIndex(1) });
    incremental::GenericJoinEvaluator<Values> failing(single_plan);
    failing.initialize(single_input);
    Change insertion(input.columns().span());
    insertion.added.insert(std::tuple { uint_t(2) });
    // Validation and the delta trie read successfully; mutation then fails after
    // the new output has already been appended to the internal delta relation.
    const ThrowingRelation unstable(insertion.added, 4);
    const std::array broken { std::pair<ThrowingRelation, BorrowedRelationView<Values>> { unstable, borrow(insertion.removed) } };
    EXPECT_THROW(failing.update(broken), std::runtime_error);
    EXPECT_THROW(failing.get_result(), std::logic_error);
    EXPECT_THROW(failing.get_delta(), std::logic_error);
    EXPECT_THROW(failing.update(broken), std::logic_error);
    failing.initialize(single_input);
    const std::array valid { std::pair { borrow(insertion.added), borrow(insertion.removed) } };
    failing.update(valid);
    EXPECT_EQ(failing.get_result().size(), 2);
    expect_same<Values>(failing.get_delta().added, insertion.added);
}
}  // namespace ygg::tests
