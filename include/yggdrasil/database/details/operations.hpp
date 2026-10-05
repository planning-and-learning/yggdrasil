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
void prepare_output(Builder<Relation<T>>& out, std::span<const Index<Column>> columns, std::initializer_list<const void*> inputs)
{
    if (!std::ranges::equal(out.columns().span(), columns))
        throw std::invalid_argument("Relational operation: output schema does not match.");
    for (const auto* input : inputs)
        if (input == out.get_storage_address())
            throw std::invalid_argument("Relational operation: output aliases an input.");
    out.clear();
}

template<TriviallyCopyable T, RelationViewConcept<T> L, RelationViewConcept<T> R>
void require_same_columns(const L& lhs, const R& rhs)
{
    if (!std::ranges::equal(lhs.columns().span(), rhs.columns().span()))
        throw std::invalid_argument("Relational operation: input schemas must have the same column order.");
}

inline void require_plan_columns(std::span<const Index<Column>> actual, std::span<const Index<Column>> expected)
{
    if (!std::ranges::equal(actual, expected))
        throw std::invalid_argument("Relational operation: input schema does not match prepared plan.");
}
}  // namespace detail

namespace detail
{
template<TriviallyCopyable T, RelationViewConcept<T> V>
void project_rows(const V& input, std::span<const Index<Column>> columns, std::span<const size_t> positions, Builder<Relation<T>>& out, Workspace<T>& workspace)
{
    detail::prepare_output(out, columns, { input.get_storage_address() });

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
        const auto source = input.row(i);
        row.clear();
        for (const auto position : positions)
            row.push_back(source[position]);
        out.insert(row);
    }
}

}  // namespace detail

template<TriviallyCopyable T, RelationViewConcept<T> V>
void project(const V& input, const ProjectionPlan& plan, Builder<Relation<T>>& out, Workspace<T>& workspace)
{
    detail::require_plan_columns(input.columns().span(), plan.input_columns().span());
    detail::project_rows(input, plan.output_columns().span(), plan.positions(), out, workspace);
}

template<TriviallyCopyable T, RelationViewConcept<T> V>
void project(const V& input, std::span<const Index<Column>> columns, Builder<Relation<T>>& out, Workspace<T>& workspace)
{
    detail::projection_positions(input.columns().span(), columns, workspace.lhs_keys);
    detail::project_rows(input, columns, workspace.lhs_keys, out, workspace);
}

template<TriviallyCopyable T, RelationViewConcept<T> V>
void project(const V& input, std::initializer_list<Index<Column>> columns, Builder<Relation<T>>& out, Workspace<T>& workspace)
{
    project(input, std::span<const Index<Column>>(columns), out, workspace);
}

template<TriviallyCopyable T, RelationViewConcept<T> V>
void project(const V& input, std::span<const Index<Column>> columns, Builder<Relation<T>>& out)
{
    auto workspace = Workspace<T>();
    project(input, columns, out, workspace);
}

template<TriviallyCopyable T, RelationViewConcept<T> V>
void project(const V& input, std::initializer_list<Index<Column>> columns, Builder<Relation<T>>& out)
{
    project(input, std::span<const Index<Column>>(columns), out);
}

template<TriviallyCopyable T, RelationViewConcept<T> V>
Builder<Relation<T>> project(const V& input, Builder<Columns> columns)
{
    auto out = Builder<Relation<T>>(std::move(columns));
    project(input, out.columns().span(), out);
    return out;
}

template<TriviallyCopyable T, RelationViewConcept<T> V>
Builder<Relation<T>> project(const V& input, std::span<const Index<Column>> columns)
{
    return project<T>(input, Builder<Columns>(columns));
}

template<TriviallyCopyable T, RelationViewConcept<T> V>
Builder<Relation<T>> project(const V& input, std::initializer_list<Index<Column>> columns)
{
    return project<T>(input, Builder<Columns>(columns));
}

template<TriviallyCopyable T, RelationViewConcept<T> V, typename Predicate>
    requires std::predicate<Predicate&, std::span<const T>>
void select(const V& input, Predicate predicate, Builder<Relation<T>>& out)
{
    detail::prepare_output(out, input.columns().span(), { input.get_storage_address() });
    for (size_t i = 0; i < input.size(); ++i)
        if (std::invoke(predicate, input.row(i)))
            out.insert(input.row(i));
}

template<TriviallyCopyable T, RelationViewConcept<T> V>
Builder<Relation<T>>& assign(Builder<Relation<T>>& destination, const V& source)
{
    if (destination.get_storage_address() == source.get_storage_address())
    {
        destination.rename(source.columns().span());
        return destination;
    }
    destination.initialize(source.columns().span());
    for (size_t i = 0; i < source.size(); ++i)
        destination.insert(source.row(i));
    return destination;
}

template<TriviallyCopyable T, RelationViewConcept<T> V, typename Predicate>
    requires std::predicate<Predicate&, std::span<const T>>
Builder<Relation<T>> select(const V& input, Predicate predicate)
{
    auto out = Builder<Relation<T>>(input.columns().span());
    select(input, std::move(predicate), out);
    return out;
}

template<TriviallyCopyable T, RelationViewConcept<T> V>
void select_equal_columns(const V& input, Index<Column> lhs, Index<Column> rhs, Builder<Relation<T>>& out)
{
    const auto lhs_index = input.column_index(lhs);
    const auto rhs_index = input.column_index(rhs);
    select(input, [=](std::span<const T> row) { return ygg::EqualTo<T> {}(row[lhs_index], row[rhs_index]); }, out);
}

template<TriviallyCopyable T, RelationViewConcept<T> V>
Builder<Relation<T>> select_equal_columns(const V& input, Index<Column> lhs, Index<Column> rhs)
{
    auto out = Builder<Relation<T>>(input.columns().span());
    select_equal_columns(input, lhs, rhs, out);
    return out;
}

template<TriviallyCopyable T, RelationViewConcept<T> V>
void select_equal_value(const V& input, Index<Column> column, const std::type_identity_t<T>& value, Builder<Relation<T>>& out)
{
    const auto index = input.column_index(column);
    select(input, [=](std::span<const T> row) { return ygg::EqualTo<T> {}(row[index], value); }, out);
}

template<TriviallyCopyable T, RelationViewConcept<T> V>
Builder<Relation<T>> select_equal_value(const V& input, Index<Column> column, const std::type_identity_t<T>& value)
{
    auto out = Builder<Relation<T>>(input.columns().span());
    select_equal_value(input, column, value, out);
    return out;
}

namespace detail
{
template<TriviallyCopyable T, RelationViewConcept<T> L, RelationViewConcept<T> R>
void join_rows(const L& lhs,
               const R& rhs,
               std::span<const Index<Column>> columns,
               std::span<const size_t> lhs_keys,
               std::span<const size_t> rhs_keys,
               std::span<const size_t> rhs_payload,
               bool build_left,
               const UnorderedMultiMap<hash_t, size_t>& index,
               Builder<Relation<T>>& out,
               Workspace<T>& workspace)
{
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
                emit(lhs.row(i), rhs.row(j));
        return;
    }

    const auto probe_rows = [&](const auto& build, const auto& probe, auto build_keys, auto probe_keys)
    {
        for (size_t i = 0; i < probe.size(); ++i)
        {
            const auto probe_row = probe.row(i);
            const auto probe_key = join_key_values(probe_row, probe_keys);
            for (const auto match : index.values(ygg::hash_range(probe_key)))
            {
                const auto build_row = build.row(match);
                if (!ygg::equal_range(join_key_values(build_row, build_keys), probe_key))
                    continue;
                const auto left = build_left ? build_row : probe_row;
                const auto right = build_left ? probe_row : build_row;
                emit(left, right);
            }
        }
    };
    if (build_left)
        probe_rows(lhs, rhs, lhs_keys, rhs_keys);
    else
        probe_rows(rhs, lhs, rhs_keys, lhs_keys);
}

template<TriviallyCopyable T, RelationViewConcept<T> L, RelationViewConcept<T> R>
void join_rows(const L& lhs,
               const R& rhs,
               std::span<const Index<Column>> columns,
               std::span<const size_t> lhs_keys,
               std::span<const size_t> rhs_keys,
               std::span<const size_t> rhs_payload,
               Builder<Relation<T>>& out,
               Workspace<T>& workspace)
{
    prepare_output(out, columns, { lhs.get_storage_address(), rhs.get_storage_address() });
    const bool build_left = lhs.size() <= rhs.size();
    if (!lhs_keys.empty() && !lhs.empty() && !rhs.empty())
    {
        if (build_left)
            build_join_index<T>(lhs, lhs_keys, workspace.join_index);
        else
            build_join_index<T>(rhs, rhs_keys, workspace.join_index);
    }
    join_rows(lhs, rhs, columns, lhs_keys, rhs_keys, rhs_payload, build_left, workspace.join_index, out, workspace);
}

}  // namespace detail

template<TriviallyCopyable T, RelationViewConcept<T> L, RelationViewConcept<T> R>
void join(const L& lhs, const R& rhs, const JoinPlan& plan, const JoinIndex<T>& index, Builder<Relation<T>>& out, Workspace<T>& workspace)
{
    detail::require_plan_columns(lhs.columns().span(), plan.lhs_columns().span());
    detail::require_plan_columns(rhs.columns().span(), plan.rhs_columns().span());
    const bool build_left = index.matches(lhs, plan.lhs_keys());
    if (!build_left && !index.matches(rhs, plan.rhs_keys()))
        throw std::invalid_argument("Relational operation: join index does not match either input's storage and keys.");
    detail::prepare_output(out, plan.output_columns().span(), { lhs.get_storage_address(), rhs.get_storage_address() });
    detail::join_rows(lhs, rhs, plan.output_columns().span(), plan.lhs_keys(), plan.rhs_keys(), plan.rhs_payload(), build_left, index.index(), out, workspace);
}

template<TriviallyCopyable T, RelationViewConcept<T> L, RelationViewConcept<T> R>
void join(const L& lhs, const R& rhs, const JoinPlan& plan, JoinIndexCache<T>& cache, JoinReuse reuse, Builder<Relation<T>>& out, Workspace<T>& workspace)
{
    detail::require_plan_columns(lhs.columns().span(), plan.lhs_columns().span());
    detail::require_plan_columns(rhs.columns().span(), plan.rhs_columns().span());
    bool build_left = lhs.size() <= rhs.size();
    if (reuse.lhs || reuse.rhs)
        build_left = reuse.lhs && (!reuse.rhs || build_left);
    if (!lhs.empty() && !rhs.empty() && !plan.lhs_keys().empty() && (reuse.lhs || reuse.rhs)
        && (build_left ? lhs.get_storage_index() : rhs.get_storage_index()) == std::numeric_limits<size_t>::max())
        throw std::invalid_argument("Relational operation: cached joins require factory-created inputs.");
    detail::prepare_output(out, plan.output_columns().span(), { lhs.get_storage_address(), rhs.get_storage_address() });
    if (lhs.empty() || rhs.empty())
        return;

    const auto* index = &workspace.join_index;
    if (!plan.lhs_keys().empty())
    {
        if (reuse.lhs || reuse.rhs)
        {
            index = &(build_left ? cache.get_or_create(lhs, plan.lhs_keys()) : cache.get_or_create(rhs, plan.rhs_keys())).index();
        }
        else if (build_left)
            detail::build_join_index<T>(lhs, plan.lhs_keys(), workspace.join_index);
        else
            detail::build_join_index<T>(rhs, plan.rhs_keys(), workspace.join_index);
    }
    detail::join_rows(lhs, rhs, plan.output_columns().span(), plan.lhs_keys(), plan.rhs_keys(), plan.rhs_payload(), build_left, *index, out, workspace);
}

template<TriviallyCopyable T, RelationViewConcept<T> L, RelationViewConcept<T> R>
void join(const L& lhs, const R& rhs, const JoinPlan& plan, Builder<Relation<T>>& out, Workspace<T>& workspace)
{
    detail::require_plan_columns(lhs.columns().span(), plan.lhs_columns().span());
    detail::require_plan_columns(rhs.columns().span(), plan.rhs_columns().span());
    detail::join_rows(lhs, rhs, plan.output_columns().span(), plan.lhs_keys(), plan.rhs_keys(), plan.rhs_payload(), out, workspace);
}

template<TriviallyCopyable T, RelationViewConcept<T> L, RelationViewConcept<T> R>
void join(const L& lhs, const R& rhs, Builder<Relation<T>>& out, Workspace<T>& workspace)
{
    detail::join_positions(lhs.columns().span(), rhs.columns().span(), workspace.columns, workspace.lhs_keys, workspace.rhs_keys, workspace.rhs_payload);
    detail::join_rows(lhs, rhs, workspace.columns, workspace.lhs_keys, workspace.rhs_keys, workspace.rhs_payload, out, workspace);
}

template<TriviallyCopyable T, RelationViewConcept<T> L, RelationViewConcept<T> R>
void join(const L& lhs, const R& rhs, Builder<Relation<T>>& out)
{
    auto workspace = Workspace<T>();
    join(lhs, rhs, out, workspace);
}

template<TriviallyCopyable T, RelationViewConcept<T> L, RelationViewConcept<T> R>
Builder<Relation<T>> join(const L& lhs, const R& rhs)
{
    auto workspace = Workspace<T>();
    detail::join_positions(lhs.columns().span(), rhs.columns().span(), workspace.columns, workspace.lhs_keys, workspace.rhs_keys, workspace.rhs_payload);
    auto out = Builder<Relation<T>>(workspace.columns);
    detail::join_rows(lhs, rhs, workspace.columns, workspace.lhs_keys, workspace.rhs_keys, workspace.rhs_payload, out, workspace);
    return out;
}

template<TriviallyCopyable T, RelationViewConcept<T> L, RelationViewConcept<T> R>
void union_(const L& lhs, const R& rhs, Builder<Relation<T>>& out)
{
    detail::require_same_columns<T>(lhs, rhs);
    detail::prepare_output(out, lhs.columns().span(), { lhs.get_storage_address(), rhs.get_storage_address() });
    for (size_t i = 0; i < lhs.size(); ++i)
        out.insert(lhs.row(i));
    for (size_t i = 0; i < rhs.size(); ++i)
        out.insert(rhs.row(i));
}

template<TriviallyCopyable T, RelationViewConcept<T> L, RelationViewConcept<T> R>
Builder<Relation<T>> union_(const L& lhs, const R& rhs)
{
    auto out = Builder<Relation<T>>(lhs.columns().span());
    union_(lhs, rhs, out);
    return out;
}

template<TriviallyCopyable T, RelationViewConcept<T> L, RelationViewConcept<T> R>
void difference(const L& lhs, const R& rhs, Builder<Relation<T>>& out)
{
    detail::require_same_columns<T>(lhs, rhs);
    detail::prepare_output(out, lhs.columns().span(), { lhs.get_storage_address(), rhs.get_storage_address() });
    for (size_t i = 0; i < lhs.size(); ++i)
        if (!rhs.contains(lhs.row(i)))
            out.insert(lhs.row(i));
}

template<TriviallyCopyable T, RelationViewConcept<T> L, RelationViewConcept<T> R>
Builder<Relation<T>> difference(const L& lhs, const R& rhs)
{
    auto out = Builder<Relation<T>>(lhs.columns().span());
    difference(lhs, rhs, out);
    return out;
}

}  // namespace ygg::database

#endif
