#include <algorithm>
#include <array>
#include <cista/serialization.h>
#include <gtest/gtest.h>
#include <memory>
#include <random>
#include <vector>
#include <yggdrasil/database/semantics/incremental/query.hpp>
#include <yggdrasil/database/semantics/query_evaluation.hpp>
#include <yggdrasil/database/syntax/query_repository.hpp>
#include <yggdrasil/database/semantics/relation_repository.hpp>
#include "query_helpers.hpp"

namespace qb = ygg::tests::qb;

namespace
{
namespace db = ygg::database;
using Column = ygg::Index<db::Column>;
using Relation = ygg::Builder<db::Relation<>>;
using Delta = db::incremental::Delta<>;
using Change = std::pair<db::BorrowedRelationView<>, db::BorrowedRelationView<>>;

// Borrowed views use a relation repository only as their context.
template<db::ColumnTypes Values>
db::BorrowedRelationView<Values> borrow(const ygg::Builder<db::Relation<Values>>& relation)
{
    static const auto context = db::RelationRepositoryFactory<Values> {}.create();
    return { relation, context };
}
const Column x(0), y(1), z(2), d(3);

auto rows(const auto& relation)
{
    std::vector<std::vector<std::byte>> result;
    for (size_t i = 0; i < relation.size(); ++i)
        result.emplace_back(relation.row(i).begin(), relation.row(i).end());
    std::ranges::sort(result);
    return result;
}
auto minus(const auto& lhs, const auto& rhs)
{
    std::remove_cvref_t<decltype(lhs)> result;
    std::ranges::set_difference(lhs, rhs, std::back_inserter(result));
    return result;
}
void apply(Relation& relation, const Delta& change)
{
    for (size_t i = 0; i < change.removed.size(); ++i)
        relation.erase(*relation.find(change.removed.row(i)));
    for (size_t i = 0; i < change.added.size(); ++i)
        relation.insert(change.added.row(i));
}
void reverse(Delta& change) { std::swap(change.added, change.removed); }

template<typename T>
concept HasArgument = requires(T view) { view.get_arg(); };
template<typename T>
concept HasOperands = requires(T view) {
    view.get_lhs();
    view.get_rhs();
};
template<typename T>
concept HasSources = requires(T view) { view.get_sources(); };
using JoinView = db::QueryView<db::DefaultColumnTypes, db::QueryJoinTag>;
using ProjectView = db::QueryView<db::DefaultColumnTypes, db::QueryProjectTag>;
using DistanceView = db::QueryView<db::DefaultColumnTypes, db::QueryDistanceTag>;
static_assert(HasOperands<JoinView> && !HasArgument<JoinView> && !HasSources<JoinView>);
static_assert(HasArgument<ProjectView> && !HasOperands<ProjectView>);
static_assert(HasSources<DistanceView> && !HasArgument<DistanceView> && !HasOperands<DistanceView>);
static_assert(!HasArgument<db::QueryView<>> && !HasOperands<db::QueryView<>>);
static_assert(!std::copy_constructible<db::QueryRepository<>>);
static_assert(
    !std::convertible_to<ygg::Index<db::Query<db::DefaultColumnTypes, db::QueryInputTag>>, ygg::Index<db::Query<db::DefaultColumnTypes, db::QueryJoinTag>>>);

template<typename Tag>
auto concrete(db::QueryView<> query)
{
    return query.get_variant().template get<ygg::Index<db::Query<db::DefaultColumnTypes, Tag>>>();
}

TEST(DatabaseQuery, TypedLogicalAlternativesKeepOperandsAndRootProvenance)
{
    auto repository = qb::repository<>();
    const auto a = qb::input(repository, 0, { x, y });
    const auto b = qb::input(repository, 1, { y, z });
    const auto empty = qb::empty(repository, { x, y });
    const auto joined = qb::join(repository, a, b);
    const auto projected = qb::project(repository, a, { x });
    const auto renamed = qb::rename(repository, a, { y, z });
    const auto equal = qb::select_equal(repository, a, x, y);
    const auto selected = qb::select_value(repository, a, x, ygg::uint_t(7));
    const auto united = qb::union_(repository, a, empty);
    const auto difference = qb::difference(repository, a, empty);
    const auto targets = qb::project(repository, b, { z });
    const auto distance = qb::distance(repository, projected, a, targets, d);
    const auto roots = std::array { a, empty, joined, projected, renamed, equal, selected, united, difference, distance };
    EXPECT_EQ(concrete<db::QueryInputTag>(a).get_input_slot(), 0);
    EXPECT_TRUE(std::ranges::equal(concrete<db::QueryEmptyTag>(empty).columns(), a.columns()));
    EXPECT_EQ(concrete<db::QueryJoinTag>(joined).get_lhs().get_index(), a.get_index());
    EXPECT_EQ(concrete<db::QueryJoinTag>(joined).get_rhs().get_index(), b.get_index());
    EXPECT_EQ(concrete<db::QueryProjectTag>(projected).get_arg().get_index(), a.get_index());
    EXPECT_EQ(concrete<db::QueryProjectTag>(projected).get_labels().front(), x);
    EXPECT_EQ(concrete<db::QueryRenameTag>(renamed).get_labels().front(), y);
    EXPECT_EQ(concrete<db::QuerySelectEqualTag>(equal).get_lhs_column(), x);
    EXPECT_EQ(concrete<db::QuerySelectEqualTag>(equal).get_rhs_column(), y);
    EXPECT_EQ(concrete<db::QuerySelectValueTag>(selected).get_column(), x);
    EXPECT_EQ(db::ColumnCodec<ygg::uint_t>::decode(concrete<db::QuerySelectValueTag>(selected).get_constant()), 7);
    EXPECT_EQ(concrete<db::QueryUnionTag>(united).get_rhs().get_index(), empty.get_index());
    EXPECT_EQ(concrete<db::QueryDifferenceTag>(difference).get_lhs().get_index(), a.get_index());
    EXPECT_EQ(concrete<db::QueryDistanceTag>(distance).get_sources().get_index(), projected.get_index());
    EXPECT_EQ(concrete<db::QueryDistanceTag>(distance).get_edges().get_index(), a.get_index());
    EXPECT_EQ(concrete<db::QueryDistanceTag>(distance).get_targets().get_index(), targets.get_index());
    EXPECT_EQ(concrete<db::QueryDistanceTag>(distance).get_distance_column(), d);
    const auto count = repository.size();
    EXPECT_EQ(count, 12);
    const auto duplicate = qb::join(repository, a, b);
    EXPECT_EQ(duplicate.get_index(), joined.get_index());
    EXPECT_EQ(concrete<db::QueryJoinTag>(duplicate).get_index(), concrete<db::QueryJoinTag>(joined).get_index());
    EXPECT_EQ(repository.size(), count);
    EXPECT_EQ(concrete<db::QueryInputTag>(a).get_index().get_value(), concrete<db::QueryJoinTag>(joined).get_index().get_value());
    EXPECT_NE(a.get_index(), joined.get_index());
    const auto plan = ygg::database::compile(roots);
    for (size_t i = 0; i < roots.size(); ++i)
    {
        ygg::visit([&](auto operation) { EXPECT_TRUE(std::ranges::equal(operation.columns(), roots[i].columns())); }, roots[i].get_variant());
        EXPECT_TRUE(std::ranges::equal(plan[plan.roots()[i]].columns(), roots[i].columns()));
    }
}

TEST(DatabaseQuery, InternsValidatedNodesAndCompilesIndependentReachablePlan)
{
    db::QueryPlan<> plan;
    {
        auto repository = qb::repository<>();
        auto other = qb::repository<>();
        const auto a = qb::input(repository, 0, { x, y });
        EXPECT_EQ(a.get_index(), qb::input(repository, 0, { x, y }).get_index());
        const auto same = qb::input(repository, 0, { x, y });
        const auto foreign = qb::input(other, 0, { x, y });
        EXPECT_NE(repository.get_index(), other.get_index());
        EXPECT_TRUE(ygg::EqualTo<decltype(a)> {}(a, same));
        EXPECT_FALSE(ygg::EqualTo<decltype(a)> {}(a, foreign));
        EXPECT_EQ(ygg::Hash<decltype(a)> {}(a), ygg::Hash<decltype(a)> {}(same));
        std::unordered_map<db::QueryView<>, int, ygg::Hash<db::QueryView<>>, ygg::EqualTo<db::QueryView<>>> keys;
        keys.emplace(a, 1);
        keys.emplace(same, 2);
        keys.emplace(foreign, 3);
        EXPECT_EQ(keys.size(), 2);
        EXPECT_THROW(qb::project(repository, a, { z }), std::out_of_range);
        EXPECT_THROW(qb::rename(repository, a, { x, x }), std::invalid_argument);
        const auto p = qb::project(repository, a, { x });
        EXPECT_EQ(p.get_index(), qb::project(repository, a, { x }).get_index());
        qb::input(repository, 9, { z });  // Unreachable inputs must not require bindings.
        const auto renamed = qb::rename(repository, p, { z });
        plan = ygg::database::compile({ p, renamed, p });
        ASSERT_EQ(plan.node_count(), 3);
        EXPECT_EQ(plan.roots()[0], plan.roots()[2]);
        EXPECT_EQ(plan[plan.roots()[1]].columns()[0].label, z);
    }
    Relation input({ x, y });
    input.insert(std::tuple { ygg::uint_t(2), ygg::uint_t(3) });
    input.insert(std::tuple { ygg::uint_t(2), ygg::uint_t(4) });
    db::QueryEvaluator<> evaluator(plan);
    EXPECT_THROW(evaluator.get_result(), std::logic_error);
    evaluator.evaluate(std::array { borrow(input) });
    EXPECT_EQ(evaluator.get_result(0).size(), 1);
    EXPECT_EQ(evaluator.get_result(1).columns()[0].label, z);
    input.clear();
    EXPECT_EQ(evaluator.get_result().size(), 1);
    EXPECT_THROW(evaluator.evaluate(std::array { borrow(evaluator.get_result()) }), std::invalid_argument);
    EXPECT_EQ(evaluator.get_result().size(), 1);
}

TEST(DatabaseQuery, IncrementalDagMatchesFullAndExactDeltasForwardBackward)
{
    auto repository = qb::repository<>();
    const auto a = qb::input(repository, 0, { x, y });
    const auto b = qb::input(repository, 1, { x, y });
    const auto c = qb::input(repository, 2, { y, z });
    const auto joined = qb::join(repository, a, c);
    const auto projection = qb::project(repository, joined, { x });
    const auto roots = std::array { joined,
                                    projection,
                                    qb::rename(repository, projection, { z }),
                                    qb::select_equal(repository, a, x, y),
                                    qb::select_value(repository, a, x, ygg::uint_t(1)),
                                    qb::union_(repository, a, b),
                                    qb::difference(repository, a, b),
                                    qb::join(repository, a, a),
                                    qb::project(repository, joined, {}),
                                    qb::empty(repository, { x }) };
    const auto plan = ygg::database::compile(roots);
    db::QueryEvaluator<> full(plan);
    db::incremental::QueryEvaluator<> incremental(plan);
    Relation first({ x, y }), second({ x, y }), third({ y, z });
    first.insert(std::tuple { ygg::uint_t(1), ygg::uint_t(1) });
    first.insert(std::tuple { ygg::uint_t(2), ygg::uint_t(2) });
    second.insert(std::tuple { ygg::uint_t(1), ygg::uint_t(1) });
    third.insert(std::tuple { ygg::uint_t(1), ygg::uint_t(5) });
    third.insert(std::tuple { ygg::uint_t(2), ygg::uint_t(6) });
    const auto inputs = std::array { borrow(first), borrow(second), borrow(third) };
    incremental.initialize(inputs);
    const auto compare = [&]
    {
        full.evaluate(inputs);
        for (size_t i = 0; i < roots.size(); ++i)
        {
            EXPECT_EQ(rows(incremental.get_result(i)), rows(full.get_result(i)));
            EXPECT_TRUE(std::ranges::equal(incremental.get_result(i).columns().span(), roots[i].columns()));
        }
    };
    compare();
    Delta da(first.columns().span()), dbb(second.columns().span()), dc(third.columns().span());
    da.removed.insert(std::tuple { ygg::uint_t(1), ygg::uint_t(1) });
    da.added.insert(std::tuple { ygg::uint_t(1), ygg::uint_t(2) });
    dbb.removed.insert(std::tuple { ygg::uint_t(1), ygg::uint_t(1) });
    dbb.added.insert(std::tuple { ygg::uint_t(1), ygg::uint_t(2) });
    dc.added.insert(std::tuple { ygg::uint_t(2), ygg::uint_t(7) });
    const auto changes =
        std::array { std::pair { borrow(da.added), borrow(da.removed) }, std::pair { borrow(dbb.added), borrow(dbb.removed) }, std::pair { borrow(dc.added), borrow(dc.removed) } };
    for (int iteration = 0; iteration < 2; ++iteration)
    {
        std::vector<decltype(rows(first))> before;
        for (size_t i = 0; i < roots.size(); ++i)
            before.push_back(rows(incremental.get_result(i)));
        incremental.update(changes);
        apply(first, da);
        apply(second, dbb);
        apply(third, dc);
        compare();
        for (size_t i = 0; i < roots.size(); ++i)
        {
            const auto after = rows(incremental.get_result(i));
            EXPECT_EQ(rows(incremental.get_delta(i).added), minus(after, before[i]));
            EXPECT_EQ(rows(incremental.get_delta(i).removed), minus(before[i], after));
        }
        reverse(da);
        reverse(dbb);
        reverse(dc);
    }
    // A replaced projection witness keeps membership and must emit no change.
    EXPECT_TRUE(incremental.get_delta(1).added.empty());
    EXPECT_TRUE(incremental.get_delta(1).removed.empty());
    EXPECT_GT(incremental.memory_usage(), 0);
    EXPECT_GT(full.memory_usage(), 0);
}

TEST(DatabaseQuery, MixedTypesConstantsAndDistanceComposition)
{
    using Values = ygg::TypeList<std::uint32_t, double>;
    using TypedRelation = ygg::Builder<db::Relation<Values>>;
    ygg::Builder<db::Columns<Values>> schema;
    schema.push_back<std::uint32_t>(x);
    schema.push_back<double>(y);
    auto mixed = qb::repository<Values>();
    auto input = qb::input(mixed, 0, schema.span());
    EXPECT_THROW(qb::select_equal(mixed, input, x, y), std::invalid_argument);
    EXPECT_THROW(qb::select_value(mixed, input, y, std::uint32_t(1)), std::invalid_argument);
    auto selected = qb::select_value(mixed, input, y, -0.0);
    const auto selected_view = selected.get_variant().template get<ygg::Index<db::Query<Values, db::QuerySelectValueTag>>>();
    EXPECT_EQ(selected_view.get_data().column_position, 1);
    EXPECT_EQ(selected.get_index(), qb::select_value(mixed, input, y, 0.0).get_index());
    TypedRelation relation(schema.span());
    relation.insert(std::tuple { std::uint32_t(4), 0.0 });
    relation.insert(std::tuple { std::uint32_t(5), 2.0 });
    db::QueryEvaluator<Values> evaluator(ygg::database::compile({ qb::project(mixed, selected, { y, x }) }));
    evaluator.evaluate(std::array { borrow(relation) });
    EXPECT_TRUE(evaluator.get_result().contains(std::tuple { 0.0, std::uint32_t(4) }));

    auto graph = qb::repository<>();
    const auto sources = qb::input(graph, 0, { x });
    const auto edges = qb::input(graph, 1, { x, y });
    const auto targets = qb::input(graph, 2, { z });
    const auto distance = qb::distance(graph, sources, edges, targets, d);
    const auto plan = ygg::database::compile({ distance, qb::select_value(graph, distance, d, ygg::uint_t(2)) });
    Relation s({ x }), e({ x, y }), t({ z });
    s.insert(std::tuple { ygg::uint_t(1) });
    e.insert(std::tuple { ygg::uint_t(1), ygg::uint_t(2) });
    e.insert(std::tuple { ygg::uint_t(2), ygg::uint_t(3) });
    t.insert(std::tuple { ygg::uint_t(3) });
    const auto inputs = std::array { borrow(s), borrow(e), borrow(t) };
    db::QueryEvaluator<> full(plan);
    db::incremental::QueryEvaluator<> incremental(plan);
    full.evaluate(inputs);
    incremental.initialize(inputs);
    EXPECT_EQ(rows(full.get_result()), rows(incremental.get_result()));
    ASSERT_TRUE(incremental.get_result(1).contains(std::tuple { ygg::uint_t(1), ygg::uint_t(3), ygg::uint_t(2) }));
    Delta ds(s.columns().span()), de(e.columns().span()), dt(t.columns().span());
    de.added.insert(std::tuple { ygg::uint_t(1), ygg::uint_t(3) });
    incremental.update(
        std::array { std::pair { borrow(ds.added), borrow(ds.removed) }, std::pair { borrow(de.added), borrow(de.removed) }, std::pair { borrow(dt.added), borrow(dt.removed) } });
    apply(e, de);
    full.evaluate(inputs);
    EXPECT_EQ(rows(full.get_result()), rows(incremental.get_result()));
    EXPECT_TRUE(incremental.get_result(1).empty());
    EXPECT_EQ(incremental.get_delta().added.size(), 1);
    EXPECT_EQ(incremental.get_delta().removed.size(), 1);
}

TEST(DatabaseQuery, RejectsInvalidBatchesBeforeMutatingAndSupportsNonDistanceTypeFamilies)
{
    using Values = ygg::TypeList<double>;
    auto repository = qb::repository<Values>();
    const auto input = qb::input(repository, 0, { x });
    const auto edges = qb::input(repository, 1, { x, y });
    EXPECT_THROW(qb::distance(repository, input, edges, input, d), std::invalid_argument);
    ygg::Builder<db::Relation<Values>> relation({ x });
    relation.insert(std::tuple { 1.0 });
    const auto plan = ygg::database::compile({ qb::select_value(repository, input, x, 1.0) });
    db::QueryEvaluator<Values> full(plan);
    db::incremental::QueryEvaluator<Values> incremental(plan);
    const auto inputs = std::array { borrow(relation) };
    full.evaluate(inputs);
    incremental.initialize(inputs);
    db::incremental::Delta<Values> invalid(relation.columns().span());
    invalid.added.insert(std::tuple { 1.0 });
    const auto changes = std::array { std::pair { borrow(invalid.added), borrow(invalid.removed) } };
    EXPECT_THROW(incremental.update(changes), std::invalid_argument);
    EXPECT_EQ(rows(incremental.get_result()), rows(full.get_result()));
    EXPECT_THROW(incremental.update(std::span<const std::pair<db::BorrowedRelationView<Values>, db::BorrowedRelationView<Values>>> {}), std::invalid_argument);
    EXPECT_EQ(incremental.get_result().size(), 1);
}

TEST(DatabaseQuery, RandomizedSharedSetDagMaintainsExactNetChanges)
{
    auto repository = qb::repository<>();
    const auto a = qb::input(repository, 0, { x, y });
    const auto b = qb::input(repository, 1, { x, y });
    const auto edges = qb::input(repository, 2, { y, z });
    const auto ready = qb::input(repository, 3, {});
    const auto u = qb::union_(repository, a, b);
    const auto difference = qb::difference(repository, a, b);
    const auto project_union = qb::project(repository, u, { x });
    const auto project_a = qb::project(repository, a, { x });
    const auto project_b = qb::project(repository, b, { x });
    const auto roots = std::array { qb::join(repository, u, edges),
                                    qb::join(repository, difference, edges),
                                    qb::project(repository, difference, { x }),
                                    project_union,
                                    qb::union_(repository, project_a, project_b),
                                    qb::difference(repository, project_a, project_b),
                                    qb::join(repository, project_a, project_b),
                                    qb::select_equal(repository, u, x, y),
                                    qb::select_value(repository, difference, x, ygg::uint_t(1)),
                                    qb::project(repository, qb::join(repository, a, edges), {}),
                                    qb::join(repository, ready, project_union),
                                    qb::rename(repository, project_union, { z }) };
    const auto plan = ygg::database::compile(roots);
    std::array<Relation, 4> inputs { Relation({ x, y }), Relation({ x, y }), Relation({ y, z }), Relation(std::span<const Column> {}) };
    const auto refs = std::array { borrow(inputs[0]), borrow(inputs[1]), borrow(inputs[2]), borrow(inputs[3]) };
    db::QueryEvaluator<> full(plan);
    db::incremental::QueryEvaluator<> incremental(plan);
    incremental.initialize(refs);
    std::mt19937 random(741);
    for (size_t iteration = 0; iteration < 120; ++iteration)
    {
        std::vector<decltype(rows(inputs[0]))> before;
        for (size_t i = 0; i < roots.size(); ++i)
            before.push_back(rows(incremental.get_result(i)));
        std::vector<Delta> changes;
        for (size_t input = 0; input < inputs.size(); ++input)
        {
            changes.emplace_back(inputs[input].columns().span());
            auto& relation = inputs[input];
            auto& change = changes.back();
            const auto toggle = [&](const auto& tuple)
            {
                if (random() % 3 != 0)
                    return;
                if (const auto position = relation.find(tuple))
                {
                    change.removed.insert(tuple);
                    relation.erase(*position);
                }
                else
                {
                    change.added.insert(tuple);
                    relation.insert(tuple);
                }
            };
            if (input == 3)
                toggle(std::tuple {});
            else
                for (ygg::uint_t i = 0; i < 3; ++i)
                    for (ygg::uint_t j = 0; j < 3; ++j)
                        toggle(std::tuple { i, j });
        }
        std::vector<Change> delta_refs;
        for (const auto& change : changes)
            delta_refs.push_back({ borrow(change.added), borrow(change.removed) });
        incremental.update(delta_refs);
        full.evaluate(refs);
        for (size_t root = 0; root < roots.size(); ++root)
        {
            SCOPED_TRACE(iteration);
            SCOPED_TRACE(root);
            const auto after = rows(full.get_result(root));
            EXPECT_EQ(rows(incremental.get_result(root)), after);
            EXPECT_EQ(rows(incremental.get_delta(root).added), minus(after, before[root]));
            EXPECT_EQ(rows(incremental.get_delta(root).removed), minus(before[root], after));
        }
    }
}

TEST(DatabaseQuery, GenericJoinPlanExecutesInBothModesAndPrunesAlternatives)
{
    auto repository = qb::repository<>();
    const auto ai = qb::input(repository, 0, { x, y });
    const auto bi = qb::input(repository, 1, { y, z });
    const auto ci = qb::input(repository, 2, { z, x });
    qb::input(repository, 8, { x, y });
    const auto root = qb::generic_join(repository, { ai, bi, ci }, { z, x, y });
    const auto plan = ygg::database::compile({ root });
    ASSERT_EQ(plan.node_count(), 4);
    db::QueryEvaluator<> full(plan);
    db::incremental::QueryEvaluator<> incremental(plan);
    Relation ar({ x, y }), br({ y, z }), cr({ z, x });
    ar.insert(std::tuple { ygg::uint_t(1), ygg::uint_t(2) });
    br.insert(std::tuple { ygg::uint_t(2), ygg::uint_t(3) });
    cr.insert(std::tuple { ygg::uint_t(3), ygg::uint_t(1) });
    const auto inputs = std::array { borrow(ar), borrow(br), borrow(cr) };
    full.evaluate(inputs);
    incremental.initialize(inputs);
    ASSERT_EQ(incremental.get_result().size(), 1);
    EXPECT_EQ(rows(full.get_result()), rows(incremental.get_result()));
    Delta da(ar.columns().span()), dbb(br.columns().span()), dc(cr.columns().span());
    dc.removed.insert(std::tuple { ygg::uint_t(3), ygg::uint_t(1) });
    incremental.update(
        std::array { std::pair { borrow(da.added), borrow(da.removed) }, std::pair { borrow(dbb.added), borrow(dbb.removed) }, std::pair { borrow(dc.added), borrow(dc.removed) } });
    EXPECT_TRUE(incremental.get_result().empty());
    EXPECT_EQ(incremental.get_delta().removed.size(), 1);
    apply(cr, dc);
    full.evaluate(inputs);
    EXPECT_EQ(rows(full.get_result()), rows(incremental.get_result()));
}

TEST(DatabaseQuery, NullaryGenericJoinIsIdentityInBothModes)
{
    auto repository = qb::repository<>();
    const auto root = qb::generic_join(repository, {}, {});
    const auto plan = ygg::database::compile({ root, root });
    ASSERT_EQ(plan.node_count(), 1);
    EXPECT_EQ(plan.roots().front(), plan.roots().back());
    db::QueryEvaluator<> full(plan);
    db::incremental::QueryEvaluator<> incremental(plan);
    const std::array<db::BorrowedRelationView<>, 0> no_inputs;
    full.evaluate(no_inputs);
    incremental.initialize(no_inputs);
    ASSERT_EQ(full.get_result().size(), 1);
    EXPECT_TRUE(full.get_result().contains(std::tuple {}));
    EXPECT_EQ(rows(full.get_result()), rows(incremental.get_result()));
    incremental.update(std::array<Change, 0> {});
    EXPECT_EQ(rows(full.get_result()), rows(incremental.get_result(1)));
    EXPECT_TRUE(incremental.get_delta().added.empty());
    EXPECT_TRUE(incremental.get_delta().removed.empty());
}

TEST(DatabaseQuery, InterningUsesArgumentsAndCanonicalOrders)
{
    auto repository = qb::repository<>();
    const auto input = qb::input(repository, 0, { x, y });
    EXPECT_EQ(qb::input(repository, 0, { x, y }).get_index(), input.get_index());
    const auto project = qb::project(repository, input, { x, y });
    EXPECT_EQ(qb::project(repository, input, { x, y }).get_index(), project.get_index());
    EXPECT_NE(qb::project(repository, input, { y, x }).get_index(), project.get_index());
    const auto renamed = qb::rename(repository, input, { x, y });
    EXPECT_NE(qb::rename(repository, input, { y, x }).get_index(), renamed.get_index());
    const auto joined = qb::generic_join(repository, { input }, { x, y });
    EXPECT_EQ(qb::generic_join(repository, { input }, { x, y }, { x, y }).get_index(), joined.get_index());
    EXPECT_NE(qb::generic_join(repository, { input }, { y, x }, { x, y }).get_index(), joined.get_index());
    EXPECT_NE(qb::generic_join(repository, { input }, { x, y }, { y, x }).get_index(), joined.get_index());
}

TEST(DatabaseQuery, CachedPlansSurviveRepositoryGrowthAndCistaRelocation)
{
    auto repository = qb::repository<>();
    const auto a = qb::input(repository, 0, { x, y });
    const auto b = qb::input(repository, 1, { y, z });
    const auto joined = qb::join(repository, a, b);
    const auto projected = qb::project(repository, joined, { z, x });
    const auto generic = qb::generic_join(repository, { a, b }, { z, y, x }, { x, z, y });
    const auto distance = qb::distance(repository, qb::project(repository, a, { x }), a, qb::project(repository, b, { z }), d);
    const auto view = concrete<db::QueryJoinTag>(joined);
    const auto* original = &view.get_data();
    for (size_t slot = 2; slot < 260; ++slot)
        qb::join(repository, qb::input(repository, slot, { x, y }), b);
    EXPECT_EQ(&view.get_data(), original);
    EXPECT_TRUE(std::ranges::equal(view.get_data().plan.output_columns().span(), joined.columns()));

    const auto round_trip = []<typename Concrete>(Concrete query)
    {
        using Record = std::remove_cvref_t<decltype(query.get_data())>;
        auto original_bytes = cista::serialize(query.get_data());
        auto relocated = original_bytes;
        original_bytes.clear();
        original_bytes.shrink_to_fit();
        const auto* restored = cista::deserialize<Record>(relocated);
        EXPECT_TRUE(std::ranges::equal(restored->plan.output_columns().span(), query.columns()));
        if constexpr (std::same_as<Concrete, db::QueryView<db::DefaultColumnTypes, db::QueryGenericJoinTag>>)
        {
            const auto& expected = query.get_data().plan;
            EXPECT_TRUE(std::ranges::equal(restored->plan.variable_order(), expected.variable_order()));
            for (size_t input = 0; input < expected.input_count(); ++input)
            {
                EXPECT_TRUE(std::ranges::equal(restored->plan.input_columns(input), expected.input_columns(input)));
                EXPECT_EQ(restored->plan.input_keys(input).size(), expected.input_keys(input).size());
            }
            for (size_t depth = 0; depth < expected.variable_order().size(); ++depth)
                EXPECT_TRUE(std::ranges::equal(restored->plan.variable_inputs(depth), expected.variable_inputs(depth)));
        }
    };
    round_trip(view);
    round_trip(concrete<db::QueryProjectTag>(projected));
    round_trip(concrete<db::QueryGenericJoinTag>(generic));
    round_trip(concrete<db::QueryDistanceTag>(distance));
}

TEST(DatabaseQuery, FactoriesRejectMalformedQueriesBeforePublishingPlans)
{
    auto repository = qb::repository<>();
    auto other = qb::repository<>();
    const auto input = qb::input(repository, 0, { x });
    const auto foreign = qb::input(other, 0, { x });
    EXPECT_THROW(ygg::database::compile({ input, foreign }), std::invalid_argument);
    EXPECT_THROW(qb::generic_join(repository, { input }, { y }), std::invalid_argument);
    EXPECT_THROW(qb::generic_join(repository, { input }, { x, x }), std::invalid_argument);
    EXPECT_THROW(qb::generic_join(repository, { input }, { x }, { y }), std::invalid_argument);
    EXPECT_THROW(qb::select_encoded(repository, input, x, std::span<const std::byte> {}), std::invalid_argument);
    const ygg::Builder<db::Columns<>> schema({ x });
    std::vector<db::ColumnLayout> malformed(schema.begin(), schema.end());
    malformed[0].offset = 1;
    EXPECT_THROW(qb::input(repository, 1, malformed), std::invalid_argument);
    EXPECT_THROW(qb::input(repository, 0, malformed), std::invalid_argument);
    malformed.assign(schema.begin(), schema.end());
    ++malformed[0].size;
    EXPECT_THROW(qb::input(repository, 1, malformed), std::invalid_argument);
    EXPECT_THROW(qb::input(repository, 0, malformed), std::invalid_argument);
    const auto plan = ygg::database::compile({ input });
    EXPECT_THROW(plan[ygg::Index<db::Query<>>(99)], std::out_of_range);
    const auto typed = concrete<db::QueryInputTag>(plan[plan.roots().front()]);
    static_assert(std::is_const_v<std::remove_reference_t<decltype(typed.get_data())>>);
    EXPECT_EQ(repository.size(), 1);
}
}  // namespace
