/*
 * Copyright (C) 2026 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef YGG_DATABASE_OPERATORS_RELATIONAL_JOIN_HPP_
#define YGG_DATABASE_OPERATORS_RELATIONAL_JOIN_HPP_

#include "yggdrasil/database/operators/relational/common.hpp"
#include "yggdrasil/database/operators/relational/join_index.hpp"

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <limits>
#include <ranges>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace ygg::database
{
/// Marks inputs whose row storage remains immutable for the cache lifetime.
struct JoinReuse
{
    bool lhs = false;
    bool rhs = false;
};

/// Natural join: match common labels and return lhs columns followed by
/// rhs-only columns. Disjoint schemas produce the Cartesian product.
/// Hashes the smaller input, retaining only hashes and row indices in the
/// transient index. Collisions are resolved by comparing the actual values.
template<RelationViewConcept L, RelationViewConcept R>
void join(const L& lhs, const R& rhs, const JoinPlan& plan, Builder<Relation>& out, Workspace& workspace);

/// Reuses an index on either input instead of rebuilding the smaller side.
/// The index must match that input's storage and the plan's ordered field slices.
/// Result row order is unspecified, as for the other relational operations.
template<RelationViewConcept L, RelationViewConcept R>
void join(const L& lhs, const R& rhs, const JoinPlan& plan, const JoinIndex& index, Builder<Relation>& out, Workspace& workspace);

/// Reuses the one marked input, or the smaller input when both are marked.
/// With neither marked, uses an ordinary transient index. Empty inputs and
/// Cartesian products do not create cached indexes.
template<RelationViewConcept L, RelationViewConcept R>
void join(const L& lhs, const R& rhs, const JoinPlan& plan, JoinIndexCache& cache, JoinReuse reuse, Builder<Relation>& out, Workspace& workspace);

template<RelationViewConcept L, RelationViewConcept R>
void join(const L& lhs, const R& rhs, Builder<Relation>& out, Workspace& workspace);

template<RelationViewConcept L, RelationViewConcept R>
void join(const L& lhs, const R& rhs, Builder<Relation>& out);

template<RelationViewConcept L, RelationViewConcept R>
Builder<Relation> join(const L& lhs, const R& rhs);

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
}  // namespace ygg::database

#endif
