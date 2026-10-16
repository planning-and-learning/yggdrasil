/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_SYNTAX_COPY_HPP_
#define YGG_DATABASE_SYNTAX_COPY_HPP_

#include "yggdrasil/database/syntax/query.hpp"

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <initializer_list>
#include <optional>
#include <ranges>
#include <span>
#include <utility>
#include <vector>

namespace ygg::database
{

/// The input slots read by the queries reachable from `roots`, each once, in increasing order.
template<ColumnTypes Values>
std::vector<size_t> input_slots(std::span<const QueryView<Values>> roots)
{
    auto slots = std::vector<size_t>();
    for (const auto query : reachable(roots))
        if (is<QueryInputTag>(query))
            slots.push_back(as<QueryInputTag>(query).get_input_slot());
    std::ranges::sort(slots);
    const auto [first, last] = std::ranges::unique(slots);
    slots.erase(first, last);
    return slots;
}

/// Copies the queries reachable from `roots` into `destination`, children first, each once, and returns the copy of
/// each root. An input's slot s becomes slot_of(s).
template<ColumnTypes Values, typename SlotOf>
    requires std::convertible_to<std::invoke_result_t<SlotOf&, size_t>, size_t>
std::vector<QueryView<Values>> copy(std::span<const QueryView<Values>> roots, SlotOf&& slot_of, QueryRepository<Values>& destination, QueryBuilder<Values>& builder)
{
    if (roots.empty())
        return {};
    auto copies = std::vector<std::optional<QueryView<Values>>>(roots.front().get_repository().size());
    auto children = std::vector<QueryView<Values>>();
    for (const auto query : reachable(roots))
    {
        auto& copied = copies[query.get_index().get_value()];
        if (is<QueryInputTag>(query))
        {
            auto data = checkout<Query<Values, QueryInputTag>>(builder);
            data->input_slot = slot_of(as<QueryInputTag>(query).get_input_slot());
            const auto columns = query.columns();
            data->columns.set(columns.begin(), columns.end());
            copied = insert_query(destination, builder, *data);
            continue;
        }
        children.clear();
        for_each_child(query, [&](QueryView<Values> child) { children.push_back(*copies[child.get_index().get_value()]); });
        copied = detail::clone_query(query, std::span<const QueryView<Values>>(children), destination, builder);
    }
    auto result = std::vector<QueryView<Values>>();
    for (const auto root : roots)
        result.push_back(*copies[root.get_index().get_value()]);
    return result;
}

/// Copies the queries reachable from `root` into `destination`; see the overload for several roots.
template<ColumnTypes Values, typename SlotOf>
    requires std::convertible_to<std::invoke_result_t<SlotOf&, size_t>, size_t>
QueryView<Values> copy(QueryView<Values> root, SlotOf&& slot_of, QueryRepository<Values>& destination, QueryBuilder<Values>& builder)
{
    return database::copy(std::span<const QueryView<Values>>(&root, 1), std::forward<SlotOf>(slot_of), destination, builder).front();
}

/// Copies the queries reachable from the roots into a fresh owning snapshot.
template<ColumnTypes Values>
QueryPlan<Values> compile(std::span<const QueryView<Values>> roots)
{
    if (roots.empty())
        return QueryPlan<Values> {};
    auto repository = roots.front().get_repository().get_factory().create_shared();
    QueryBuilder<Values> builder;
    std::vector<Index<Query<Values>>> outputs;
    for (const auto copied : database::copy(roots, [](size_t slot) { return slot; }, *repository, builder))
        outputs.push_back(copied.get_index());
    return QueryPlan<Values>(std::move(repository), std::move(outputs));
}

template<std::ranges::contiguous_range R>
auto compile(const R& roots)
{
    return compile(std::span<const std::ranges::range_value_t<R>>(roots));
}

template<ColumnTypes Values>
QueryPlan<Values> compile(std::initializer_list<QueryView<Values>> roots)
{
    return compile(std::span<const QueryView<Values>>(roots));
}
}  // namespace ygg::database

#endif
