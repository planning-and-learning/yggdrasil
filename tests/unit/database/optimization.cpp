#include "query_helpers.hpp"

#include <array>
#include <cmath>
#include <gtest/gtest.h>
#include <limits>
#include <random>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <yggdrasil/database/optimization/optimization.hpp>
#include <yggdrasil/database/semantics/bound/query_evaluation.hpp>
#include <yggdrasil/database/semantics/incremental/query.hpp>
#include <yggdrasil/database/semantics/query_evaluation.hpp>
#include <yggdrasil/database/semantics/relation_repository.hpp>
#include <yggdrasil/database/syntax/formatter.hpp>
#include <yggdrasil/database/syntax/row.hpp>

namespace
{
using Column = ygg::Index<ygg::database::Column>;
using PlanId = ygg::Index<ygg::database::Query<>>;
using Relation = ygg::Builder<ygg::database::Relation>;
using Delta = ygg::database::incremental::Delta;
using Change = std::pair<ygg::database::BorrowedRelationView, ygg::database::BorrowedRelationView>;

// Borrowed views use a relation repository only as their context.
inline ygg::database::BorrowedRelationView borrow(const ygg::Builder<ygg::database::Relation>& relation)
{
    static const auto context = ygg::database::RelationRepositoryFactory {}.create();
    return { relation, context };
}
const Column w(3), x(0), y(1), z(2);

auto rows(const auto& relation)
{
    std::vector<std::vector<std::byte>> result;
    result.reserve(relation.size());
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
size_t count(const ygg::database::QueryPlan& plan)
{
    size_t result = 0;
    for (size_t position = 0; position < plan.node_count(); ++position)
        result += ygg::database::is<Tag>(plan[PlanId(ygg::to_uint_t(position))]);
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
    ygg::Builder<ygg::database::Columns> columns;
    columns.push_back<double>(x);
    columns.push_back<bool>(y);
    Relation relation(columns.span());
    relation.insert(std::tuple { 0.0, false });
    relation.insert(std::tuple { -0.0, true });
    relation.insert(std::tuple { std::numeric_limits<double>::quiet_NaN(), false });
    const auto stats = ygg::database::collect_statistics(std::array { borrow(relation) });
    EXPECT_EQ(stats.inputs.at(0).rows, 3);
    EXPECT_EQ(stats.inputs.at(0).distinct.at(x), 2);
    EXPECT_EQ(stats.inputs.at(0).distinct.at(y), 2);
}

TEST(DatabaseOptimization, AveragesStatisticsOverSamplesAndWarnsAboutAlwaysEmptyKeys)
{
    ygg::Builder<ygg::database::Columns> columns;
    columns.push_back<double>(x);
    columns.push_back<double>(y);
    Relation two(columns.span()), four(columns.span()), empty(columns.span());
    two.insert(std::tuple { 0.0, 0.0 });
    two.insert(std::tuple { 0.0, 1.0 });
    for (int i = 0; i < 4; ++i)
        four.insert(std::tuple { static_cast<double>(i), 0.0 });

    auto accumulator = ygg::database::StatisticsAccumulator<std::string>();
    for (const auto* sample : { &two, &four, &empty })
    {
        accumulator.add("sometimes", borrow(*sample));
        accumulator.add("always", borrow(empty));
    }
    auto warnings = ygg::diagnostics::Warnings();
    const auto stats = accumulator.finish(warnings, [](const std::string& key) { return key; });

    EXPECT_EQ(stats.at("sometimes").rows, 2);                  // (2 + 4 + 0) / 3
    EXPECT_EQ(stats.at("sometimes").distinct.at(x), 5.0 / 3);  // (1 + 4 + 0) / 3
    EXPECT_EQ(stats.at("sometimes").distinct.at(y), 1);        // (2 + 1 + 0) / 3, clamped into [1, rows]
    EXPECT_EQ(stats.at("always").rows, 0);
    ASSERT_EQ(warnings.get().size(), 1);
    EXPECT_NE(warnings.get()[0].message.find("always was empty in all 3 samples"), std::string::npos);
}

TEST(DatabaseOptimization, RejectsInvalidStatistics)
{
    auto queries = ygg::tests::qb::repository();
    const auto input = ygg::tests::qb::input(queries, 0, { x });
    EXPECT_NO_THROW(ygg::database::optimize(input));
    ygg::database::Statistics statistics;
    statistics.inputs[0].rows = 3;
    EXPECT_NO_THROW(ygg::database::optimize(input, statistics));
    const auto valid = statistics;
    for (const double rows : { -1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN() })
    {
        statistics = valid;
        statistics.inputs[0].rows = rows;
        EXPECT_THROW(ygg::database::optimize(input, statistics), std::invalid_argument);
    }
    statistics = valid;
    statistics.inputs[0].distinct[x] = 4;
    EXPECT_THROW(ygg::database::optimize(input, statistics), std::invalid_argument);
    statistics = valid;
    statistics.inputs[0].distinct[z] = 1;
    EXPECT_THROW(ygg::database::optimize(input, statistics), std::invalid_argument);
}

TEST(DatabaseOptimization, GyoRecognizesAcyclicAndCyclicHypergraphs)
{
    using ygg::database::detail::join_tree;
    const auto path = join_tree({ labels({ x, y }), labels({ y, z }), labels({ z, w }) });
    ASSERT_TRUE(path);
    EXPECT_EQ(std::ranges::count_if(path.value(), [](const auto& parent) { return !parent; }), 1);
    EXPECT_TRUE(join_tree({ labels({ x, y }), labels({ x, z }), labels({ x, w }) }));
    EXPECT_TRUE(join_tree({ labels({ x, y, z }), labels({ x, y }), labels({ y, z }), labels({ z, x }) }));
    EXPECT_FALSE(join_tree({ labels({ x, y }), labels({ y, z }), labels({ z, x }) }));
    EXPECT_TRUE(join_tree({ labels({ x }), labels({ y }) }));
}

TEST(DatabaseOptimization, YannakakisBoundsIntermediatesByInputTimesOutput)
{
    auto queries = ygg::tests::qb::repository();
    const auto r = ygg::tests::qb::input(queries, 0, { x, y });
    const auto s = ygg::tests::qb::input(queries, 1, { y, z });
    const auto t = ygg::tests::qb::input(queries, 2, { z, w });
    const auto root = ygg::tests::qb::project(queries, ygg::tests::qb::join(queries, ygg::tests::qb::join(queries, r, s), t), { x });
    std::array<Relation, 3> inputs { Relation({ x, y }), Relation({ y, z }), Relation({ z, w }) };
    for (ygg::uint_t i = 0; i < 10; ++i)
    {
        inputs[0].insert(std::tuple { i, ygg::uint_t(0) });
        inputs[1].insert(std::tuple { ygg::uint_t(0), i });
        inputs[2].insert(std::tuple { i, ygg::uint_t(i % 2) });
        inputs[2].insert(std::tuple { i + 100, ygg::uint_t(0) });  // dangling
    }
    const auto refs = std::array { borrow(inputs[0]), borrow(inputs[1]), borrow(inputs[2]) };
    const auto plan = ygg::database::optimize(root);
    EXPECT_EQ(count<ygg::database::QueryGenericJoinTag>(plan), 0);
    // Evaluate every operator of the plan as its own root.
    std::vector<ygg::database::QueryView<>> nodes;
    nodes.reserve(plan.node_count());
    for (size_t position = 0; position < plan.node_count(); ++position)
        nodes.push_back(plan[PlanId(ygg::to_uint_t(position))]);
    ygg::database::QueryEvaluator evaluator(ygg::database::compile(std::span<const ygg::database::QueryView<>>(nodes)));
    evaluator.evaluate(refs);
    ygg::database::QueryEvaluator reference(ygg::database::compile({ root })), optimized(plan);
    reference.evaluate(refs);
    optimized.evaluate(refs);
    EXPECT_EQ(rows(optimized.get_result()), rows(reference.get_result()));
    // Yannakakis' bound for projected acyclic joins: O(|input| · |output|).
    const auto bound = (inputs[0].size() + inputs[1].size() + inputs[2].size()) * reference.get_result().size();
    for (size_t position = 0; position < nodes.size(); ++position)
        EXPECT_LE(evaluator.get_result(position).size(), bound);
}

TEST(DatabaseOptimization, BoundRootsKeepTheirJoinStructure)
{
    // π_{x,w}(R(x,y) ⋈ S(y,z) ⋈ T(z,w)), evaluated with x bound as the edges of a distance are. Yannakakis' semijoins
    // would flatten into extra atoms of every binding's plan; a bound root keeps its three.
    auto queries = ygg::tests::qb::repository();
    const auto r = ygg::tests::qb::input(queries, 0, { x, y });
    const auto s = ygg::tests::qb::input(queries, 1, { y, z });
    const auto t = ygg::tests::qb::input(queries, 2, { z, w });
    const auto root = ygg::tests::qb::project(queries, ygg::tests::qb::join(queries, ygg::tests::qb::join(queries, r, s), t), { x, w });
    const auto atoms = [](ygg::database::QueryView<> query)
    {
        size_t result = 0;
        for (const auto& branch : ygg::database::bound::union_normal_form(query).branches)
            result += branch.atoms.size();
        return result;
    };
    const auto roots = std::array { root };
    const auto bound = std::array { ygg::database::Evaluation::bound };
    const auto plan = ygg::database::optimize(std::span<const ygg::database::QueryView<>>(roots), std::span<const ygg::database::Evaluation>(bound));
    const auto planned = plan[plan.roots().front()];
    EXPECT_EQ(atoms(planned), 3);
    const auto full = ygg::database::optimize(root);
    EXPECT_GT(atoms(full[full.roots().front()]), 3);
    std::array<Relation, 3> inputs { Relation({ x, y }), Relation({ y, z }), Relation({ z, w }) };
    for (ygg::uint_t i = 0; i < 10; ++i)
    {
        inputs[0].insert(std::tuple { i % 3, i });
        inputs[1].insert(std::tuple { i, i % 4 });
        inputs[2].insert(std::tuple { i % 4, i });
    }
    const auto successors = [&](ygg::database::QueryView<> query)
    {
        auto evaluator = ygg::database::bound::QueryEvaluator(query);
        for (size_t slot = 0; slot < inputs.size(); ++slot)
            evaluator.bind_slot(slot, inputs[slot]);
        const auto binding = evaluator.prepare({ 0 });
        auto bytes = std::vector<std::byte>();
        auto result = std::vector<std::vector<std::byte>>();
        evaluator.evaluate(binding,
                           ygg::database::encode_row<ygg::uint_t>(std::array { ygg::uint_t(1) }, bytes),
                           [&](std::span<const std::byte> row)
                           {
                               result.emplace_back(row.begin(), row.end());
                               return true;
                           });
        std::ranges::sort(result);
        return result;
    };
    EXPECT_FALSE(successors(root).empty());
    EXPECT_EQ(successors(planned), successors(root));
    EXPECT_THROW((ygg::database::optimize(std::span<const ygg::database::QueryView<>>(roots), std::span<const ygg::database::Evaluation>())),
                 std::invalid_argument);
}

TEST(DatabaseOptimization, StructureChoosesGenericJoinOnlyForCyclicBlocks)
{
    auto queries = ygg::tests::qb::repository();
    const auto a = ygg::tests::qb::input(queries, 0, { x, y });
    const auto b = ygg::tests::qb::input(queries, 1, { y, z });
    const auto c = ygg::tests::qb::input(queries, 2, { z, x });
    const auto triangle = ygg::tests::qb::join(queries, ygg::tests::qb::join(queries, a, b), c);
    const auto path = ygg::tests::qb::join(queries, a, b);
    EXPECT_EQ(count<ygg::database::QueryGenericJoinTag>(ygg::database::optimize(triangle)), 1);
    EXPECT_EQ(count<ygg::database::QueryGenericJoinTag>(ygg::database::optimize(path)), 0);
    // A measured input alone does not make the block measured.
    ygg::database::Statistics partial;
    partial.inputs[0] = { 10, {} };
    EXPECT_EQ(count<ygg::database::QueryGenericJoinTag>(ygg::database::optimize(triangle, partial)), 1);
}

TEST(DatabaseOptimization, CostBasedPlanChoosesTheSelectiveSubjoin)
{
    auto queries = ygg::tests::qb::repository();
    const auto a = ygg::tests::qb::input(queries, 0, { x, y });
    const auto b = ygg::tests::qb::input(queries, 1, { y, z });
    const auto c = ygg::tests::qb::input(queries, 2, { z });
    const auto root = ygg::tests::qb::join(queries, ygg::tests::qb::join(queries, a, b), c);
    ygg::database::Statistics statistics;
    statistics.inputs[0] = { 10000, { { x, 10000 }, { y, 10000 } } };
    statistics.inputs[1] = { 10000, { { y, 10000 }, { z, 10000 } } };
    statistics.inputs[2] = { 1, { { z, 1 } } };
    const auto plan = ygg::database::optimize(root, statistics);
    EXPECT_EQ(count<ygg::database::QueryGenericJoinTag>(plan), 0);
    bool found = false;
    for (size_t position = 0; position < plan.node_count(); ++position)
        ygg::visit(
            [&]<typename Concrete>(Concrete query)
            {
                if constexpr (std::same_as<Concrete, ygg::database::QueryView<ygg::database::QueryJoinTag>>)
                {
                    std::set<size_t> slots;
                    for (const auto child : { query.get_lhs(), query.get_rhs() })
                        for (const auto node : ygg::database::reachable(std::span<const ygg::database::QueryView<>>(&child, 1)))
                            ygg::visit(
                                [&]<typename Leaf>(Leaf leaf)
                                {
                                    if constexpr (std::same_as<Leaf, ygg::database::QueryView<ygg::database::QueryInputTag>>)
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
    auto queries = ygg::tests::qb::repository();
    const auto a = ygg::tests::qb::input(queries, 0, { x, y });
    const auto b = ygg::tests::qb::input(queries, 1, { y, z });
    const auto c = ygg::tests::qb::input(queries, 2, { z, x });
    const auto root = ygg::tests::qb::join(queries, ygg::tests::qb::join(queries, a, b), c);
    // Every pairwise join grows: 10^4 · 10^4 / 100 rows from inputs of 10^4 rows.
    ygg::database::Statistics statistics;
    statistics.inputs[0] = { 10000, { { x, 100 }, { y, 100 } } };
    statistics.inputs[1] = { 10000, { { y, 100 }, { z, 100 } } };
    statistics.inputs[2] = { 10000, { { z, 100 }, { x, 100 } } };
    EXPECT_EQ(count<ygg::database::QueryGenericJoinTag>(ygg::database::optimize(root, statistics)), 1);
    // With key-like columns no join grows, and binary joins remain.
    for (auto& [slot, input] : statistics.inputs)
        for (auto& [column, count] : input.distinct)
            count = 10000;
    EXPECT_EQ(count<ygg::database::QueryGenericJoinTag>(ygg::database::optimize(root, statistics)), 0);
}

TEST(DatabaseOptimization, NullaryGenericJoinHasOneRow)
{
    auto queries = ygg::tests::qb::repository();
    const auto root = ygg::tests::qb::generic_join(queries, {}, {});
    for (const auto& plan : { ygg::database::optimize(root), ygg::database::optimize(root, ygg::database::Statistics { 4, {}, {} }) })
    {
        ygg::database::QueryEvaluator evaluator(plan);
        evaluator.evaluate(std::array<ygg::database::BorrowedRelationView, 0> {});
        EXPECT_EQ(evaluator.get_result().size(), 1);
    }
}

TEST(DatabaseOptimization, BothModesPreserveDifferencesRenamesAndExistentialVariables)
{
    auto queries = ygg::tests::qb::repository();
    const auto a = ygg::tests::qb::input(queries, 0, { x, y });
    const auto b = ygg::tests::qb::input(queries, 1, { x, y });
    const auto edges = ygg::tests::qb::input(queries, 2, { y, z });
    const auto projection = ygg::tests::qb::project(queries, a, { x });
    const auto renamed = ygg::tests::qb::rename(queries, a, { y, x });
    const auto selected = ygg::tests::qb::select_value(queries, renamed, y, ygg::uint_t(1));
    const auto roots = std::array { ygg::tests::qb::join(queries, projection, edges),
                                    ygg::tests::qb::project(queries, ygg::tests::qb::difference(queries, a, b), { x }),
                                    ygg::tests::qb::difference(queries, projection, ygg::tests::qb::project(queries, b, { x })),
                                    selected,
                                    ygg::tests::qb::project(queries, ygg::tests::qb::union_(queries, a, b), { y, x }),
                                    ygg::tests::qb::join(queries, ygg::tests::qb::union_(queries, a, b), edges),
                                    ygg::tests::qb::join(queries, a, ygg::tests::qb::project(queries, a, { x })),
                                    ygg::tests::qb::project(queries, ygg::tests::qb::join(queries, a, edges), { z, x }),
                                    ygg::tests::qb::project(queries, a, { x }),
                                    ygg::tests::qb::project(queries, a, { x }) };
    std::array<Relation, 3> inputs { Relation({ x, y }), Relation({ x, y }), Relation({ y, z }) };
    inputs[0].insert(std::tuple { ygg::uint_t(1), ygg::uint_t(2) });
    inputs[0].insert(std::tuple { ygg::uint_t(1), ygg::uint_t(3) });
    inputs[1].insert(std::tuple { ygg::uint_t(1), ygg::uint_t(2) });
    inputs[2].insert(std::tuple { ygg::uint_t(0), ygg::uint_t(2) });
    const auto refs = std::array { borrow(inputs[0]), borrow(inputs[1]), borrow(inputs[2]) };
    const auto statistics = ygg::database::collect_statistics(refs);
    const auto unmeasured = ygg::database::optimize(std::span<const ygg::database::QueryView<>>(roots));
    const auto measured = ygg::database::optimize(std::span<const ygg::database::QueryView<>>(roots), statistics);
    ygg::database::QueryEvaluator reference(ygg::database::compile(roots));
    reference.evaluate(refs);
    EXPECT_EQ(reference.get_result(0).size(), 1);
    EXPECT_EQ(reference.get_result(1).size(), 1);
    EXPECT_TRUE(reference.get_result(2).empty());
    for (const auto* chosen : { &unmeasured, &measured })
    {
        EXPECT_EQ(chosen->roots()[8], chosen->roots()[9]);
        ygg::database::QueryEvaluator full(*chosen);
        ygg::database::incremental::QueryEvaluator incremental(*chosen);
        full.evaluate(refs);
        incremental.initialize(refs);
        for (size_t root = 0; root < roots.size(); ++root)
        {
            EXPECT_EQ(rows(full.get_result(root)), rows(reference.get_result(root)));
            EXPECT_EQ(rows(incremental.get_result(root)), rows(reference.get_result(root)));
            EXPECT_TRUE(std::ranges::equal(full.get_result(root).columns().span(), roots[root].columns()));
        }
    }
    ygg::database::incremental::QueryEvaluator incremental(measured);
    incremental.initialize(refs);
    std::mt19937 random(1597);
    for (size_t iteration = 0; iteration < 35; ++iteration)
    {
        std::vector<decltype(rows(inputs[0]))> before;
        before.reserve(roots.size());
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
        changes.reserve(deltas.size());
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
    auto queries = ygg::tests::qb::repository();
    const auto a = ygg::tests::qb::input(queries, 0, { x, y });
    const auto b = ygg::tests::qb::input(queries, 1, { x, y });
    const auto c = ygg::tests::qb::input(queries, 2, { y, z });
    const auto e = ygg::tests::qb::input(queries, 3, { x, z });
    const auto leaves = std::array { a, b, c, e };
    std::array<Relation, 4> inputs { Relation({ x, y }), Relation({ x, y }), Relation({ y, z }), Relation({ x, z }) };
    const auto refs = std::array { borrow(inputs[0]), borrow(inputs[1]), borrow(inputs[2]), borrow(inputs[3]) };
    std::mt19937 random(12553);
    const auto labels = [](ygg::database::QueryView<> query)
    {
        std::vector<Column> result;
        for (const auto& column : query.columns())
            result.push_back(column.label);
        return result;
    };
    const auto peer = [&](ygg::database::QueryView<> query)
    {
        const auto target = labels(query);
        auto result = target.size() == 3 ? ygg::tests::qb::join(queries, a, c) : leaves[random() % leaves.size()];
        auto columns = labels(result);
        columns.resize(target.size());
        result = ygg::tests::qb::project(queries, result, columns);
        return ygg::tests::qb::rename(queries, result, target);
    };
    for (size_t expression = 0; expression < 64; ++expression)
    {
        auto root = leaves[random() % leaves.size()];
        for (size_t depth = 0; depth < 4; ++depth)
        {
            auto columns = labels(root);
            switch (random() % 9)
            {
                case 0:
                    root = ygg::tests::qb::join(queries, root, leaves[random() % leaves.size()]);
                    break;
                case 1:
                    std::shuffle(columns.begin(), columns.end(), random);
                    columns.resize(random() % (columns.size() + 1));
                    root = ygg::tests::qb::project(queries, root, columns);
                    break;
                case 2:
                {
                    std::array names { x, y, z };
                    std::shuffle(names.begin(), names.end(), random);
                    root = ygg::tests::qb::rename(queries, root, std::span<const Column>(names.data(), columns.size()));
                    break;
                }
                case 3:
                    if (!columns.empty())
                        root = ygg::tests::qb::select_equal(queries, root, columns[random() % columns.size()], columns[random() % columns.size()]);
                    break;
                case 4:
                    if (!columns.empty())
                        root = ygg::tests::qb::select_value(queries, root, columns[random() % columns.size()], ygg::uint_t(random() % 4));
                    break;
                case 5:
                    root = ygg::tests::qb::union_(queries, root, peer(root));
                    break;
                case 6:
                    root = ygg::tests::qb::difference(queries, root, peer(root));
                    break;
                case 7:
                    root = ygg::tests::qb::semijoin(queries, root, leaves[random() % leaves.size()]);
                    break;
                case 8:
                    root = ygg::tests::qb::antijoin(queries, root, leaves[random() % leaves.size()]);
                    break;
                default:
                    ADD_FAILURE() << "random() % 9 is below 9";
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
        SCOPED_TRACE(ygg::database::explain(baseline));
        const auto statistics = ygg::database::collect_statistics(refs);
        const auto unmeasured = ygg::database::optimize(root);
        const auto measured = ygg::database::optimize(root, statistics);
        ygg::database::QueryEvaluator reference(baseline);
        reference.evaluate(refs);
        for (const auto* optimized : { &unmeasured, &measured })
        {
            ygg::database::QueryEvaluator evaluator(*optimized);
            evaluator.evaluate(refs);
            EXPECT_TRUE(std::ranges::equal(evaluator.get_result().columns().span(), root.columns()));
            EXPECT_EQ(rows(evaluator.get_result()), rows(reference.get_result()));
        }
    }
}

TEST(DatabaseOptimization, BothModesPreserveFactoredUnionsAndFilteredDifferences)
{
    auto queries = ygg::tests::qb::repository();
    const auto a = ygg::tests::qb::input(queries, 0, { x, y });
    const auto b = ygg::tests::qb::input(queries, 1, { x, y });
    const auto c = ygg::tests::qb::input(queries, 2, { y, z });
    const auto selected_difference =
        ygg::tests::qb::select_value(queries, ygg::tests::qb::rename(queries, ygg::tests::qb::difference(queries, a, b), { y, x }), y, ygg::uint_t(1));
    const auto roots =
        std::array { ygg::tests::qb::union_(queries, ygg::tests::qb::join(queries, a, c), ygg::tests::qb::join(queries, b, c)),
                     ygg::tests::qb::join(queries, ygg::tests::qb::union_(queries, a, b), c),
                     selected_difference,
                     ygg::tests::qb::select_equal(queries, ygg::tests::qb::rename(queries, ygg::tests::qb::difference(queries, a, b), { y, x }), y, x),
                     ygg::tests::qb::union_(queries, a, ygg::tests::qb::join(queries, a, b)),
                     ygg::tests::qb::join(queries, a, ygg::tests::qb::union_(queries, a, b)) };
    Relation ar({ x, y }), br({ x, y }), cr({ y, z });
    ar.insert(std::tuple { ygg::uint_t(1), ygg::uint_t(1) });
    ar.insert(std::tuple { ygg::uint_t(1), ygg::uint_t(2) });
    br.insert(std::tuple { ygg::uint_t(1), ygg::uint_t(2) });
    br.insert(std::tuple { ygg::uint_t(2), ygg::uint_t(2) });
    cr.insert(std::tuple { ygg::uint_t(1), ygg::uint_t(3) });
    cr.insert(std::tuple { ygg::uint_t(2), ygg::uint_t(2) });
    const auto inputs = std::array { borrow(ar), borrow(br), borrow(cr) };
    const auto statistics = ygg::database::collect_statistics(inputs);
    const auto original = ygg::database::compile(roots);
    const auto unmeasured = ygg::database::optimize(std::span<const ygg::database::QueryView<>>(roots));
    const auto measured = ygg::database::optimize(std::span<const ygg::database::QueryView<>>(roots), statistics);
    ygg::database::QueryEvaluator reference(original);
    reference.evaluate(inputs);
    EXPECT_EQ(rows(reference.get_result(0)), rows(reference.get_result(1)));
    EXPECT_EQ(reference.get_result(2).size(), 1);
    EXPECT_EQ(reference.get_result(3).size(), 1);
    EXPECT_EQ(rows(reference.get_result(4)), rows(ar));
    EXPECT_EQ(rows(reference.get_result(5)), rows(ar));
    for (const auto* chosen : { &unmeasured, &measured })
    {
        ygg::database::QueryEvaluator full(*chosen);
        ygg::database::incremental::QueryEvaluator incremental(*chosen);
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
