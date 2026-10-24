/*
 * Copyright (C) 2026 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef YGG_DATABASE_SEMANTICS_DETAILS_OPERATIONS_HPP_
#define YGG_DATABASE_SEMANTICS_DETAILS_OPERATIONS_HPP_

#include "yggdrasil/database/semantics/operations.hpp"
#include "yggdrasil/semantics/equal_to.hpp"
#include "yggdrasil/semantics/hash.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace ygg::database
{

namespace detail
{
/// Copy canonical fields verbatim; plans resolve their byte slices before evaluation.
void append_fields(std::vector<std::byte>& output, std::span<const std::byte> input, std::span<const ColumnSlice> fields);

void prepare_output(Builder<Relation>& out, std::span<const ColumnLayout> columns, std::initializer_list<const void*> inputs);

template<RelationViewConcept L, RelationViewConcept R>
void require_same_columns(const L& lhs, const R& rhs)
{
    if (!std::ranges::equal(lhs.columns().span(), rhs.columns().span()))
        throw std::invalid_argument("Relational operation: input schemas must have the same column order.");
}

void require_plan_columns(std::span<const ColumnLayout> actual, std::span<const ColumnLayout> expected);
}  // namespace detail

namespace detail
{
template<RelationViewConcept V>
void project_rows(const V& input, std::span<const ColumnLayout> columns, std::span<const ColumnSlice> positions, Builder<Relation>& out, Workspace& workspace)
{
    detail::prepare_output(out, columns, { input.get_storage_address() });

    if (positions.empty())
    {
        if (!input.empty())
            out.insert(Row({}, columns));
        return;
    }

    auto& row = workspace.row;
    row.reserve(out.columns().row_size());
    for (size_t i = 0; i < input.size(); ++i)
    {
        const auto source = input.row(i);
        row.clear();
        append_fields(row, source, positions);
        out.insert(Row(row, columns));
    }
}

}  // namespace detail

template<RelationViewConcept V>
void project(const V& input, const ProjectionPlan& plan, Builder<Relation>& out, Workspace& workspace)
{
    detail::require_plan_columns(input.columns().span(), plan.input_columns().span());
    detail::project_rows(input, plan.output_columns().span(), plan.positions(), out, workspace);
}

template<RelationViewConcept V>
void project(const V& input, std::span<const Index<Column>> columns, Builder<Relation>& out, Workspace& workspace)
{
    detail::projection_positions(input.columns().span(), columns, workspace.columns, workspace.lhs_keys);
    detail::project_rows(input, workspace.columns, workspace.lhs_keys, out, workspace);
}

template<RelationViewConcept V>
void project(const V& input, std::initializer_list<Index<Column>> columns, Builder<Relation>& out, Workspace& workspace)
{ project(input, std::span<const Index<Column>>(columns), out, workspace); }

template<RelationViewConcept V>
void project(const V& input, std::span<const Index<Column>> columns, Builder<Relation>& out)
{
    auto workspace = Workspace();
    project(input, columns, out, workspace);
}

template<RelationViewConcept V>
void project(const V& input, std::initializer_list<Index<Column>> columns, Builder<Relation>& out)
{ project(input, std::span<const Index<Column>>(columns), out); }

template<RelationViewConcept V>
Builder<Relation> project(const V& input, Builder<Columns> columns)
{
    auto out = Builder<Relation>(std::move(columns));
    auto workspace = Workspace();
    const auto input_columns = input.columns().span();
    for (const auto& column : out.columns())
    {
        const auto& source = input_columns[input.column_index(column.label)];
        if (source.type != column.type)
            throw std::invalid_argument("Relational operation: projection output has an incompatible column type.");
        workspace.lhs_keys.push_back(detail::column_slice(source));
    }
    detail::project_rows(input, out.columns().span(), workspace.lhs_keys, out, workspace);
    return out;
}

template<RelationViewConcept V>
Builder<Relation> project(const V& input, std::span<const Index<Column>> columns)
{
    auto workspace = Workspace();
    detail::projection_positions(input.columns().span(), columns, workspace.columns, workspace.lhs_keys);
    auto out = Builder<Relation>(workspace.columns);
    detail::project_rows(input, workspace.columns, workspace.lhs_keys, out, workspace);
    return out;
}

template<RelationViewConcept V>
Builder<Relation> project(const V& input, std::initializer_list<Index<Column>> columns)
{ return project(input, std::span<const Index<Column>>(columns)); }

template<RelationViewConcept V, typename Predicate>
    requires std::predicate<Predicate&, Row>
void select(const V& input, Predicate predicate, Builder<Relation>& out)
{
    detail::prepare_output(out, input.columns().span(), { input.get_storage_address() });
    for (size_t i = 0; i < input.size(); ++i)
        if (std::invoke(predicate, Row(input.row(i), input.columns().span())))
            out.insert(Row(input.row(i), input.columns().span()));
}

template<RelationViewConcept V>
Builder<Relation>& assign(Builder<Relation>& destination, const V& source)
{
    if (destination.get_storage_address() == source.get_storage_address())
    {
        destination.rename(source.columns().span());
        return destination;
    }
    destination.initialize(source.columns().span());
    for (size_t i = 0; i < source.size(); ++i)
        destination.insert(Row(source.row(i), source.columns().span()));
    return destination;
}

template<RelationViewConcept V, typename Predicate>
    requires std::predicate<Predicate&, Row>
Builder<Relation> select(const V& input, Predicate predicate)
{
    auto out = Builder<Relation>(input.columns().span());
    select(input, std::move(predicate), out);
    return out;
}

template<RelationViewConcept V>
void select_equal_columns(const V& input, Index<Column> lhs, Index<Column> rhs, Builder<Relation>& out)
{
    const auto& left = input.columns().span()[input.column_index(lhs)];
    const auto& right = input.columns().span()[input.column_index(rhs)];
    if (left.type != right.type)
        throw std::invalid_argument("Relational operation: selected columns must have the same type.");
    select(input, [=](Row row) { return std::ranges::equal(row.bytes().subspan(left.offset, left.size), row.bytes().subspan(right.offset, right.size)); }, out);
}

template<RelationViewConcept V>
Builder<Relation> select_equal_columns(const V& input, Index<Column> lhs, Index<Column> rhs)
{
    auto out = Builder<Relation>(input.columns().span());
    select_equal_columns(input, lhs, rhs, out);
    return out;
}

template<RelationViewConcept V, ByteCodable T>
void select_equal_value(const V& input, Index<Column> column, const T& value, Builder<Relation>& out)
{
    const auto& field = input.columns().span()[input.column_index(column)];
    if (field.type != type_id<T>)
        throw std::invalid_argument("Relational operation: selected value has an incompatible column type.");
    auto encoded = std::array<std::byte, ByteCodec<T>::size> {};
    ByteCodec<T>::encode(value, encoded);
    select(input, [=](Row row) { return std::ranges::equal(row.bytes().subspan(field.offset, field.size), encoded); }, out);
}

template<RelationViewConcept V, ByteCodable T>
Builder<Relation> select_equal_value(const V& input, Index<Column> column, const T& value)
{
    auto out = Builder<Relation>(input.columns().span());
    select_equal_value(input, column, value, out);
    return out;
}

namespace detail
{
/// Appends joined rows. Lookup values are row positions in the selected build
/// input for a key hash; actual key equality resolves hash collisions.
template<RelationViewConcept L, RelationViewConcept R, std::unsigned_integral Position>
void join_rows(const L& lhs,
               const R& rhs,
               std::span<const ColumnLayout> columns,
               std::span<const ColumnSlice> lhs_keys,
               std::span<const ColumnSlice> rhs_keys,
               std::span<const ColumnSlice> rhs_payload,
               bool build_left,
               const UnorderedMultiMap<hash_t, Position>& index,
               Builder<Relation>& out,
               Workspace& workspace)
{
    if (lhs.empty() || rhs.empty())
        return;

    auto& row = workspace.row;
    row.reserve(out.columns().row_size());
    const auto emit = [&](std::span<const std::byte> left, std::span<const std::byte> right)
    {
        row.assign(left.begin(), left.end());
        append_fields(row, right, rhs_payload);
        out.insert(Row(row, columns));
    };

    if (lhs_keys.empty())
    {
        for (size_t i = 0; i < lhs.size(); ++i)
            for (size_t j = 0; j < rhs.size(); ++j)
                emit(lhs.row(i), rhs.row(j));
        return;
    }

    const auto probe_rows = [&](const RelationViewConcept auto& build,
                                const RelationViewConcept auto& probe,
                                std::span<const ColumnSlice> build_keys,
                                std::span<const ColumnSlice> probe_keys)
    {
        for (size_t i = 0; i < probe.size(); ++i)
        {
            const auto probe_row = probe.row(i);
            for (const auto match : index.values(join_key_hash(probe_row, probe_keys)))
            {
                const auto build_row = build.row(match);
                if (!join_keys_equal(build_row, build_keys, probe_row, probe_keys))
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

template<RelationViewConcept L, RelationViewConcept R>
void join_rows(const L& lhs,
               const R& rhs,
               std::span<const ColumnLayout> columns,
               std::span<const ColumnSlice> lhs_keys,
               std::span<const ColumnSlice> rhs_keys,
               std::span<const ColumnSlice> rhs_payload,
               Builder<Relation>& out,
               Workspace& workspace)
{
    prepare_output(out, columns, { lhs.get_storage_address(), rhs.get_storage_address() });
    const bool build_left = lhs.size() <= rhs.size();
    if (!lhs_keys.empty() && !lhs.empty() && !rhs.empty())
    {
        if (build_left)
            build_join_index(lhs, lhs_keys, workspace.join_index);
        else
            build_join_index(rhs, rhs_keys, workspace.join_index);
    }
    join_rows(lhs, rhs, columns, lhs_keys, rhs_keys, rhs_payload, build_left, workspace.join_index, out, workspace);
}

}  // namespace detail

template<RelationViewConcept L, RelationViewConcept R>
void join(const L& lhs, const R& rhs, const JoinPlan& plan, const JoinIndex& index, Builder<Relation>& out, Workspace& workspace)
{
    detail::require_plan_columns(lhs.columns().span(), plan.lhs_columns().span());
    detail::require_plan_columns(rhs.columns().span(), plan.rhs_columns().span());
    const bool build_left = index.matches(lhs, plan.lhs_keys());
    if (!build_left && !index.matches(rhs, plan.rhs_keys()))
        throw std::invalid_argument("Relational operation: join index does not match either input's storage and keys.");
    detail::prepare_output(out, plan.output_columns().span(), { lhs.get_storage_address(), rhs.get_storage_address() });
    detail::join_rows(lhs, rhs, plan.output_columns().span(), plan.lhs_keys(), plan.rhs_keys(), plan.rhs_payload(), build_left, index.index(), out, workspace);
}

template<RelationViewConcept L, RelationViewConcept R>
void join(const L& lhs, const R& rhs, const JoinPlan& plan, Builder<Relation>& out, Workspace& workspace)
{
    detail::require_plan_columns(lhs.columns().span(), plan.lhs_columns().span());
    detail::require_plan_columns(rhs.columns().span(), plan.rhs_columns().span());
    detail::join_rows(lhs, rhs, plan.output_columns().span(), plan.lhs_keys(), plan.rhs_keys(), plan.rhs_payload(), out, workspace);
}

template<RelationViewConcept L, RelationViewConcept R>
void join(const L& lhs, const R& rhs, const JoinPlan& plan, JoinIndexCache& cache, JoinReuse reuse, Builder<Relation>& out, Workspace& workspace)
{
    if (!reuse.lhs && !reuse.rhs)
        return join(lhs, rhs, plan, out, workspace);
    detail::require_plan_columns(lhs.columns().span(), plan.lhs_columns().span());
    detail::require_plan_columns(rhs.columns().span(), plan.rhs_columns().span());
    // Build on a reused input, the smaller one if both are reused.
    const bool build_left = reuse.lhs && (!reuse.rhs || lhs.size() <= rhs.size());
    if (!lhs.empty() && !rhs.empty() && !plan.lhs_keys().empty()
        && (build_left ? lhs.get_storage_index() : rhs.get_storage_index()) == std::numeric_limits<size_t>::max())
        throw std::invalid_argument("Relational operation: cached joins require factory-created inputs.");
    detail::prepare_output(out, plan.output_columns().span(), { lhs.get_storage_address(), rhs.get_storage_address() });
    if (lhs.empty() || rhs.empty())
        return;
    const auto* index = &workspace.join_index;
    if (!plan.lhs_keys().empty())
        index = &(build_left ? cache.get_or_create(lhs, plan.lhs_keys()) : cache.get_or_create(rhs, plan.rhs_keys())).index();
    detail::join_rows(lhs, rhs, plan.output_columns().span(), plan.lhs_keys(), plan.rhs_keys(), plan.rhs_payload(), build_left, *index, out, workspace);
}

template<RelationViewConcept L, RelationViewConcept R>
void join(const L& lhs, const R& rhs, Builder<Relation>& out, Workspace& workspace)
{
    detail::join_positions(lhs.columns().span(), rhs.columns().span(), workspace.columns, workspace.lhs_keys, workspace.rhs_keys, workspace.rhs_payload);
    detail::join_rows(lhs, rhs, workspace.columns, workspace.lhs_keys, workspace.rhs_keys, workspace.rhs_payload, out, workspace);
}

template<RelationViewConcept L, RelationViewConcept R>
void join(const L& lhs, const R& rhs, Builder<Relation>& out)
{
    auto workspace = Workspace();
    join(lhs, rhs, out, workspace);
}

template<RelationViewConcept L, RelationViewConcept R>
Builder<Relation> join(const L& lhs, const R& rhs)
{
    auto workspace = Workspace();
    detail::join_positions(lhs.columns().span(), rhs.columns().span(), workspace.columns, workspace.lhs_keys, workspace.rhs_keys, workspace.rhs_payload);
    auto out = Builder<Relation>(workspace.columns);
    detail::join_rows(lhs, rhs, workspace.columns, workspace.lhs_keys, workspace.rhs_keys, workspace.rhs_payload, out, workspace);
    return out;
}

template<RelationViewConcept L, RelationViewConcept R>
void union_(const L& lhs, const R& rhs, Builder<Relation>& out)
{
    detail::require_same_columns(lhs, rhs);
    detail::prepare_output(out, lhs.columns().span(), { lhs.get_storage_address(), rhs.get_storage_address() });
    for (size_t i = 0; i < lhs.size(); ++i)
        out.insert(Row(lhs.row(i), lhs.columns().span()));
    for (size_t i = 0; i < rhs.size(); ++i)
        out.insert(Row(rhs.row(i), rhs.columns().span()));
}

template<RelationViewConcept L, RelationViewConcept R>
Builder<Relation> union_(const L& lhs, const R& rhs)
{
    auto out = Builder<Relation>(lhs.columns().span());
    union_(lhs, rhs, out);
    return out;
}

template<RelationViewConcept L, RelationViewConcept R>
void difference(const L& lhs, const R& rhs, Builder<Relation>& out)
{
    detail::require_same_columns(lhs, rhs);
    detail::prepare_output(out, lhs.columns().span(), { lhs.get_storage_address(), rhs.get_storage_address() });
    for (size_t i = 0; i < lhs.size(); ++i)
        if (!rhs.contains(Row(lhs.row(i), lhs.columns().span())))
            out.insert(Row(lhs.row(i), lhs.columns().span()));
}

template<RelationViewConcept L, RelationViewConcept R>
Builder<Relation> difference(const L& lhs, const R& rhs)
{
    auto out = Builder<Relation>(lhs.columns().span());
    difference(lhs, rhs, out);
    return out;
}

namespace detail
{
/// Keeps the lhs rows whose match in rhs on the plan's keys exists (`matching`) or does not.
template<RelationViewConcept L, RelationViewConcept R>
void filter_matches(const L& lhs, const R& rhs, const JoinPlan& plan, bool matching, Builder<Relation>& out, Workspace& workspace)
{
    require_plan_columns(lhs.columns().span(), plan.lhs_columns().span());
    require_plan_columns(rhs.columns().span(), plan.rhs_columns().span());
    prepare_output(out, lhs.columns().span(), { lhs.get_storage_address(), rhs.get_storage_address() });
    const auto lhs_keys = plan.lhs_keys();
    const auto rhs_keys = plan.rhs_keys();
    if (!lhs_keys.empty() && !rhs.empty())
        build_join_index(rhs, rhs_keys, workspace.join_index);
    const auto matched = [&](std::span<const std::byte> row)
    {
        if (rhs.empty() || lhs_keys.empty())
            return !rhs.empty();
        for (const auto match : workspace.join_index.values(join_key_hash(row, lhs_keys)))
            if (join_keys_equal(rhs.row(match), rhs_keys, row, lhs_keys))
                return true;
        return false;
    };
    for (size_t i = 0; i < lhs.size(); ++i)
        if (matched(lhs.row(i)) == matching)
            out.insert(Row(lhs.row(i), lhs.columns().span()));
}
}  // namespace detail

template<RelationViewConcept L, RelationViewConcept R>
void semijoin(const L& lhs, const R& rhs, const JoinPlan& plan, Builder<Relation>& out, Workspace& workspace)
{ detail::filter_matches(lhs, rhs, plan, true, out, workspace); }

template<RelationViewConcept L, RelationViewConcept R>
Builder<Relation> semijoin(const L& lhs, const R& rhs)
{
    auto workspace = Workspace();
    auto out = Builder<Relation>(lhs.columns().span());
    semijoin(lhs, rhs, JoinPlan(lhs.columns().span(), rhs.columns().span()), out, workspace);
    return out;
}

template<RelationViewConcept L, RelationViewConcept R>
void antijoin(const L& lhs, const R& rhs, const JoinPlan& plan, Builder<Relation>& out, Workspace& workspace)
{ detail::filter_matches(lhs, rhs, plan, false, out, workspace); }

template<RelationViewConcept L, RelationViewConcept R>
Builder<Relation> antijoin(const L& lhs, const R& rhs)
{
    auto workspace = Workspace();
    auto out = Builder<Relation>(lhs.columns().span());
    antijoin(lhs, rhs, JoinPlan(lhs.columns().span(), rhs.columns().span()), out, workspace);
    return out;
}

}  // namespace ygg::database

#endif
