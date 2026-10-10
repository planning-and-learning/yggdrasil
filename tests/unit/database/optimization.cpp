#include <array>
#include <utility>
#include <cmath>
#include <gtest/gtest.h>
#include <limits>
#include <random>
#include <type_traits>
#include <yggdrasil/database/formatter.hpp>
#include <yggdrasil/database/incremental/query.hpp>
#include <yggdrasil/database/optimization.hpp>
#include <yggdrasil/database/query_evaluation.hpp>
#include <yggdrasil/database/relation_repository.hpp>
#include "query_helpers.hpp"

namespace qb = ygg::tests::qb;

namespace
{
struct IdentityOptimizerTag
{
};
struct WrongSchemaOptimizerTag
{
};
struct ForwardReferenceOptimizerTag
{
};
struct ForeignInputOptimizerTag
{
};
struct MissingRootOptimizerTag
{
};
struct InvalidRootOptimizerTag
{
};
bool custom_called = false;
}
namespace ygg::database
{
template<>
struct Optimizer<IdentityOptimizerTag, DefaultColumnTypes>
{
    static OptimizationResult<> run(std::span<const QueryView<>> roots, const OptimizationContext<>&)
    {
        custom_called = true;
        return { compile(roots), {} };
    }
};
template<>
struct Optimizer<WrongSchemaOptimizerTag, DefaultColumnTypes>
{
    static OptimizationResult<> run(std::span<const QueryView<>> roots, const OptimizationContext<>&)
    {
        auto repository = qb::repository<>();
        const auto schema = roots[0].columns();
        const auto input = qb::input(repository, 0, schema);
        auto labels = detail::query_labels(schema);
        labels[0] = Index<Column>(99);
        return { ygg::database::compile({ qb::rename(repository, input, labels) }), {} };
    }
};
template<>
struct Optimizer<ForwardReferenceOptimizerTag, DefaultColumnTypes>
{
    static OptimizationResult<> run(std::span<const QueryView<>> roots, const OptimizationContext<>&)
    {
        auto repository = qb::repository<>();
        const auto schema = roots[0].columns();
        qb::input(repository, 0, schema);
        const auto future = QueryView<>(Index<Query<>>(to_uint_t(repository.size())), repository);
        const auto root = qb::project(repository, future, detail::query_labels(schema));
        return { ygg::database::compile({ root }), {} };
    }
};
template<>
struct Optimizer<ForeignInputOptimizerTag, DefaultColumnTypes>
{
    static OptimizationResult<> run(std::span<const QueryView<>> roots, const OptimizationContext<>&)
    {
        auto repository = qb::repository<>();
        const auto input = qb::input(repository, 99, roots[0].columns());
        return { ygg::database::compile({ input }), {} };
    }
};
template<>
struct Optimizer<MissingRootOptimizerTag, DefaultColumnTypes>
{
    static OptimizationResult<> run(std::span<const QueryView<>>, const OptimizationContext<>&) { return { QueryPlan<> {}, {} }; }
};
template<>
struct Optimizer<InvalidRootOptimizerTag, DefaultColumnTypes>
{
    static OptimizationResult<> run(std::span<const QueryView<>>, const OptimizationContext<>&)
    {
        auto repository = qb::repository<>();
        const auto invalid = QueryView<>(Index<Query<>>(99), repository);
        return { ygg::database::compile({ invalid }), {} };
    }
};

}
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
const Column x(0), y(1), z(2), distance_column(3);

auto rows(const auto& relation)
{
    std::vector<std::vector<std::byte>> result;
    for (size_t i = 0; i < relation.size(); ++i)
        result.emplace_back(relation.row(i).begin(), relation.row(i).end());
    std::ranges::sort(result);
    return result;
}
auto difference(const auto& lhs, const auto& rhs)
{
    std::remove_cvref_t<decltype(lhs)> result;
    std::ranges::set_difference(lhs, rhs, std::back_inserter(result));
    return result;
}

TEST(DatabaseOptimization, CustomTagDispatchValidatesReturnedRootsAndGraph)
{
    auto queries = qb::repository<>();
    const auto input = qb::input(queries, 0, { x });
    auto result = db::optimize<IdentityOptimizerTag>(input);
    EXPECT_TRUE(custom_called);
    ASSERT_EQ(result.plan.root_count(), 1);
    EXPECT_TRUE(std::ranges::equal(result.plan[result.plan.roots()[0]].columns(), input.columns()));
    EXPECT_THROW(db::optimize<WrongSchemaOptimizerTag>(input), std::invalid_argument);
    EXPECT_THROW(db::optimize<ForwardReferenceOptimizerTag>(input), std::invalid_argument);
    EXPECT_THROW(db::optimize<ForeignInputOptimizerTag>(input), std::invalid_argument);
    EXPECT_THROW(db::optimize<MissingRootOptimizerTag>(input), std::invalid_argument);
    EXPECT_THROW(db::optimize<InvalidRootOptimizerTag>(input), std::invalid_argument);
}

TEST(DatabaseOptimization, ZeroBudgetKeepsTheOriginalPlanWithoutStatistics)
{
    auto queries = qb::repository<>();
    const auto a = qb::input(queries, 0, { x, y });
    const auto b = qb::input(queries, 1, { y, z });
    const auto c = qb::input(queries, 2, { z, x });
    const auto root = qb::project(queries, qb::join(queries, qb::join(queries, a, b), c), { z, x });
    db::OptimizationContext<> context;
    context.limits.memo_expressions = 0;
    context.limits.candidate_evaluations = 0;
    const auto result = db::optimize(root, context);
    EXPECT_TRUE(result.report.budget_exhausted);
    // The incumbent is the original query; only its node numbering may differ.
    EXPECT_EQ(result.plan.node_count(), ygg::database::compile({ root }).node_count());
    EXPECT_DOUBLE_EQ(result.report.estimated_score, result.report.baseline_score);
}

TEST(DatabaseOptimization, FallsBackWithoutStatisticsAndRejectsBadMetadata)
{
    auto queries = qb::repository<>();
    const auto input = qb::input(queries, 0, { x });
    EXPECT_NO_THROW(db::optimize(input));
    db::OptimizationContext<> context;
    context.statistics.inputs[0].rows = 3;
    EXPECT_NO_THROW(db::optimize(input, context));
    const auto valid = context;
    for (const double rows : { -1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN() })
    {
        context = valid;
        context.statistics.inputs[0].rows = rows;
        EXPECT_THROW(db::optimize(input, context), std::invalid_argument);
    }
    context = valid;
    context.statistics.inputs[0].distinct[x] = 4;
    EXPECT_THROW(db::optimize(input, context), std::invalid_argument);
    context = valid;
    context.statistics.inputs[0].distinct[z] = 1;
    EXPECT_THROW(db::optimize(input, context), std::invalid_argument);
    context = valid;
    context.statistics.inputs[0].work = -1;
    EXPECT_THROW(db::optimize(input, context), std::invalid_argument);
    context = valid;
    context.cost.memory_weight = -1;
    EXPECT_THROW(db::optimize(input, context), std::invalid_argument);
    context = valid;
    context.cost.domain_size = 0;
    EXPECT_THROW(db::optimize(input, context), std::invalid_argument);
}

TEST(DatabaseOptimization, CollectsCanonicalTypedDistinctCounts)
{
    ygg::Builder<db::Columns<>> columns;
    columns.push_back<double>(x);
    columns.push_back<bool>(y);
    Relation relation(columns.span());
    relation.insert(std::tuple { 0.0, false });
    relation.insert(std::tuple { -0.0, true });
    relation.insert(std::tuple { std::numeric_limits<double>::quiet_NaN(), false });
    const auto stats = db::collect_statistics<db::DefaultColumnTypes>(std::array { borrow(relation) });
    EXPECT_EQ(stats.inputs.at(0).rows, 3);
    EXPECT_EQ(stats.inputs.at(0).distinct.at(x), 2);
    EXPECT_EQ(stats.inputs.at(0).distinct.at(y), 2);
}

TEST(DatabaseOptimization, ScopedRewritesPreserveDifferencesRenamesAndExistentialVariables)
{
    auto queries = qb::repository<>();
    const auto a = qb::input(queries, 0, { x, y });
    const auto b = qb::input(queries, 1, { x, y });
    const auto edges = qb::input(queries, 2, { y, z });
    const auto projection = qb::project(queries, a, { x });
    const auto renamed = qb::rename(queries, a, { y, x });
    const auto selected = qb::select_value(queries, renamed, y, ygg::uint_t(1));
    const auto roots = std::array { qb::join(queries, projection, edges),
                                    qb::project(queries, qb::difference(queries, a, b), { x }),
                                    qb::difference(queries, projection, qb::project(queries, b, { x })),
                                    selected,
                                    qb::project(queries, qb::union_(queries, a, b), { y, x }),
                                    qb::join(queries, qb::union_(queries, a, b), edges),
                                    qb::join(queries, a, qb::project(queries, a, { x })),
                                    qb::project(queries, qb::join(queries, a, edges), { z, x }),
                                    qb::project(queries, a, { x }),
                                    qb::project(queries, a, { x }) };
    std::array<Relation, 3> inputs { Relation({ x, y }), Relation({ x, y }), Relation({ y, z }) };
    inputs[0].insert(std::tuple { ygg::uint_t(1), ygg::uint_t(2) });
    inputs[0].insert(std::tuple { ygg::uint_t(1), ygg::uint_t(3) });
    inputs[1].insert(std::tuple { ygg::uint_t(1), ygg::uint_t(2) });
    inputs[2].insert(std::tuple { ygg::uint_t(0), ygg::uint_t(2) });
    const auto refs = std::array { borrow(inputs[0]), borrow(inputs[1]), borrow(inputs[2]) };
    db::OptimizationContext<> context;
    context.statistics = db::collect_statistics<db::DefaultColumnTypes>(refs);
    context.limits = { 180, 64, 160, 4 };
    const auto unmeasured = db::optimize(std::span<const db::QueryView<>>(roots));
    const auto measured = db::optimize(std::span<const db::QueryView<>>(roots), context);
    db::QueryEvaluator<> reference(ygg::database::compile(roots));
    reference.evaluate(refs);
    EXPECT_EQ(reference.get_result(0).size(), 1);
    EXPECT_EQ(reference.get_result(1).size(), 1);
    EXPECT_TRUE(reference.get_result(2).empty());
    for (const auto* chosen : { &unmeasured, &measured })
    {
        EXPECT_EQ(chosen->plan.roots()[8], chosen->plan.roots()[9]);
        db::QueryEvaluator<> full(chosen->plan);
        db::incremental::QueryEvaluator<> incremental(chosen->plan);
        full.evaluate(refs);
        incremental.initialize(refs);
        for (size_t root = 0; root < roots.size(); ++root)
        {
            EXPECT_EQ(rows(full.get_result(root)), rows(reference.get_result(root)));
            EXPECT_EQ(rows(incremental.get_result(root)), rows(reference.get_result(root)));
            EXPECT_TRUE(std::ranges::equal(full.get_result(root).columns().span(), roots[root].columns()));
        }
    }
    EXPECT_LE(measured.report.estimated_score, measured.report.baseline_score + 1e-9);

    db::incremental::QueryEvaluator<> incremental(measured.plan);
    incremental.initialize(refs);
    std::mt19937 random(1597);
    for (size_t iteration = 0; iteration < 35; ++iteration)
    {
        std::vector<decltype(rows(inputs[0]))> before;
        for (size_t root = 0; root < roots.size(); ++root)
            before.push_back(rows(incremental.get_result(root)));
        std::vector<Delta> deltas;
        for (auto& input : inputs)
        {
            deltas.emplace_back(input.columns().span());
            for (ygg::uint_t a = 0; a < 4; ++a)
                for (ygg::uint_t b = 0; b < 4; ++b)
                {
                    if (random() % 4 != 0)
                        continue;
                    const auto row = std::tuple { a, b };
                    if (const auto position = input.find(row))
                    {
                        deltas.back().removed.insert(row);
                        input.erase(*position);
                    }
                    else
                    {
                        deltas.back().added.insert(row);
                        input.insert(row);
                    }
                }
        }
        std::vector<Change> changes;
        for (const auto& change : deltas)
            changes.push_back({ borrow(change.added), borrow(change.removed) });
        incremental.update(changes);
        reference.evaluate(refs);
        for (size_t root = 0; root < roots.size(); ++root)
        {
            SCOPED_TRACE(iteration);
            SCOPED_TRACE(root);
            const auto after = rows(reference.get_result(root));
            EXPECT_EQ(rows(incremental.get_result(root)), after);
            EXPECT_EQ(rows(incremental.get_delta(root).added), difference(after, before[root]));
            EXPECT_EQ(rows(incremental.get_delta(root).removed), difference(before[root], after));
        }
    }
}

TEST(DatabaseOptimization, TinyBudgetRetainsAnIncumbentNoWorseThanBaseline)
{
    auto queries = qb::repository<>();
    const auto a = qb::input(queries, 0, { x, y });
    const auto b = qb::input(queries, 1, { y, z });
    const auto c = qb::input(queries, 2, { z, x });
    const auto root = qb::project(queries, qb::join(queries, qb::join(queries, a, b), c), { x });
    for (const size_t budget : { size_t(0), size_t(1), size_t(4) })
    {
        db::OptimizationContext<> context;
        context.statistics.inputs[0] = { 120, { { x, 12 }, { y, 20 } }, {} };
        context.statistics.inputs[1] = { 60, { { y, 20 }, { z, 6 } }, {} };
        context.statistics.inputs[2] = { 24, { { z, 6 }, { x, 12 } }, {} };
        context.limits = { budget, 64, budget, 1 };
        const auto result = db::optimize<db::CostBasedTag>(root, context);
        EXPECT_TRUE(result.report.budget_exhausted);
        EXPECT_TRUE(std::isfinite(result.report.estimated_score));
        EXPECT_LE(result.report.estimated_score, result.report.baseline_score + 1e-9);
        EXPECT_TRUE(std::ranges::equal(result.plan[result.plan.roots()[0]].columns(), root.columns()));
    }
}

TEST(DatabaseOptimization, DefaultSearchMatchesRandomRelationalExpressions)
{
    auto queries = qb::repository<>();
    const auto a = qb::input(queries, 0, { x, y });
    const auto b = qb::input(queries, 1, { x, y });
    const auto c = qb::input(queries, 2, { y, z });
    const auto e = qb::input(queries, 3, { x, z });
    const auto leaves = std::array { a, b, c, e };
    std::array<Relation, 4> inputs { Relation({ x, y }), Relation({ x, y }), Relation({ y, z }), Relation({ x, z }) };
    const auto refs = std::array { borrow(inputs[0]), borrow(inputs[1]), borrow(inputs[2]), borrow(inputs[3]) };
    std::mt19937 random(12553);
    const auto labels = [](db::QueryView<> query)
    {
        std::vector<Column> result;
        for (const auto& column : query.columns())
            result.push_back(column.label);
        return result;
    };
    const auto peer = [&](db::QueryView<> query)
    {
        const auto target = labels(query);
        auto result = target.size() == 3 ? qb::join(queries, a, c) : leaves[random() % leaves.size()];
        auto columns = labels(result);
        columns.resize(target.size());
        result = qb::project(queries, result, columns);
        return qb::rename(queries, result, target);
    };
    for (size_t expression = 0; expression < 64; ++expression)
    {
        auto root = leaves[random() % leaves.size()];
        for (size_t depth = 0; depth < 4; ++depth)
        {
            auto columns = labels(root);
            switch (random() % 7)
            {
                case 0:
                    root = qb::join(queries, root, leaves[random() % leaves.size()]);
                    break;
                case 1:
                    std::shuffle(columns.begin(), columns.end(), random);
                    columns.resize(random() % (columns.size() + 1));
                    root = qb::project(queries, root, columns);
                    break;
                case 2:
                {
                    std::array names { x, y, z };
                    std::shuffle(names.begin(), names.end(), random);
                    root = qb::rename(queries, root, std::span<const Column>(names.data(), columns.size()));
                    break;
                }
                case 3:
                    if (!columns.empty())
                        root = qb::select_equal(queries, root, columns[random() % columns.size()], columns[random() % columns.size()]);
                    break;
                case 4:
                    if (!columns.empty())
                        root = qb::select_value(queries, root, columns[random() % columns.size()], ygg::uint_t(random() % 4));
                    break;
                case 5:
                    root = qb::union_(queries, root, peer(root));
                    break;
                case 6:
                    root = qb::difference(queries, root, peer(root));
                    break;
            }
        }
        for (auto& input : inputs)
        {
            input.clear();
            for (ygg::uint_t left = 0; left < 4; ++left)
                for (ygg::uint_t right = 0; right < 4; ++right)
                    if (random() % 3 != 0)
                        input.insert(std::tuple { left, right });
        }
        const auto baseline = ygg::database::compile({ root });
        SCOPED_TRACE(expression);
        SCOPED_TRACE(db::explain(baseline));
        db::OptimizationContext<> context;
        context.statistics = db::collect_statistics<db::DefaultColumnTypes>(refs);
        const auto unmeasured = db::optimize(root);
        const auto measured = db::optimize(root, context);
        db::QueryEvaluator<> reference(baseline);
        reference.evaluate(refs);
        for (const auto* optimized : { &unmeasured, &measured })
        {
            db::QueryEvaluator<> evaluator(optimized->plan);
            evaluator.evaluate(refs);
            EXPECT_TRUE(std::ranges::equal(evaluator.get_result().columns().span(), root.columns()));
            EXPECT_EQ(rows(evaluator.get_result()), rows(reference.get_result()));
        }
    }
}

TEST(DatabaseOptimization, DefaultSearchPreservesFactoredUnionsAndFilteredDifferences)
{
    auto queries = qb::repository<>();
    const auto a = qb::input(queries, 0, { x, y });
    const auto b = qb::input(queries, 1, { x, y });
    const auto c = qb::input(queries, 2, { y, z });
    const auto selected_difference = qb::select_value(queries, qb::rename(queries, qb::difference(queries, a, b), { y, x }), y, ygg::uint_t(1));
    const auto roots = std::array { qb::union_(queries, qb::join(queries, a, c), qb::join(queries, b, c)),
                                    qb::join(queries, qb::union_(queries, a, b), c),
                                    selected_difference,
                                    qb::select_equal(queries, qb::rename(queries, qb::difference(queries, a, b), { y, x }), y, x),
                                    qb::union_(queries, a, qb::join(queries, a, b)),
                                    qb::join(queries, a, qb::union_(queries, a, b)) };
    Relation ar({ x, y }), br({ x, y }), cr({ y, z });
    ar.insert(std::tuple { ygg::uint_t(1), ygg::uint_t(1) });
    ar.insert(std::tuple { ygg::uint_t(1), ygg::uint_t(2) });
    br.insert(std::tuple { ygg::uint_t(1), ygg::uint_t(2) });
    br.insert(std::tuple { ygg::uint_t(2), ygg::uint_t(2) });
    cr.insert(std::tuple { ygg::uint_t(1), ygg::uint_t(3) });
    cr.insert(std::tuple { ygg::uint_t(2), ygg::uint_t(2) });
    const auto inputs = std::array { borrow(ar), borrow(br), borrow(cr) };
    db::OptimizationContext<> context;
    context.statistics = db::collect_statistics<db::DefaultColumnTypes>(inputs);
    const auto original = ygg::database::compile(roots);
    const auto unmeasured = db::optimize(std::span<const db::QueryView<>>(roots));
    const auto measured = db::optimize(std::span<const db::QueryView<>>(roots), context);
    db::QueryEvaluator<> reference(original);
    reference.evaluate(inputs);
    EXPECT_EQ(rows(reference.get_result(0)), rows(reference.get_result(1)));
    EXPECT_EQ(reference.get_result(2).size(), 1);
    EXPECT_EQ(reference.get_result(3).size(), 1);
    EXPECT_EQ(rows(reference.get_result(4)), rows(ar));
    EXPECT_EQ(rows(reference.get_result(5)), rows(ar));
    for (const auto* chosen : { &unmeasured, &measured })
    {
        db::QueryEvaluator<> full(chosen->plan);
        db::incremental::QueryEvaluator<> incremental(chosen->plan);
        full.evaluate(inputs);
        incremental.initialize(inputs);
        for (size_t root = 0; root < roots.size(); ++root)
        {
            EXPECT_EQ(rows(full.get_result(root)), rows(reference.get_result(root)));
            EXPECT_EQ(rows(incremental.get_result(root)), rows(reference.get_result(root)));
        }
    }
}
}  // namespace
