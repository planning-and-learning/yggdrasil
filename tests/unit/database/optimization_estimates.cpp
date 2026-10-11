#include "query_helpers.hpp"

#include <array>
#include <gtest/gtest.h>
#include <map>
#include <yggdrasil/database/optimization/optimization.hpp>

namespace
{
using Column = ygg::Index<ygg::database::Column>;
using PlanId = ygg::Index<ygg::database::Query<>>;
const Column x(0), y(1), z(2);

// Exercises System R estimation independently of planning: the same conjunction
// must have one estimate whatever its join and filter association.
auto estimate(ygg::database::QueryView<> root, const ygg::database::Statistics<>& statistics)
{
    std::map<PlanId, ygg::database::detail::Estimate> estimates;
    for (const auto query : ygg::database::reachable(std::span<const ygg::database::QueryView<>>(&root, 1)))
        estimates[query.get_index()] = ygg::database::detail::estimate(
            query,
            query.get_index(),
            [&](ygg::database::QueryView<> child) { return estimates.at(child.get_index()); },
            statistics);
    return estimates.at(root.get_index());
}

void same_estimate(const auto& lhs, const auto& rhs)
{
    EXPECT_NEAR(lhs.rows, rhs.rows, 1e-9 * std::max(1.0, lhs.rows));
    ASSERT_EQ(lhs.distinct.size(), rhs.distinct.size());
    for (const auto& [column, count] : lhs.distinct)
        EXPECT_NEAR(count, rhs.distinct.at(column), 1e-9 * std::max(1.0, count));
}

TEST(DatabaseOptimizationEstimates, JoinAndFilterAssociationHaveOneNormalizedEstimate)
{
    auto queries = ygg::tests::qb::repository<>();
    const auto a = ygg::tests::qb::input(queries, 0, { x, y });
    const auto b = ygg::tests::qb::input(queries, 1, { y, z });
    const auto c = ygg::tests::qb::input(queries, 2, { x, z });
    ygg::database::Statistics<> statistics;
    statistics.inputs[0] = { 100, { { x, 10 }, { y, 20 } } };
    statistics.inputs[1] = { 200, { { y, 20 }, { z, 10 } } };
    statistics.inputs[2] = { 50, { { x, 10 }, { z, 10 } } };

    const auto left = ygg::tests::qb::join(queries, ygg::tests::qb::join(queries, a, b), c);
    const auto right = ygg::tests::qb::join(queries, a, ygg::tests::qb::join(queries, b, c));
    const auto reordered = ygg::tests::qb::join(queries, c, ygg::tests::qb::join(queries, b, a));
    const auto expected = estimate(left, statistics);
    EXPECT_NEAR(expected.rows, 500, 1e-9);
    same_estimate(expected, estimate(right, statistics));
    same_estimate(expected, estimate(reordered, statistics));

    const auto above = ygg::tests::qb::select_value(queries, left, x, ygg::uint_t(7));
    const auto below = ygg::tests::qb::join(queries, ygg::tests::qb::join(queries, ygg::tests::qb::select_value(queries, a, x, ygg::uint_t(7)), b), c);
    const auto repeated = ygg::tests::qb::select_value(queries, below, x, ygg::uint_t(7));
    EXPECT_NEAR(estimate(above, statistics).rows, 50, 1e-9);
    same_estimate(estimate(above, statistics), estimate(below, statistics));
    same_estimate(estimate(above, statistics), estimate(repeated, statistics));

    const auto equality_above = ygg::tests::qb::select_equal(queries, left, x, z);
    const auto equality_below = ygg::tests::qb::join(queries, ygg::tests::qb::join(queries, a, b), ygg::tests::qb::select_equal(queries, c, z, x));
    same_estimate(estimate(equality_above, statistics), estimate(equality_below, statistics));
}

TEST(DatabaseOptimizationEstimates, EqualityAndConstantPredicatesAreIdempotentAndDetectConflicts)
{
    auto queries = ygg::tests::qb::repository<>();
    const auto input = ygg::tests::qb::input(queries, 0, { x, y, z });
    ygg::database::Statistics<> statistics;
    statistics.inputs[0] = { 1000, { { x, 10 }, { y, 20 }, { z, 25 } } };
    const auto equal = ygg::tests::qb::select_equal(queries, input, x, y);
    EXPECT_NEAR(estimate(equal, statistics).rows, 50, 1e-9);
    same_estimate(estimate(equal, statistics), estimate(ygg::tests::qb::select_equal(queries, equal, y, x), statistics));
    same_estimate(estimate(input, statistics), estimate(ygg::tests::qb::select_equal(queries, input, x, x), statistics));

    const auto transitive = ygg::tests::qb::select_equal(queries, equal, y, z);
    EXPECT_NEAR(estimate(transitive, statistics).rows, 2, 1e-9);
    same_estimate(estimate(transitive, statistics), estimate(ygg::tests::qb::select_equal(queries, transitive, z, x), statistics));

    const auto selected = ygg::tests::qb::select_value(queries, equal, x, ygg::uint_t(7));
    same_estimate(estimate(selected, statistics), estimate(ygg::tests::qb::select_value(queries, selected, y, ygg::uint_t(7)), statistics));
    const auto conflict = estimate(ygg::tests::qb::select_value(queries, selected, y, ygg::uint_t(8)), statistics);
    EXPECT_EQ(conflict.rows, 0);
    for (const auto& [column, count] : conflict.distinct)
        EXPECT_EQ(count, 0);
    EXPECT_EQ(
        estimate(ygg::tests::qb::select_value(queries, ygg::tests::qb::select_value(queries, input, x, ygg::uint_t(7)), x, ygg::uint_t(8)), statistics).rows,
        0);

    const auto self_join = ygg::tests::qb::join(queries, input, input);
    same_estimate(estimate(input, statistics), estimate(self_join, statistics));
    const auto alias = ygg::tests::qb::rename(queries, input, { z, y, x });
    const auto aliased = ygg::tests::qb::join(queries, input, alias);
    EXPECT_LT(estimate(aliased, statistics).rows, estimate(input, statistics).rows);
}

TEST(DatabaseOptimizationEstimates, ProjectionCreatesAnIndependentScopeForHiddenColumns)
{
    auto queries = ygg::tests::qb::repository<>();
    const auto a = ygg::tests::qb::input(queries, 0, { x, y });
    const auto b = ygg::tests::qb::input(queries, 1, { y, z });
    ygg::database::Statistics<> statistics;
    statistics.inputs[0] = { 100, { { x, 10 }, { y, 20 } } };
    statistics.inputs[1] = { 200, { { y, 20 }, { z, 10 } } };

    const auto x_values = ygg::tests::qb::project(queries, a, { x });
    const auto hidden_y = ygg::tests::qb::join(queries, x_values, b);
    EXPECT_NEAR(estimate(hidden_y, statistics).rows, 2000, 1e-9);
    EXPECT_NEAR(estimate(ygg::tests::qb::join(queries, a, b), statistics).rows, 1000, 1e-9);

    const auto y_values = ygg::tests::qb::project(queries, a, { y });
    EXPECT_NEAR(estimate(ygg::tests::qb::join(queries, x_values, y_values), statistics).rows, 200, 1e-9);
    const auto selected_scope = ygg::tests::qb::project(queries, ygg::tests::qb::select_equal(queries, a, x, y), { x });
    // Duplicate elimination: min(T/2, V(x)) = min(5/2, 5) rows survive the projection.
    EXPECT_NEAR(estimate(ygg::tests::qb::join(queries, selected_scope, b), statistics).rows, 500, 1e-9);
    const auto renamed = ygg::tests::qb::rename(queries, x_values, { y });
    EXPECT_NEAR(estimate(ygg::tests::qb::join(queries, renamed, b), statistics).rows, 100, 1e-9);
    same_estimate(estimate(a, statistics), estimate(ygg::tests::qb::project(queries, a, { y, x }), statistics));
}

TEST(DatabaseOptimizationEstimates, ObservationsOverrideDerivedEstimates)
{
    auto queries = ygg::tests::qb::repository<>();
    const auto a = ygg::tests::qb::input(queries, 3, { x, y });
    const auto b = ygg::tests::qb::input(queries, 8, { y, z });
    const auto join = ygg::tests::qb::join(queries, a, b);
    ygg::database::Statistics<> statistics;
    statistics.inputs[3] = { 100, {} };
    statistics.inputs[8] = { 200, {} };
    EXPECT_NEAR(estimate(join, statistics).rows, 100, 1e-9);
    statistics.expressions[join.get_index()] = { 7, { { x, 2 }, { y, 3 } } };
    const auto observed = estimate(join, statistics);
    EXPECT_EQ(observed.rows, 7);
    EXPECT_EQ(observed.distinct.at(x), 2);
    EXPECT_EQ(observed.distinct.at(z), 7);
    // A known empty input stays empty.
    statistics.expressions.clear();
    statistics.inputs[3].rows = 0;
    EXPECT_EQ(estimate(join, statistics).rows, 0);
}

TEST(DatabaseOptimizationEstimates, BoundedDomainsCapDistinctCounts)
{
    auto queries = ygg::tests::qb::repository<>();
    const auto a = ygg::tests::qb::input(queries, 0, { x, y });
    const auto b = ygg::tests::qb::input(queries, 1, { y, z });
    ygg::database::Statistics<> statistics;
    statistics.inputs[0] = { 1000, {} };
    statistics.inputs[1] = { 1000, {} };
    EXPECT_NEAR(estimate(ygg::tests::qb::join(queries, a, b), statistics).rows, 1000, 1e-9);
    statistics.objects = 10;
    EXPECT_NEAR(estimate(a, statistics).distinct.at(y), 10, 1e-9);
    EXPECT_NEAR(estimate(ygg::tests::qb::join(queries, a, b), statistics).rows, 100000, 1e-9);
    EXPECT_LE(estimate(ygg::tests::qb::union_(queries, a, a), statistics).distinct.at(x), 10);
}

TEST(DatabaseOptimizationEstimates, FactorIdentitySeparatesInputsAndQueries)
{
    ygg::Builder<ygg::database::Columns<>> columns;
    columns.push_back<ygg::uint_t>(x);
    const ygg::database::RelationStatistics statistics { 10, { { x, 2 } } };
    const auto input = ygg::database::detail::factor(statistics, columns.span(), ygg::database::detail::InputFactorIdentity { 7 });
    const auto query = ygg::database::detail::factor(statistics, columns.span(), ygg::database::detail::QueryFactorIdentity { 7 });
    const std::array labels { x };
    EXPECT_NEAR(ygg::database::detail::conjunction(ygg::database::detail::combine(input, input), labels).rows, 10, 1e-9);
    const auto combined = ygg::database::detail::conjunction(ygg::database::detail::combine(input, query), labels);
    EXPECT_EQ(combined.factors.size(), 2);
    EXPECT_NEAR(combined.rows, 50, 1e-9);
}
}  // namespace
