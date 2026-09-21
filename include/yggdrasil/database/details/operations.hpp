/*
 * Copyright (C) 2026 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef YGG_DATABASE_DETAILS_OPERATIONS_HPP_
#define YGG_DATABASE_DETAILS_OPERATIONS_HPP_

#include "yggdrasil/database/operations.hpp"
#include "yggdrasil/semantics/equal_to.hpp"
#include "yggdrasil/semantics/hash.hpp"

#include <algorithm>
#include <functional>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace ygg::database
{

namespace detail
{
template<TriviallyCopyable T>
void prepare_output(Relation<T>& out, std::span<const Column> columns, std::initializer_list<const RawArraySet<T>*> inputs)
{
    if (!std::ranges::equal(out.columns(), columns))
        throw std::invalid_argument("Relational operation: output schema does not match.");
    for (const auto* input : inputs)
        if (input == &out.storage())
            throw std::invalid_argument("Relational operation: output aliases an input.");
    out.clear();
}

template<TriviallyCopyable T>
void require_same_columns(const RelationView<T>& lhs, const RelationView<T>& rhs)
{
    if (!std::ranges::equal(lhs.columns(), rhs.columns()))
        throw std::invalid_argument("Relational operation: input schemas must have the same column order.");
}

inline void require_plan_columns(std::span<const Column> actual, std::span<const Column> expected)
{
    if (!std::ranges::equal(actual, expected))
        throw std::invalid_argument("Relational operation: input schema does not match prepared plan.");
}
}  // namespace detail

template<TriviallyCopyable T>
RelationView<T> rename(const RelationView<T>& input, ColumnsView columns)
{
    return RelationView<T>(input.storage(), columns);
}

template<TriviallyCopyable T, typename C, size_t Extent>
    requires std::same_as<std::remove_const_t<C>, Column>
RelationView<T> rename(const RelationView<T>& input, std::span<C, Extent> columns)
{
    return RelationView<T>(input.storage(), columns);
}

namespace detail
{
template<TriviallyCopyable T>
void project_rows(const RelationView<T>& input, std::span<const Column> columns, std::span<const size_t> positions, Relation<T>& out, Workspace<T>& workspace)
{
    detail::prepare_output(out, columns, { &input.storage() });

    if (positions.empty())
    {
        if (!input.empty())
            out.insert({});
        return;
    }

    auto& row = workspace.row;
    row.reserve(positions.size());
    for (size_t i = 0; i < input.size(); ++i)
    {
        const auto source = input[i];
        row.clear();
        for (const auto position : positions)
            row.push_back(source[position]);
        out.insert(row);
    }
}

}  // namespace detail

template<TriviallyCopyable T>
void project(const RelationView<T>& input, const ProjectionPlan& plan, Relation<T>& out, Workspace<T>& workspace)
{
    detail::require_plan_columns(input.columns(), plan.input_columns());
    detail::project_rows(input, plan.output_columns(), plan.positions(), out, workspace);
}

template<TriviallyCopyable T>
void project(const RelationView<T>& input, ColumnsView columns, Relation<T>& out, Workspace<T>& workspace)
{
    detail::projection_positions(input.columns(), columns, workspace.lhs_keys);
    detail::project_rows(input, columns, workspace.lhs_keys, out, workspace);
}

template<TriviallyCopyable T>
void project(const RelationView<T>& input, std::span<const Column> columns, Relation<T>& out, Workspace<T>& workspace)
{
    project(input, ColumnsView(columns), out, workspace);
}

template<TriviallyCopyable T>
void project(const RelationView<T>& input, std::initializer_list<Column> columns, Relation<T>& out, Workspace<T>& workspace)
{
    project(input, std::span<const Column>(columns), out, workspace);
}

template<TriviallyCopyable T>
void project(const RelationView<T>& input, ColumnsView columns, Relation<T>& out)
{
    auto workspace = Workspace<T>();
    project(input, columns, out, workspace);
}

template<TriviallyCopyable T>
void project(const RelationView<T>& input, std::span<const Column> columns, Relation<T>& out)
{
    project(input, ColumnsView(columns), out);
}

template<TriviallyCopyable T>
void project(const RelationView<T>& input, std::initializer_list<Column> columns, Relation<T>& out)
{
    project(input, std::span<const Column>(columns), out);
}

template<TriviallyCopyable T>
Relation<T> project(const RelationView<T>& input, Columns columns)
{
    auto out = Relation<T>(std::move(columns));
    project(input, out.columns(), out);
    return out;
}

template<TriviallyCopyable T>
Relation<T> project(const RelationView<T>& input, ColumnsView columns)
{
    return project(input, Columns(columns));
}

template<TriviallyCopyable T>
Relation<T> project(const RelationView<T>& input, const std::vector<Column>& columns)
{
    return project(input, Columns(columns));
}

template<TriviallyCopyable T>
Relation<T> project(const RelationView<T>& input, std::initializer_list<Column> columns)
{
    return project(input, Columns(columns));
}

template<TriviallyCopyable T, typename Predicate>
    requires std::predicate<Predicate&, std::span<const T>>
void select(const RelationView<T>& input, Predicate predicate, Relation<T>& out)
{
    detail::prepare_output(out, input.columns(), { &input.storage() });
    for (size_t i = 0; i < input.size(); ++i)
        if (std::invoke(predicate, input[i]))
            out.insert(input[i]);
}

template<TriviallyCopyable T, typename Predicate>
    requires std::predicate<Predicate&, std::span<const T>>
Relation<T> select(const RelationView<T>& input, Predicate predicate)
{
    auto out = Relation<T>(input.columns());
    select(input, std::move(predicate), out);
    return out;
}

template<TriviallyCopyable T>
void select_equal_columns(const RelationView<T>& input, Column lhs, Column rhs, Relation<T>& out)
{
    const auto lhs_index = input.column_index(lhs);
    const auto rhs_index = input.column_index(rhs);
    select(input, [=](std::span<const T> row) { return ygg::EqualTo<T> {}(row[lhs_index], row[rhs_index]); }, out);
}

template<TriviallyCopyable T>
Relation<T> select_equal_columns(const RelationView<T>& input, Column lhs, Column rhs)
{
    auto out = Relation<T>(input.columns());
    select_equal_columns(input, lhs, rhs, out);
    return out;
}

template<TriviallyCopyable T>
void select_equal_value(const RelationView<T>& input, Column column, const std::type_identity_t<T>& value, Relation<T>& out)
{
    const auto index = input.column_index(column);
    select(input, [=](std::span<const T> row) { return ygg::EqualTo<T> {}(row[index], value); }, out);
}

template<TriviallyCopyable T>
Relation<T> select_equal_value(const RelationView<T>& input, Column column, const std::type_identity_t<T>& value)
{
    auto out = Relation<T>(input.columns());
    select_equal_value(input, column, value, out);
    return out;
}

namespace detail
{
template<TriviallyCopyable T>
void join_rows(const RelationView<T>& lhs,
               const RelationView<T>& rhs,
               std::span<const Column> columns,
               std::span<const size_t> lhs_keys,
               std::span<const size_t> rhs_keys,
               std::span<const size_t> rhs_payload,
               Relation<T>& out,
               Workspace<T>& workspace)
{
    prepare_output(out, columns, { &lhs.storage(), &rhs.storage() });
    if (lhs.empty() || rhs.empty())
        return;

    auto& row = workspace.row;
    row.reserve(columns.size());
    const auto emit = [&](std::span<const T> left, std::span<const T> right)
    {
        row.assign(left.begin(), left.end());
        for (const auto position : rhs_payload)
            row.push_back(right[position]);
        out.insert(row);
    };

    if (lhs_keys.empty())
    {
        for (size_t i = 0; i < lhs.size(); ++i)
            for (size_t j = 0; j < rhs.size(); ++j)
                emit(lhs[i], rhs[j]);
        return;
    }

    const bool build_left = lhs.size() <= rhs.size();
    const auto& build = build_left ? lhs : rhs;
    const auto& probe = build_left ? rhs : lhs;
    const auto& build_keys = build_left ? lhs_keys : rhs_keys;
    const auto& probe_keys = build_left ? rhs_keys : lhs_keys;
    const auto key_values = [](std::span<const T> tuple, std::span<const size_t> positions)
    {
        return positions | std::views::transform([tuple](size_t position) -> const T& { return tuple[position]; });
    };

    auto& index = workspace.join_index;
    index.clear();
    index.reserve(build.size());
    for (size_t i = 0; i < build.size(); ++i)
        index.insert(ygg::hash_range(key_values(build[i], build_keys)), i);

    for (size_t i = 0; i < probe.size(); ++i)
    {
        const auto probe_row = probe[i];
        const auto probe_key = key_values(probe_row, probe_keys);
        for (const auto match : index.values(ygg::hash_range(probe_key)))
        {
            const auto build_row = build[match];
            if (!ygg::equal_range(key_values(build_row, build_keys), probe_key))
                continue;
            const auto left = build_left ? build_row : probe_row;
            const auto right = build_left ? probe_row : build_row;
            emit(left, right);
        }
    }
}

}  // namespace detail

template<TriviallyCopyable T>
void join(const RelationView<T>& lhs, const RelationView<T>& rhs, const JoinPlan& plan, Relation<T>& out, Workspace<T>& workspace)
{
    detail::require_plan_columns(lhs.columns(), plan.lhs_columns());
    detail::require_plan_columns(rhs.columns(), plan.rhs_columns());
    detail::join_rows(lhs, rhs, plan.output_columns(), plan.lhs_keys(), plan.rhs_keys(), plan.rhs_payload(), out, workspace);
}

template<TriviallyCopyable T>
void join(const RelationView<T>& lhs, const RelationView<T>& rhs, Relation<T>& out, Workspace<T>& workspace)
{
    detail::join_positions(lhs.columns(), rhs.columns(), workspace.columns, workspace.lhs_keys, workspace.rhs_keys, workspace.rhs_payload);
    detail::join_rows(lhs, rhs, workspace.columns, workspace.lhs_keys, workspace.rhs_keys, workspace.rhs_payload, out, workspace);
}

template<TriviallyCopyable T>
void join(const RelationView<T>& lhs, const RelationView<T>& rhs, Relation<T>& out)
{
    auto workspace = Workspace<T>();
    join(lhs, rhs, out, workspace);
}

template<TriviallyCopyable T>
Relation<T> join(const RelationView<T>& lhs, const RelationView<T>& rhs)
{
    auto workspace = Workspace<T>();
    detail::join_positions(lhs.columns(), rhs.columns(), workspace.columns, workspace.lhs_keys, workspace.rhs_keys, workspace.rhs_payload);
    auto out = Relation<T>(workspace.columns);
    detail::join_rows(lhs, rhs, workspace.columns, workspace.lhs_keys, workspace.rhs_keys, workspace.rhs_payload, out, workspace);
    return out;
}

template<TriviallyCopyable T>
void union_(const RelationView<T>& lhs, const RelationView<T>& rhs, Relation<T>& out)
{
    detail::require_same_columns(lhs, rhs);
    detail::prepare_output(out, lhs.columns(), { &lhs.storage(), &rhs.storage() });
    for (size_t i = 0; i < lhs.size(); ++i)
        out.insert(lhs[i]);
    for (size_t i = 0; i < rhs.size(); ++i)
        out.insert(rhs[i]);
}

template<TriviallyCopyable T>
Relation<T> union_(const RelationView<T>& lhs, const RelationView<T>& rhs)
{
    auto out = Relation<T>(lhs.columns());
    union_(lhs, rhs, out);
    return out;
}

template<TriviallyCopyable T>
void difference(const RelationView<T>& lhs, const RelationView<T>& rhs, Relation<T>& out)
{
    detail::require_same_columns(lhs, rhs);
    detail::prepare_output(out, lhs.columns(), { &lhs.storage(), &rhs.storage() });
    for (size_t i = 0; i < lhs.size(); ++i)
        if (!rhs.contains(lhs[i]))
            out.insert(lhs[i]);
}

template<TriviallyCopyable T>
Relation<T> difference(const RelationView<T>& lhs, const RelationView<T>& rhs)
{
    auto out = Relation<T>(lhs.columns());
    difference(lhs, rhs, out);
    return out;
}

}  // namespace ygg::database

#endif
