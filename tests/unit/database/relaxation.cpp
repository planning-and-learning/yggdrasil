#include "query_helpers.hpp"

#include <array>
#include <gtest/gtest.h>
#include <random>
#include <vector>
#include <yggdrasil/database/semantics/operations.hpp>
#include <yggdrasil/database/semantics/query_evaluation.hpp>
#include <yggdrasil/database/semantics/relation_repository.hpp>
#include <yggdrasil/database/syntax/formatter.hpp>
#include <yggdrasil/database/syntax/translations/relaxation.hpp>

namespace
{
using Column = ygg::Index<ygg::database::Column>;
using Relation = ygg::Builder<ygg::database::Relation<>>;
using View = ygg::database::QueryView<>;
namespace qb = ygg::tests::qb;
const Column x(0), y(1), z(2);

ygg::database::BorrowedRelationView<> borrow(const Relation& relation)
{
    static const auto context = ygg::database::RelationRepositoryFactory<> {}.create();
    return { relation, context };
}

/// Evaluates a query over the given inputs.
Relation evaluate(View query, const std::vector<const Relation*>& inputs)
{
    auto bindings = std::vector<ygg::database::BorrowedRelationView<>>();
    for (const auto* input : inputs)
        bindings.push_back(borrow(*input));
    auto evaluator = ygg::database::QueryEvaluator<>(ygg::database::compile({ query }));
    evaluator.evaluate(bindings);
    auto result = Relation(evaluator.get_result().columns().span());
    ygg::database::assign(result, evaluator.get_result());
    return result;
}

struct Relaxed
{
    ygg::database::QueryRepository<> repository = qb::repository<>();
    ygg::database::Relaxation<> relaxation;
};

std::unique_ptr<Relaxed> relax(std::span<const View> roots, const std::array<bool, 4>& known)
{
    auto result = std::make_unique<Relaxed>();
    result->relaxation = ygg::database::relax(roots, [&](size_t slot) { return known[slot]; }, result->repository, qb::builder<ygg::database::DefaultColumnTypes>());
    return result;
}
std::unique_ptr<Relaxed> relax(View query, const std::array<bool, 4>& known) { return relax(std::span<const View>(&query, 1), known); }

/// Q(I) ⊆ Q̂(I) × U^(free labels): Q's rows projected onto Q̂'s labels are rows of Q̂.
void expect_superset(View query, const ygg::database::Relaxation<>& relaxation, size_t root, const std::array<Relation, 4>& inputs)
{
    const auto& relaxed_query = relaxation.queries[root];
    if (!relaxed_query)
        return;
    const auto full = evaluate(query, { &inputs[0], &inputs[1], &inputs[2], &inputs[3] });
    auto known = std::vector<const Relation*>();
    for (const auto slot : relaxation.slots)
        known.push_back(&inputs[slot]);
    const auto relaxed = evaluate(*relaxed_query, known);
    const auto labels = ygg::database::column_labels(relaxed_query->columns());
    for (const auto& column : relaxed_query->columns())
        EXPECT_TRUE(ygg::database::contains_column(query.columns(), column.label));
    const auto projected = ygg::database::project<ygg::database::DefaultColumnTypes>(full, std::span<const Column>(labels));
    for (size_t i = 0; i < projected.size(); ++i)
        EXPECT_TRUE(relaxed.contains(projected.row(i)));
}

TEST(DatabaseRelaxation, RandomExpressionsAreOverApproximatedAndExactWhenKnown)
{
    auto queries = qb::repository<>();
    const auto a = qb::input(queries, 0, { x, y });
    const auto b = qb::input(queries, 1, { x, y });
    const auto c = qb::input(queries, 2, { y, z });
    const auto e = qb::input(queries, 3, { x, z });
    const auto leaves = std::array { a, b, c, e };
    auto inputs = std::array<Relation, 4> { Relation({ x, y }), Relation({ x, y }), Relation({ y, z }), Relation({ x, z }) };
    auto random = std::mt19937(4711);
    const auto labels_of = [](View query)
    {
        auto result = std::vector<Column>();
        for (const auto& column : query.columns())
            result.push_back(column.label);
        return result;
    };
    const auto peer = [&](View query)
    {
        const auto target = labels_of(query);
        auto result = target.size() == 3 ? qb::join(queries, a, c) : leaves[random() % leaves.size()];
        auto columns = labels_of(result);
        columns.resize(target.size());
        result = qb::project(queries, result, std::span<const Column>(columns));
        return qb::rename(queries, result, std::span<const Column>(target));
    };
    for (size_t expression = 0; expression < 128; ++expression)
    {
        auto root = leaves[random() % leaves.size()];
        // Exactness is lost once a projection eliminates labels a difference or antijoin below it shares.
        auto negated = false;
        auto exact = true;
        for (size_t depth = 0; depth < 4; ++depth)
        {
            auto columns = labels_of(root);
            const auto operation = random() % 9;
            negated = negated || operation == 6 || operation == 8;
            exact = exact && !(operation == 1 && negated);
            switch (operation)
            {
                case 0:
                    root = qb::join(queries, root, leaves[random() % leaves.size()]);
                    break;
                case 1:
                    std::shuffle(columns.begin(), columns.end(), random);
                    columns.resize(random() % (columns.size() + 1));
                    root = qb::project(queries, root, std::span<const Column>(columns));
                    break;
                case 2:
                {
                    auto names = std::array { x, y, z };
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
                case 7:
                    root = qb::semijoin(queries, root, leaves[random() % leaves.size()]);
                    break;
                case 8:
                    root = qb::antijoin(queries, root, leaves[random() % leaves.size()]);
                    break;
            }
        }
        SCOPED_TRACE(expression);
        SCOPED_TRACE(ygg::database::explain(ygg::database::compile({ root })));
        for (auto& input : inputs)
        {
            input.clear();
            for (ygg::uint_t left = 0; left < 4; ++left)
                for (ygg::uint_t right = 0; right < 4; ++right)
                    if (random() % 3 != 0)
                        input.insert(std::tuple { left, right });
        }
        const auto known = std::array { bool(random() % 2), bool(random() % 2), bool(random() % 2), bool(random() % 2) };
        // The root and a projection of it onto random labels, relaxed together.
        auto kept = labels_of(root);
        std::shuffle(kept.begin(), kept.end(), random);
        kept.resize(random() % (kept.size() + 1));
        const auto roots = std::array { root, qb::project(queries, root, std::span<const Column>(kept)) };
        const auto partial = relax(roots, known);
        expect_superset(roots[0], partial->relaxation, 0, inputs);
        expect_superset(roots[1], partial->relaxation, 1, inputs);

        if (!exact)
            continue;
        const auto all_known = relax(root, { true, true, true, true });
        ASSERT_TRUE(all_known->relaxation.queries[0].has_value());
        EXPECT_EQ(ygg::database::column_labels(all_known->relaxation.queries[0]->columns()), labels_of(root));
        auto all = std::vector<const Relation*>();
        for (const auto slot : all_known->relaxation.slots)
            all.push_back(&inputs[slot]);
        const auto expected = evaluate(root, { &inputs[0], &inputs[1], &inputs[2], &inputs[3] });
        const auto actual = evaluate(*all_known->relaxation.queries[0], all);
        EXPECT_EQ(actual.size(), expected.size());
        for (size_t i = 0; i < expected.size(); ++i)
            EXPECT_TRUE(actual.contains(expected.row(i)));
    }
}

TEST(DatabaseRelaxation, UnknownInputsDropConstraints)
{
    auto queries = qb::repository<>();
    const auto known_edges = qb::input(queries, 0, { x, y });
    const auto unknown = qb::input(queries, 1, { y, z });
    const auto blocked = qb::input(queries, 2, { x, y });
    const auto both_known = std::array { true, false, true, true };

    // An unknown conjunct drops out of a join; its label z is unconstrained.
    const auto joined = relax(qb::join(queries, known_edges, unknown), both_known);
    ASSERT_TRUE(joined->relaxation.queries[0].has_value());
    EXPECT_EQ(ygg::database::column_labels(joined->relaxation.queries[0]->columns()), (std::vector<Column> { x, y }));
    EXPECT_EQ(joined->relaxation.slots, (std::vector<size_t> { 0 }));

    // A union with an unknown side constrains nothing.
    const auto renamed = qb::rename(queries, unknown, { x, y });
    EXPECT_FALSE(relax(qb::union_(queries, known_edges, renamed), both_known)->relaxation.queries[0].has_value());

    // A known antijoin rhs stays: the result is exact.
    const auto anti = relax(qb::antijoin(queries, known_edges, blocked), both_known);
    ASSERT_TRUE(anti->relaxation.queries[0].has_value());
    EXPECT_TRUE(ygg::database::is<ygg::database::QueryAntijoinTag>(*anti->relaxation.queries[0]));

    // A selection on an unconstrained column is dropped.
    const auto selected = relax(qb::select_value(queries, qb::join(queries, known_edges, unknown), z, ygg::uint_t(1)), both_known);
    ASSERT_TRUE(selected->relaxation.queries[0].has_value());
    EXPECT_TRUE(ygg::database::is<ygg::database::QueryJoinTag>(*selected->relaxation.queries[0]) || ygg::database::is<ygg::database::QueryInputTag>(*selected->relaxation.queries[0]));
}

TEST(DatabaseRelaxation, ProjectedJoinEliminatesUnneededConjunctsAndAntijoins)
{
    // π_{x,y}((E(x,y) ⋈ I(u,v)) ▷ I(y,u)): I(u,v) shares nothing needed, and the antijoin filters on u, which is
    // eliminated, so the relaxation is E with I(u,v) reduced to an existence test, without a product.
    auto queries = qb::repository<>();
    const Column u(3), v(4);
    const auto edges = qb::input(queries, 0, { x, y });
    const auto identity = qb::input(queries, 1, { u, v });
    const auto blocked = qb::rename(queries, identity, { y, u });
    const auto walk = qb::antijoin(queries, qb::join(queries, edges, identity), blocked);
    const auto pattern = qb::project(queries, walk, { x, y });
    const auto relaxed = relax(pattern, { true, true, true, true });
    ASSERT_TRUE(relaxed->relaxation.queries[0].has_value());
    const auto plan = ygg::database::compile({ *relaxed->relaxation.queries[0] });
    for (size_t i = 0; i < plan.node_count(); ++i)
    {
        const auto node = plan[ygg::Index<ygg::database::Query<>>(ygg::to_uint_t(i))];
        EXPECT_FALSE(ygg::database::is<ygg::database::QueryAntijoinTag>(node));
        // Every join has a nullary side: the existence test of I.
        if (ygg::database::is<ygg::database::QueryJoinTag>(node))
        {
            const auto join = ygg::database::as<ygg::database::QueryJoinTag>(node);
            EXPECT_TRUE(join.get_lhs().columns().empty() || join.get_rhs().columns().empty());
        }
    }

    // An antijoin whose shared labels are all needed stays exact.
    const auto kept = relax(qb::antijoin(queries, edges, qb::rename(queries, identity, { x, y })), { true, true, true, true });
    ASSERT_TRUE(kept->relaxation.queries[0].has_value());
    EXPECT_TRUE(ygg::database::is<ygg::database::QueryAntijoinTag>(*kept->relaxation.queries[0]));
}

TEST(DatabaseRelaxation, RootsShareOneInputNumbering)
{
    auto queries = qb::repository<>();
    const auto a = qb::input(queries, 0, { x, y });
    const auto c = qb::input(queries, 2, { y, z });
    const auto joined = qb::join(queries, a, c);
    const auto roots = std::array { qb::project(queries, joined, { x }), qb::project(queries, c, { z }) };
    const auto relaxed = relax(roots, { true, false, true, false });
    ASSERT_TRUE(relaxed->relaxation.queries[0].has_value() && relaxed->relaxation.queries[1].has_value());
    EXPECT_EQ(relaxed->relaxation.slots, (std::vector<size_t> { 0, 2 }));
    auto inputs = std::array<Relation, 4> { Relation({ x, y }), Relation({ x, y }), Relation({ y, z }), Relation({ x, z }) };
    inputs[0].insert(std::tuple { ygg::uint_t(1), ygg::uint_t(2) });
    inputs[2].insert(std::tuple { ygg::uint_t(2), ygg::uint_t(3) });
    const auto plan = ygg::database::compile({ *relaxed->relaxation.queries[0], *relaxed->relaxation.queries[1] });
    auto evaluator = ygg::database::QueryEvaluator<>(plan);
    evaluator.evaluate(std::array { borrow(inputs[0]), borrow(inputs[2]) });
    EXPECT_EQ(evaluator.get_result(0).size(), 1);
    EXPECT_EQ(evaluator.get_result(1).size(), 1);
}
}  // namespace
