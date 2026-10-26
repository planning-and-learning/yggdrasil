/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/semantics/aggregate.hpp"

#include "query_helpers.hpp"
#include "yggdrasil/database/semantics/incremental/aggregate.hpp"
#include "yggdrasil/database/semantics/incremental/query.hpp"
#include "yggdrasil/database/semantics/query_evaluation.hpp"

#include <algorithm>
#include <array>
#include <gtest/gtest.h>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <tuple>
#include <vector>

namespace ygg::tests
{
namespace
{
namespace db = database;
using Label = Index<db::Column>;
using Rows = std::set<std::vector<uint_t>>;
using Function = db::AggregateFunction;

const Label g = Label(0), x = Label(1), r = Label(2);
constexpr std::array functions = { Function::COUNT, Function::SUM, Function::MIN, Function::MAX };

Builder<db::Relation> relation(std::initializer_list<Label> labels, const Rows& rows)
{
    auto result = Builder<db::Relation>(labels);
    for (const auto& row : rows)
    {
        auto bytes = std::vector<std::byte>(row.size() * ByteCodec<uint_t>::size);
        for (size_t c = 0; c < row.size(); ++c)
            ByteCodec<uint_t>::encode(row[c], std::span(bytes).subspan(c * ByteCodec<uint_t>::size, ByteCodec<uint_t>::size));
        result.insert(std::span<const std::byte>(bytes));
    }
    return result;
}

Rows rows_of(const Builder<db::Relation>& relation)
{
    auto result = Rows();
    for (size_t i = 0; i < relation.size(); ++i)
    {
        auto row = std::vector<uint_t>(relation.arity());
        for (size_t c = 0; c < row.size(); ++c)
            row[c] = ByteCodec<uint_t>::decode(relation.row(i).subspan(c * ByteCodec<uint_t>::size, ByteCodec<uint_t>::size));
        result.insert(row);
    }
    return result;
}

/// The aggregate of each group of `groups` over the (g, x) rows of `arg`.
Rows naive(Function function, const Rows& arg, const Rows& groups)
{
    auto result = Rows();
    for (const auto& group : groups)
    {
        auto values = std::vector<uint_t>();
        for (const auto& row : arg)
            if (row[0] == group[0])
                values.push_back(row[1]);
        auto value = std::optional<uint_t>();
        switch (function)
        {
            case Function::COUNT:
                value = static_cast<uint_t>(values.size());
                break;
            case Function::SUM:
                value = 0;
                for (const auto v : values)
                    *value += v;
                break;
            case Function::MIN:
                if (!values.empty())
                    value = *std::ranges::min_element(values);
                break;
            case Function::MAX:
                if (!values.empty())
                    value = *std::ranges::max_element(values);
                break;
        }
        if (value)
            result.insert({ group[0], *value });
    }
    return result;
}

Rows random_rows(std::mt19937& rng, size_t arity, size_t count, uint_t domain)
{
    auto result = Rows();
    while (result.size() < count)
    {
        auto row = std::vector<uint_t>(arity);
        for (auto& v : row)
            v = static_cast<uint_t>(rng() % domain);
        result.insert(row);
    }
    return result;
}

Rows minus(const Rows& lhs, const Rows& rhs)
{
    auto result = Rows();
    std::ranges::set_difference(lhs, rhs, std::inserter(result, result.end()));
    return result;
}
}  // namespace

TEST(DatabaseAggregate, FullEvaluationMatchesTheDefinition)
{
    const auto arg_schema = relation({ g, x }, {});
    const auto groups_schema = relation({ g }, {});
    auto rng = std::mt19937(3);
    for (const auto function : functions)
        for (int trial = 0; trial < 20; ++trial)
        {
            const auto arg = random_rows(rng, 2, rng() % 12, 5), groups = random_rows(rng, 1, rng() % 5, 6);
            const auto plan = db::AggregatePlan(arg_schema.columns().span(), groups_schema.columns().span(), function, x, r);
            auto out = Builder<db::Relation>();
            db::aggregate(relation({ g, x }, arg), relation({ g }, groups), plan, out);
            EXPECT_EQ(rows_of(out), naive(function, arg, groups));
        }
}

TEST(DatabaseAggregate, IncrementalEvaluationMatchesFullAfterEveryChange)
{
    const auto arg_schema = relation({ g, x }, {});
    const auto groups_schema = relation({ g }, {});
    auto rng = std::mt19937(7);
    for (const auto function : functions)
    {
        const auto plan = db::AggregatePlan(arg_schema.columns().span(), groups_schema.columns().span(), function, x, r);
        auto evaluator = db::incremental::AggregateEvaluator(plan);
        auto arg = random_rows(rng, 2, 6, 4);
        auto groups = random_rows(rng, 1, 3, 5);
        evaluator.initialize(relation({ g, x }, arg), relation({ g }, groups));
        EXPECT_EQ(rows_of(evaluator.get_result()), naive(function, arg, groups));
        for (int step = 0; step < 200; ++step)
        {
            const auto next_arg = random_rows(rng, 2, rng() % 10, 4), next_groups = random_rows(rng, 1, rng() % 4, 5);
            const auto before = rows_of(evaluator.get_result());
            evaluator.update(std::make_tuple(relation({ g, x }, minus(next_arg, arg)), relation({ g, x }, minus(arg, next_arg))),
                             std::make_tuple(relation({ g }, minus(next_groups, groups)), relation({ g }, minus(groups, next_groups))));
            arg = next_arg;
            groups = next_groups;
            const auto after = naive(function, arg, groups);
            ASSERT_EQ(rows_of(evaluator.get_result()), after);
            EXPECT_EQ(rows_of(evaluator.get_delta().added), minus(after, before));
            EXPECT_EQ(rows_of(evaluator.get_delta().removed), minus(before, after));
        }
    }
}

TEST(DatabaseAggregate, QueryEvaluatorsAgree)
{
    auto repository = qb::repository();
    const auto arg = qb::input(repository, 0, { g, x });
    const auto groups = qb::input(repository, 1, { g });
    const auto count = qb::aggregate(repository, arg, groups, Function::COUNT, Label(), r);
    const auto maximum = qb::aggregate(repository, arg, groups, Function::MAX, x, r);
    const auto plan = db::compile({ count, maximum });
    auto full = db::QueryEvaluator(plan);
    auto incremental = db::incremental::QueryEvaluator(plan);
    auto rng = std::mt19937(11);
    auto arg_rows = random_rows(rng, 2, 6, 4);
    auto group_rows = random_rows(rng, 1, 3, 5);
    {
        const auto inputs = std::array { relation({ g, x }, arg_rows), relation({ g }, group_rows) };
        incremental.initialize(inputs);
    }
    for (int step = 0; step < 100; ++step)
    {
        const auto next_arg = random_rows(rng, 2, rng() % 10, 4), next_groups = random_rows(rng, 1, rng() % 4, 5);
        const auto changes = std::array { std::make_tuple(relation({ g, x }, minus(next_arg, arg_rows)), relation({ g, x }, minus(arg_rows, next_arg))),
                                          std::make_tuple(relation({ g }, minus(next_groups, group_rows)), relation({ g }, minus(group_rows, next_groups))) };
        incremental.update(changes);
        arg_rows = next_arg;
        group_rows = next_groups;
        const auto inputs = std::array { relation({ g, x }, arg_rows), relation({ g }, group_rows) };
        full.evaluate(inputs);
        EXPECT_EQ(rows_of(incremental.get_result(0)), naive(Function::COUNT, arg_rows, group_rows));
        EXPECT_EQ(rows_of(incremental.get_result(1)), naive(Function::MAX, arg_rows, group_rows));
        EXPECT_EQ(rows_of(full.get_result(0)), rows_of(incremental.get_result(0)));
        EXPECT_EQ(rows_of(full.get_result(1)), rows_of(incremental.get_result(1)));
    }
}
}  // namespace ygg::tests
