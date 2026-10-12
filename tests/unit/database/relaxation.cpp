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

std::unique_ptr<Relaxed> relax(View query, const std::array<bool, 4>& known)
{
    auto result = std::make_unique<Relaxed>();
    result->relaxation = ygg::database::relax(query, [&](size_t slot) { return known[slot]; }, result->repository, qb::builder<ygg::database::DefaultColumnTypes>());
    return result;
}

/// Q(I) ⊆ Q̂(I) × U^(free labels): Q's rows projected onto Q̂'s labels are rows of Q̂.
void expect_superset(View query, const ygg::database::Relaxation<>& relaxation, const std::array<Relation, 4>& inputs)
{
    if (!relaxation.query)
        return;
    const auto full = evaluate(query, { &inputs[0], &inputs[1], &inputs[2], &inputs[3] });
    auto known = std::vector<const Relation*>();
    for (const auto slot : relaxation.slots)
        known.push_back(&inputs[slot]);
    const auto relaxed = evaluate(*relaxation.query, known);
    const auto labels = ygg::database::column_labels(relaxation.query->columns());
    for (const auto& column : relaxation.query->columns())
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
        for (size_t depth = 0; depth < 4; ++depth)
        {
            auto columns = labels_of(root);
            switch (random() % 9)
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
        const auto partial = relax(root, known);
        expect_superset(root, partial->relaxation, inputs);

        const auto exact = relax(root, { true, true, true, true });
        ASSERT_TRUE(exact->relaxation.query.has_value());
        EXPECT_EQ(ygg::database::column_labels(exact->relaxation.query->columns()), labels_of(root));
        auto all = std::vector<const Relation*>();
        for (const auto slot : exact->relaxation.slots)
            all.push_back(&inputs[slot]);
        const auto expected = evaluate(root, { &inputs[0], &inputs[1], &inputs[2], &inputs[3] });
        const auto actual = evaluate(*exact->relaxation.query, all);
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
    ASSERT_TRUE(joined->relaxation.query.has_value());
    EXPECT_EQ(ygg::database::column_labels(joined->relaxation.query->columns()), (std::vector<Column> { x, y }));
    EXPECT_EQ(joined->relaxation.slots, (std::vector<size_t> { 0 }));

    // A union with an unknown side constrains nothing.
    const auto renamed = qb::rename(queries, unknown, { x, y });
    EXPECT_FALSE(relax(qb::union_(queries, known_edges, renamed), both_known)->relaxation.query.has_value());

    // A known antijoin rhs stays: the result is exact.
    const auto anti = relax(qb::antijoin(queries, known_edges, blocked), both_known);
    ASSERT_TRUE(anti->relaxation.query.has_value());
    EXPECT_TRUE(ygg::database::is<ygg::database::QueryAntijoinTag>(*anti->relaxation.query));

    // A selection on an unconstrained column is dropped.
    const auto selected = relax(qb::select_value(queries, qb::join(queries, known_edges, unknown), z, ygg::uint_t(1)), both_known);
    ASSERT_TRUE(selected->relaxation.query.has_value());
    EXPECT_TRUE(ygg::database::is<ygg::database::QueryJoinTag>(*selected->relaxation.query) || ygg::database::is<ygg::database::QueryInputTag>(*selected->relaxation.query));
}
}  // namespace
