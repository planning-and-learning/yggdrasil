#include <array>
#include <gtest/gtest.h>
#include <set>
#include <yggdrasil/database/optimization.hpp>
#include "query_helpers.hpp"

namespace qb = ygg::tests::qb;

namespace
{
namespace db = ygg::database;
using Column = ygg::Index<db::Column>;
using PlanId = ygg::Index<db::Query<>>;
const Column x(0), y(1), z(2);

// Exercise the estimate independently of extraction: a cheaper physical plan
// must never result merely from applying the same selectivity twice.
auto estimate(db::QueryView<> root, const db::OptimizationContext<>& context)
{
    std::vector<db::optimization_detail::Estimate> estimates(root.get_repository().size());
    for (const auto query : db::reachable(std::span<const db::QueryView<>>(&root, 1)))
    {
        const auto id = query.get_index();
        estimates[id.get_value()] = db::optimization_detail::estimate(query, id, id, [&](PlanId child) { return estimates.at(child.get_value()); }, context);
    }
    return estimates.at(root.get_index().get_value());
}

void same_estimate(const auto& lhs, const auto& rhs)
{
    EXPECT_NEAR(lhs.rows, rhs.rows, 1e-9 * std::max(1.0, lhs.rows));
    ASSERT_EQ(lhs.distinct.size(), rhs.distinct.size());
    for (const auto& [column, count] : lhs.distinct)
        EXPECT_NEAR(count, rhs.distinct.at(column), 1e-9 * std::max(1.0, count));
}

template<class Tag>
bool contains(const db::QueryPlan<>& plan)
{
    for (size_t position = 0; position < plan.node_count(); ++position)
        if (plan[PlanId(ygg::to_uint_t(position))].get_variant().template is<ygg::Index<db::Query<db::DefaultColumnTypes, Tag>>>())
            return true;
    return false;
}

TEST(DatabaseOptimizationEstimates, JoinAndFilterAssociationHaveOneNormalizedEstimate)
{
    auto queries = qb::repository<>();
    const auto a = qb::input(queries, 0, { x, y });
    const auto b = qb::input(queries, 1, { y, z });
    const auto c = qb::input(queries, 2, { x, z });
    db::OptimizationContext<> context;
    context.statistics.inputs[0] = { 100, { { x, 10 }, { y, 20 } }, {} };
    context.statistics.inputs[1] = { 200, { { y, 20 }, { z, 10 } }, {} };
    context.statistics.inputs[2] = { 50, { { x, 10 }, { z, 10 } }, {} };

    const auto left = qb::join(queries, qb::join(queries, a, b), c);
    const auto right = qb::join(queries, a, qb::join(queries, b, c));
    const auto reordered = qb::join(queries, c, qb::join(queries, b, a));
    const auto expected = estimate(left, context);
    EXPECT_NEAR(expected.rows, 500, 1e-9);
    same_estimate(expected, estimate(right, context));
    same_estimate(expected, estimate(reordered, context));

    const auto above = qb::select_value(queries, left, x, ygg::uint_t(7));
    const auto below = qb::join(queries, qb::join(queries, qb::select_value(queries, a, x, ygg::uint_t(7)), b), c);
    const auto repeated = qb::select_value(queries, below, x, ygg::uint_t(7));
    EXPECT_NEAR(estimate(above, context).rows, 50, 1e-9);
    same_estimate(estimate(above, context), estimate(below, context));
    same_estimate(estimate(above, context), estimate(repeated, context));

    const auto equality_above = qb::select_equal(queries, left, x, z);
    const auto equality_below = qb::join(queries, qb::join(queries, a, b), qb::select_equal(queries, c, z, x));
    same_estimate(estimate(equality_above, context), estimate(equality_below, context));
}

TEST(DatabaseOptimizationEstimates, EqualityAndConstantPredicatesAreIdempotentAndDetectConflicts)
{
    auto queries = qb::repository<>();
    const auto input = qb::input(queries, 0, { x, y, z });
    db::OptimizationContext<> context;
    context.statistics.inputs[0] = { 1000, { { x, 10 }, { y, 20 }, { z, 25 } }, {} };
    const auto equal = qb::select_equal(queries, input, x, y);
    EXPECT_NEAR(estimate(equal, context).rows, 50, 1e-9);
    same_estimate(estimate(equal, context), estimate(qb::select_equal(queries, equal, y, x), context));
    same_estimate(estimate(input, context), estimate(qb::select_equal(queries, input, x, x), context));

    const auto transitive = qb::select_equal(queries, equal, y, z);
    EXPECT_NEAR(estimate(transitive, context).rows, 2, 1e-9);
    same_estimate(estimate(transitive, context), estimate(qb::select_equal(queries, transitive, z, x), context));

    const auto selected = qb::select_value(queries, equal, x, ygg::uint_t(7));
    same_estimate(estimate(selected, context), estimate(qb::select_value(queries, selected, y, ygg::uint_t(7)), context));
    const auto conflict = estimate(qb::select_value(queries, selected, y, ygg::uint_t(8)), context);
    EXPECT_EQ(conflict.rows, 0);
    for (const auto& [column, count] : conflict.distinct)
        EXPECT_EQ(count, 0);
    EXPECT_EQ(estimate(qb::select_value(queries, qb::select_value(queries, input, x, ygg::uint_t(7)), x, ygg::uint_t(8)), context).rows, 0);

    const auto self_join = qb::join(queries, input, input);
    same_estimate(estimate(input, context), estimate(self_join, context));
    const auto alias = qb::rename(queries, input, { z, y, x });
    const auto aliased = qb::join(queries, input, alias);
    EXPECT_LT(estimate(aliased, context).rows, estimate(input, context).rows);
}

TEST(DatabaseOptimizationEstimates, ProjectionCreatesAnIndependentScopeForHiddenColumns)
{
    auto queries = qb::repository<>();
    const auto a = qb::input(queries, 0, { x, y });
    const auto b = qb::input(queries, 1, { y, z });
    db::OptimizationContext<> context;
    context.statistics.inputs[0] = { 100, { { x, 10 }, { y, 20 } }, {} };
    context.statistics.inputs[1] = { 200, { { y, 20 }, { z, 10 } }, {} };

    const auto x_values = qb::project(queries, a, { x });
    const auto hidden_y = qb::join(queries, x_values, b);
    EXPECT_NEAR(estimate(hidden_y, context).rows, 2000, 1e-9);
    EXPECT_NEAR(estimate(qb::join(queries, a, b), context).rows, 1000, 1e-9);

    const auto y_values = qb::project(queries, a, { y });
    EXPECT_NEAR(estimate(qb::join(queries, x_values, y_values), context).rows, 200, 1e-9);
    const auto selected_scope = qb::project(queries, qb::select_equal(queries, a, x, y), { x });
    EXPECT_NEAR(estimate(qb::join(queries, selected_scope, b), context).rows, 1000, 1e-9);
    const auto renamed = qb::rename(queries, x_values, { y });
    EXPECT_NEAR(estimate(qb::join(queries, renamed, b), context).rows, 100, 1e-9);
    same_estimate(estimate(a, context), estimate(qb::project(queries, a, { y, x }), context));
}

TEST(DatabaseOptimizationEstimates, ExplicitCountsOverrideLogicalExpressionsAndMissingInputsFallBack)
{
    auto queries = qb::repository<>();
    const auto a = qb::input(queries, 3, { x, y });
    const auto b = qb::input(queries, 8, { y, z });
    const auto join = qb::join(queries, a, b);
    db::OptimizationContext<> context;
    context.limits = { 0, 64, 0, 1 };
    context.cost.memory_weight = 0;
    context.statistics.inputs[3] = { 100, {}, {} };
    context.statistics.expressions[join.get_index()] = { 7, { { x, 2 }, { y, 3 }, { z, 4 } }, 19 };
    // Input 8 has no statistics and uses domain_size rows.
    EXPECT_EQ(estimate(a, context).rows, 100);
    EXPECT_EQ(estimate(b, context).rows, context.cost.domain_size);
    EXPECT_EQ(estimate(b, context).distinct.at(y), context.cost.domain_size);
    context.statistics.inputs[8] = { 200, {}, {} };
    const auto observed = estimate(join, context);
    EXPECT_EQ(observed.rows, 7);
    EXPECT_EQ(observed.distinct.at(x), 2);
    const auto optimized = db::optimize(join, context);
    // Inputs scan rows * width; the observed join work replaces its own.
    EXPECT_DOUBLE_EQ(optimized.report.estimated_work, 100 * 2 + 200 * 2 + 19);
    EXPECT_DOUBLE_EQ(optimized.report.estimated_score, optimized.report.estimated_work);

    // A known empty input is distinct from an absent measurement. No default
    // cardinality may turn an empty join into a nonempty estimate.
    context.statistics.expressions.clear();
    context.statistics.inputs[3].rows = 0;
    EXPECT_EQ(estimate(join, context).rows, 0);
    EXPECT_NO_THROW(db::optimize<db::CostBasedTag>(join, context));
    EXPECT_NO_THROW(db::optimize<>(join));
}

TEST(DatabaseOptimizationEstimates, MultipleNamedRootsChargeEachSharedNodeOnce)
{
    auto queries = qb::repository<>();
    const auto input = qb::input(queries, 0, { x, y });
    const auto first = qb::select_value(queries, input, x, ygg::uint_t(1));
    const auto second = qb::select_value(queries, input, y, ygg::uint_t(2));
    db::OptimizationContext<> context;
    context.statistics.inputs[0] = { 100, { { x, 10 }, { y, 20 } }, {} };
    context.limits = { 0, 64, 0, 1 };
    context.cost.memory_weight = 0;
    const auto single = db::optimize<db::CostBasedTag>(first, context);
    const auto duplicate_roots = std::array { first, first, input };
    const auto duplicate = db::optimize<db::CostBasedTag>(std::span<const db::QueryView<>>(duplicate_roots), context);
    EXPECT_EQ(duplicate.plan.roots()[0], duplicate.plan.roots()[1]);
    EXPECT_DOUBLE_EQ(duplicate.report.estimated_work, single.report.estimated_work);
    EXPECT_DOUBLE_EQ(duplicate.report.retained, single.report.retained);
    const auto roots = std::array { first, second };
    const auto shared = db::optimize<db::CostBasedTag>(std::span<const db::QueryView<>>(roots), context);
    // One input scan; each filter reads 100 rows and writes its selected rows.
    EXPECT_NEAR(shared.report.estimated_work, 100 * 2 + (100 + 10 * 2) + (100 + 5 * 2), 1e-9);
}

TEST(DatabaseOptimizationEstimates, SelectiveChainChoosesTheSelectiveBinarySubjoin)
{
    auto queries = qb::repository<>();
    const auto a = qb::input(queries, 0, { x, y });
    const auto b = qb::input(queries, 1, { y, z });
    const auto c = qb::input(queries, 2, { z });
    const auto root = qb::join(queries, qb::join(queries, a, b), c);
    db::OptimizationContext<> context;
    context.cost.memory_weight = 0;
    context.limits = { 100, 64, 2000, 8 };
    context.statistics.inputs[0] = { 10000, { { x, 10000 }, { y, 10000 } }, {} };
    context.statistics.inputs[1] = { 10000, { { y, 10000 }, { z, 10000 } }, {} };
    context.statistics.inputs[2] = { 1, { { z, 1 } }, {} };
    const auto result = db::optimize<db::CostBasedTag>(root, context);
    EXPECT_FALSE(contains<db::QueryGenericJoinTag>(result.plan));
    bool found_selective_subjoin = false;
    for (size_t position = 0; position < result.plan.node_count(); ++position)
        ygg::visit(
            [&]<typename Concrete>(Concrete query)
            {
                if constexpr (std::same_as<Concrete, db::QueryView<db::DefaultColumnTypes, db::QueryJoinTag>>)
                {
                    std::set<size_t> slots;
                    for (const auto child : { query.get_lhs(), query.get_rhs() })
                        ygg::visit(
                            [&]<typename Child>(Child input)
                            {
                                if constexpr (std::same_as<Child, db::QueryView<db::DefaultColumnTypes, db::QueryInputTag>>)
                                    slots.insert(input.get_input_slot());
                            },
                            child.get_variant());
                    found_selective_subjoin |= slots == std::set<size_t> { 1, 2 };
                }
            },
            result.plan[PlanId(ygg::to_uint_t(position))].get_variant());
    EXPECT_TRUE(found_selective_subjoin);
    EXPECT_LT(result.report.estimated_score, result.report.baseline_score);
}

using ExpressionId = ygg::Index<db::Query<>>;
using GroupId = db::optimization_detail::MemoGroupId<db::DefaultColumnTypes>;
static_assert(!std::convertible_to<GroupId, ExpressionId>);
static_assert(!std::convertible_to<ygg::Index<db::Query<>>, GroupId>);

TEST(DatabaseOptimizationEstimates, MemoKeepsFiniteWitnessesWhenEquivalenceGroupsCycle)
{
    auto queries = qb::repository<>();
    const auto a = qb::input(queries, 0, { x, y });
    const auto b = qb::input(queries, 1, { x, z });
    const auto identity = qb::project(queries, a, { x, y });
    const auto root = qb::project(queries, qb::join(queries, a, b), { y, x });
    const std::array original { a, identity, root };
    db::OptimizationContext<> context;
    context.limits = { 512, 64, 1000, 4 };
    context.statistics.inputs[0].rows = 10;
    context.statistics.inputs[1].rows = 10;
    db::OptimizationReport report;
    db::optimization_detail::Memo<db::DefaultColumnTypes> memo(original, context.limits, report);
    memo.saturate();
    EXPECT_FALSE(report.budget_exhausted);
    EXPECT_EQ(memo.group(memo.roots()[0]), memo.group(memo.roots()[1]));
    const auto count = memo.size();
    memo.saturate();
    EXPECT_EQ(memo.size(), count);
    for (size_t value = 0; value < memo.size(); ++value)
    {
        const auto id = ExpressionId(ygg::to_uint_t(value));
        db::for_each_child(memo.node(id), [&](auto child) { EXPECT_LT(child.get_index().get_value(), value); });
        EXPECT_LE(memo.representative(memo.group(id)).get_value(), value);
    }
    db::optimization_detail::Search<db::DefaultColumnTypes> search(memo, original, context, report);
    const auto extracted = search.run();
    EXPECT_EQ(extracted.root_count(), original.size());
    for (size_t position = 0; position < extracted.node_count(); ++position)
        db::for_each_child(extracted[PlanId(ygg::to_uint_t(position))], [&](auto child) { EXPECT_LT(child.get_index().get_value(), position); });
}

TEST(DatabaseOptimizationEstimates, InterningRetainsEveryOriginalLogicalObservation)
{
    auto queries = qb::repository<>();
    const auto input = qb::input(queries, 0, { x });
    const auto identity = qb::project(queries, input, { x });
    const std::array original { input, identity };
    // Saturation merges this identity projection with its input. The observation
    // attached to the other logical source must survive the group merge.
    db::OptimizationContext<> context;
    context.cost.memory_weight = 0;
    context.statistics.inputs[0].rows = 100;
    context.statistics.expressions[identity.get_index()].rows = 4;
    const auto result = db::optimization_detail::optimize<db::DefaultColumnTypes>(original, context);
    EXPECT_DOUBLE_EQ(result.report.estimated_work, 4);
    EXPECT_EQ(result.plan.roots()[0], result.plan.roots()[1]);
}

TEST(DatabaseOptimizationEstimates, NullaryGenericJoinHasOneRowWithoutChildStatistics)
{
    auto queries = qb::repository<>();
    const auto root = qb::generic_join(queries, {}, {});
    const std::array original { root };
    db::OptimizationContext<> context;
    context.cost.memory_weight = 0;
    const auto result = db::optimization_detail::optimize<db::DefaultColumnTypes>(original, context);
    EXPECT_DOUBLE_EQ(result.report.estimated_work, 1);
    EXPECT_TRUE(result.plan[result.plan.roots().front()].columns().empty());
    EXPECT_EQ(result.plan.root_count(), 1);
}

TEST(DatabaseOptimizationEstimates, FactorIdentitySeparatesInputMemoAndPhysicalDomains)
{
    namespace opt = db::optimization_detail;
    ygg::Builder<db::Columns<>> columns;
    columns.push_back<ygg::uint_t>(x);
    const db::RelationStatistics statistics { 10, { { x, 2 } }, {} };
    const auto input = opt::factor(statistics, columns.span(), opt::InputFactorIdentity { 7 });
    const auto memo = opt::factor(statistics, columns.span(), opt::MemoFactorIdentity { 7 });
    const auto plan = opt::factor(statistics, columns.span(), opt::PlanFactorIdentity { 7 });
    const auto combined = opt::conjunction(opt::combine(opt::combine(input, memo), plan), columns.span());
    EXPECT_EQ(combined.factors.size(), 3);
    EXPECT_NEAR(combined.rows, 250, 1e-9);
}
}  // namespace
