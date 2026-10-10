#include "query_helpers.hpp"

#include <array>
#include <cmath>
#include <gtest/gtest.h>
#include <limits>
#include <random>
#include <set>
#include <type_traits>
#include <utility>
#include <yggdrasil/database/optimization/optimization.hpp>
#include <yggdrasil/database/semantics/incremental/query.hpp>
#include <yggdrasil/database/semantics/query_evaluation.hpp>
#include <yggdrasil/database/semantics/relation_repository.hpp>
#include <yggdrasil/database/syntax/formatter.hpp>

namespace qb = ygg::tests::qb;

namespace
{
namespace db = ygg::database;
using Column = ygg::Index<db::Column>;
using PlanId = ygg::Index<db::Query<>>;
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
const Column w(3), x(0), y(1), z(2);

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
template<class Tag>
size_t count(const db::QueryPlan<>& plan)
{
    size_t result = 0;
    for (size_t position = 0; position < plan.node_count(); ++position)
        result += db::is<Tag>(plan[PlanId(ygg::to_uint_t(position))]);
    return result;
}
std::vector<Column> labels(std::initializer_list<Column> values)
{
    std::vector<Column> result(values);
    std::ranges::sort(result);
    return result;
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

TEST(DatabaseOptimization, RejectsInvalidStatistics)
{
    auto queries = qb::repository<>();
    const auto input = qb::input(queries, 0, { x });
    EXPECT_NO_THROW(db::optimize(input));
    db::Statistics<> statistics;
    statistics.inputs[0].rows = 3;
    EXPECT_NO_THROW(db::optimize(input, statistics));
    const auto valid = statistics;
    for (const double rows : { -1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN() })
    {
        statistics = valid;
        statistics.inputs[0].rows = rows;
        EXPECT_THROW(db::optimize(input, statistics), std::invalid_argument);
    }
    statistics = valid;
    statistics.inputs[0].distinct[x] = 4;
    EXPECT_THROW(db::optimize(input, statistics), std::invalid_argument);
    statistics = valid;
    statistics.inputs[0].distinct[z] = 1;
    EXPECT_THROW(db::optimize(input, statistics), std::invalid_argument);
}

TEST(DatabaseOptimization, GyoRecognizesAcyclicAndCyclicHypergraphs)
{
    using db::detail::join_tree;
    const auto path = join_tree({ labels({ x, y }), labels({ y, z }), labels({ z, w }) });
    ASSERT_TRUE(path);
    EXPECT_EQ(std::ranges::count_if(*path, [](const auto& parent) { return !parent; }), 1);
    EXPECT_TRUE(join_tree({ labels({ x, y }), labels({ x, z }), labels({ x, w }) }));
    EXPECT_TRUE(join_tree({ labels({ x, y, z }), labels({ x, y }), labels({ y, z }), labels({ z, x }) }));
    EXPECT_FALSE(join_tree({ labels({ x, y }), labels({ y, z }), labels({ z, x }) }));
    EXPECT_TRUE(join_tree({ labels({ x }), labels({ y }) }));
}

TEST(DatabaseOptimization, YannakakisBoundsIntermediatesByInputTimesOutput)
{
    auto queries = qb::repository<>();
    const auto r = qb::input(queries, 0, { x, y });
    const auto s = qb::input(queries, 1, { y, z });
    const auto t = qb::input(queries, 2, { z, w });
    const auto root = qb::project(queries, qb::join(queries, qb::join(queries, r, s), t), { x });
    std::array<Relation, 3> inputs { Relation({ x, y }), Relation({ y, z }), Relation({ z, w }) };
    for (ygg::uint_t i = 0; i < 10; ++i)
    {
        inputs[0].insert(std::tuple { i, ygg::uint_t(0) });
        inputs[1].insert(std::tuple { ygg::uint_t(0), i });
        inputs[2].insert(std::tuple { i, ygg::uint_t(i % 2) });
        inputs[2].insert(std::tuple { i + 100, ygg::uint_t(0) });  // dangling
    }
    const auto refs = std::array { borrow(inputs[0]), borrow(inputs[1]), borrow(inputs[2]) };
    const auto plan = db::optimize(root);
    EXPECT_EQ(count<db::QueryGenericJoinTag>(plan), 0);
    // Evaluate every operator of the plan as its own root.
    std::vector<db::QueryView<>> nodes;
    for (size_t position = 0; position < plan.node_count(); ++position)
        nodes.push_back(plan[PlanId(ygg::to_uint_t(position))]);
    db::QueryEvaluator<> evaluator(db::compile(std::span<const db::QueryView<>>(nodes)));
    evaluator.evaluate(refs);
    db::QueryEvaluator<> reference(db::compile({ root })), optimized(plan);
    reference.evaluate(refs);
    optimized.evaluate(refs);
    EXPECT_EQ(rows(optimized.get_result()), rows(reference.get_result()));
    // Yannakakis' bound for projected acyclic joins: O(|input| · |output|).
    const auto bound = (inputs[0].size() + inputs[1].size() + inputs[2].size()) * reference.get_result().size();
    for (size_t position = 0; position < nodes.size(); ++position)
        EXPECT_LE(evaluator.get_result(position).size(), bound);
}

TEST(DatabaseOptimization, StructureChoosesGenericJoinOnlyForCyclicBlocks)
{
    auto queries = qb::repository<>();
    const auto a = qb::input(queries, 0, { x, y });
    const auto b = qb::input(queries, 1, { y, z });
    const auto c = qb::input(queries, 2, { z, x });
    const auto triangle = qb::join(queries, qb::join(queries, a, b), c);
    const auto path = qb::join(queries, a, b);
    EXPECT_EQ(count<db::QueryGenericJoinTag>(db::optimize(triangle)), 1);
    EXPECT_EQ(count<db::QueryGenericJoinTag>(db::optimize(path)), 0);
    // A measured input alone does not make the block measured.
    db::Statistics<> partial;
    partial.inputs[0] = { 10, {} };
    EXPECT_EQ(count<db::QueryGenericJoinTag>(db::optimize(triangle, partial)), 1);
}

TEST(DatabaseOptimization, CostBasedPlanChoosesTheSelectiveSubjoin)
{
    auto queries = qb::repository<>();
    const auto a = qb::input(queries, 0, { x, y });
    const auto b = qb::input(queries, 1, { y, z });
    const auto c = qb::input(queries, 2, { z });
    const auto root = qb::join(queries, qb::join(queries, a, b), c);
    db::Statistics<> statistics;
    statistics.inputs[0] = { 10000, { { x, 10000 }, { y, 10000 } } };
    statistics.inputs[1] = { 10000, { { y, 10000 }, { z, 10000 } } };
    statistics.inputs[2] = { 1, { { z, 1 } } };
    const auto plan = db::optimize(root, statistics);
    EXPECT_EQ(count<db::QueryGenericJoinTag>(plan), 0);
    bool found = false;
    for (size_t position = 0; position < plan.node_count(); ++position)
        ygg::visit(
            [&]<typename Concrete>(Concrete query)
            {
                if constexpr (std::same_as<Concrete, db::QueryView<db::DefaultColumnTypes, db::QueryJoinTag>>)
                {
                    std::set<size_t> slots;
                    for (const auto child : { query.get_lhs(), query.get_rhs() })
                        for (const auto node : db::reachable(std::span<const db::QueryView<>>(&child, 1)))
                            ygg::visit(
                                [&]<typename Leaf>(Leaf leaf)
                                {
                                    if constexpr (std::same_as<Leaf, db::QueryView<db::DefaultColumnTypes, db::QueryInputTag>>)
                                        slots.insert(leaf.get_input_slot());
                                },
                                node.get_variant());
                    found |= slots == std::set<size_t> { 1, 2 };
                }
            },
            plan[PlanId(ygg::to_uint_t(position))].get_variant());
    EXPECT_TRUE(found);
}

TEST(DatabaseOptimization, CostBasedPlanUsesGenericJoinWhereJoinsGrow)
{
    auto queries = qb::repository<>();
    const auto a = qb::input(queries, 0, { x, y });
    const auto b = qb::input(queries, 1, { y, z });
    const auto c = qb::input(queries, 2, { z, x });
    const auto root = qb::join(queries, qb::join(queries, a, b), c);
    // Every pairwise join grows: 10^4 · 10^4 / 100 rows from inputs of 10^4 rows.
    db::Statistics<> statistics;
    statistics.inputs[0] = { 10000, { { x, 100 }, { y, 100 } } };
    statistics.inputs[1] = { 10000, { { y, 100 }, { z, 100 } } };
    statistics.inputs[2] = { 10000, { { z, 100 }, { x, 100 } } };
    EXPECT_EQ(count<db::QueryGenericJoinTag>(db::optimize(root, statistics)), 1);
    // With key-like columns no join grows, and binary joins remain.
    for (auto& [slot, input] : statistics.inputs)
        for (auto& [column, count] : input.distinct)
            count = 10000;
    EXPECT_EQ(count<db::QueryGenericJoinTag>(db::optimize(root, statistics)), 0);
}

TEST(DatabaseOptimization, NullaryGenericJoinHasOneRow)
{
    auto queries = qb::repository<>();
    const auto root = qb::generic_join(queries, {}, {});
    for (const auto& plan : { db::optimize(root), db::optimize(root, db::Statistics<> { 4, {}, {} }) })
    {
        db::QueryEvaluator<> evaluator(plan);
        evaluator.evaluate(std::array<db::BorrowedRelationView<>, 0> {});
        EXPECT_EQ(evaluator.get_result().size(), 1);
    }
}

TEST(DatabaseOptimization, BothModesPreserveDifferencesRenamesAndExistentialVariables)
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
    const auto statistics = db::collect_statistics<db::DefaultColumnTypes>(refs);
    const auto unmeasured = db::optimize(std::span<const db::QueryView<>>(roots));
    const auto measured = db::optimize(std::span<const db::QueryView<>>(roots), statistics);
    db::QueryEvaluator<> reference(ygg::database::compile(roots));
    reference.evaluate(refs);
    EXPECT_EQ(reference.get_result(0).size(), 1);
    EXPECT_EQ(reference.get_result(1).size(), 1);
    EXPECT_TRUE(reference.get_result(2).empty());
    for (const auto* chosen : { &unmeasured, &measured })
    {
        EXPECT_EQ(chosen->roots()[8], chosen->roots()[9]);
        db::QueryEvaluator<> full(*chosen);
        db::incremental::QueryEvaluator<> incremental(*chosen);
        full.evaluate(refs);
        incremental.initialize(refs);
        for (size_t root = 0; root < roots.size(); ++root)
        {
            EXPECT_EQ(rows(full.get_result(root)), rows(reference.get_result(root)));
            EXPECT_EQ(rows(incremental.get_result(root)), rows(reference.get_result(root)));
            EXPECT_TRUE(std::ranges::equal(full.get_result(root).columns().span(), roots[root].columns()));
        }
    }
    db::incremental::QueryEvaluator<> incremental(measured);
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

TEST(DatabaseOptimization, BothModesMatchRandomRelationalExpressions)
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
        const auto statistics = db::collect_statistics<db::DefaultColumnTypes>(refs);
        const auto unmeasured = db::optimize(root);
        const auto measured = db::optimize(root, statistics);
        db::QueryEvaluator<> reference(baseline);
        reference.evaluate(refs);
        for (const auto* optimized : { &unmeasured, &measured })
        {
            db::QueryEvaluator<> evaluator(*optimized);
            evaluator.evaluate(refs);
            EXPECT_TRUE(std::ranges::equal(evaluator.get_result().columns().span(), root.columns()));
            EXPECT_EQ(rows(evaluator.get_result()), rows(reference.get_result()));
        }
    }
}

TEST(DatabaseOptimization, BothModesPreserveFactoredUnionsAndFilteredDifferences)
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
    const auto statistics = db::collect_statistics<db::DefaultColumnTypes>(inputs);
    const auto original = ygg::database::compile(roots);
    const auto unmeasured = db::optimize(std::span<const db::QueryView<>>(roots));
    const auto measured = db::optimize(std::span<const db::QueryView<>>(roots), statistics);
    db::QueryEvaluator<> reference(original);
    reference.evaluate(inputs);
    EXPECT_EQ(rows(reference.get_result(0)), rows(reference.get_result(1)));
    EXPECT_EQ(reference.get_result(2).size(), 1);
    EXPECT_EQ(reference.get_result(3).size(), 1);
    EXPECT_EQ(rows(reference.get_result(4)), rows(ar));
    EXPECT_EQ(rows(reference.get_result(5)), rows(ar));
    for (const auto* chosen : { &unmeasured, &measured })
    {
        db::QueryEvaluator<> full(*chosen);
        db::incremental::QueryEvaluator<> incremental(*chosen);
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
