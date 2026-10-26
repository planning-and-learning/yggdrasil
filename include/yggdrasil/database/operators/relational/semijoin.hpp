/*
 * Copyright (C) 2026 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef YGG_DATABASE_OPERATORS_RELATIONAL_SEMIJOIN_HPP_
#define YGG_DATABASE_OPERATORS_RELATIONAL_SEMIJOIN_HPP_

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
/// Semijoin lhs ⋉ rhs: the lhs rows with a match in rhs on the plan's keys. Without shared columns, lhs if rhs is
/// nonempty. Hashes rhs.
template<RelationViewConcept L, RelationViewConcept R>
void semijoin(const L& lhs, const R& rhs, const JoinPlan& plan, Builder<Relation>& out, Workspace& workspace);

template<RelationViewConcept L, RelationViewConcept R>
Builder<Relation> semijoin(const L& lhs, const R& rhs);

/// Antijoin lhs ▷ rhs: the lhs rows without a match in rhs on the plan's keys. Without shared columns, lhs if rhs is
/// empty. Hashes rhs.
template<RelationViewConcept L, RelationViewConcept R>
void antijoin(const L& lhs, const R& rhs, const JoinPlan& plan, Builder<Relation>& out, Workspace& workspace);

template<RelationViewConcept L, RelationViewConcept R>
Builder<Relation> antijoin(const L& lhs, const R& rhs);

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
