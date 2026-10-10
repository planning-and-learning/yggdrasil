/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_SYNTAX_QUERY_HPP_
#define YGG_DATABASE_SYNTAX_QUERY_HPP_

#include "yggdrasil/database/syntax/query_construction.hpp"

#include <concepts>
#include <initializer_list>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <vector>

namespace ygg::database
{
namespace detail
{
inline std::vector<Index<Column>> query_labels(std::span<const ColumnLayout> columns)
{
    std::vector<Index<Column>> result;
    result.reserve(columns.size());
    for (const auto& column : columns)
        result.push_back(column.label);
    return result;
}

/// Rebinds a query's operation to schema-identical children in the destination.
template<ColumnTypes Values>
QueryView<Values>
clone_query(QueryView<Values> source, std::span<const QueryView<Values>> children, QueryRepository<Values>& destination, QueryBuilder<Values>& builder)
{
    std::vector<QueryView<Values>> original;
    for_each_child(source, [&](QueryView<Values> child) { original.push_back(child); });
    if (original.size() != children.size())
        throw std::invalid_argument("Query clone: wrong child count.");
    for (size_t i = 0; i < children.size(); ++i)
    {
        if (&children[i].get_repository() != &destination)
            throw std::invalid_argument("Query clone: children must belong to the destination.");
        require_plan_columns(children[i].columns(), original[i].columns());
    }
    return ygg::visit(
        [&]<typename Tag>(QueryView<Values, Tag> concrete)
        {
            auto data = checkout<Query<Values, Tag>>(builder);
            *data = concrete.get_data();
            size_t child = 0;
            detail::query_children<Tag>(*data, [&](Index<Query<Values>>& index) { index = children[child++].get_index(); });
            return insert_query(destination, builder, *data);
        },
        source.get_variant());
}
}  // namespace detail

/// An immutable owning snapshot of the same typed query records used to build queries.
/// Copies share its stable repository; repository indices are topological, while roots retain caller order.
template<ColumnTypes Values>
class QueryPlan
{
    using QueryIndex = Index<Query<Values>>;
    std::shared_ptr<const QueryRepository<Values>> m_repository;
    std::vector<QueryIndex> m_roots;

public:
    QueryPlan() : m_repository(QueryRepositoryFactory<Values> {}.create_shared()) {}
    QueryPlan(std::shared_ptr<const QueryRepository<Values>> repository, std::vector<QueryIndex> roots) :
        m_repository(std::move(repository)),
        m_roots(std::move(roots))
    {
    }

    size_t node_count() const noexcept { return m_repository->size(); }
    size_t root_count() const noexcept { return m_roots.size(); }
    std::span<const QueryIndex> roots() const& noexcept { return m_roots; }
    std::span<const QueryIndex> roots() const&& = delete;
    const QueryRepository<Values>& get_repository() const& noexcept { return *m_repository; }
    const QueryRepository<Values>& get_repository() const&& = delete;
    QueryView<Values> operator[](QueryIndex id) const&
    {
        if (id.get_value() >= node_count())
            throw std::out_of_range("Query plan: invalid query index.");
        return QueryView<Values>(id, *m_repository);
    }
    QueryView<Values> operator[](QueryIndex id) const&& = delete;
};

/// The queries reachable from roots of one repository, each once, children first.
template<ColumnTypes Values>
std::vector<QueryView<Values>> reachable(std::span<const QueryView<Values>> roots)
{
    if (roots.empty())
        return {};
    const auto& repository = roots.front().get_repository();
    std::vector<bool> seen(repository.size());
    std::vector<QueryView<Values>> pending;
    for (const auto root : roots)
    {
        if (&root.get_repository() != &repository || root.get_index().get_value() >= repository.size())
            throw std::invalid_argument("Query: roots must belong to one repository.");
        pending.push_back(root);
    }
    while (!pending.empty())
    {
        const auto query = pending.back();
        pending.pop_back();
        if (seen[query.get_index().get_value()])
            continue;
        seen[query.get_index().get_value()] = true;
        for_each_child(query, [&](QueryView<Values> child) { pending.push_back(child); });
    }
    std::vector<QueryView<Values>> result;
    for (size_t value = 0; value < seen.size(); ++value)
        if (seen[value])
            result.emplace_back(Index<Query<Values>>(to_uint_t(value)), repository);
    return result;
}

/// Copies the queries reachable from the roots into a fresh owning snapshot.
template<ColumnTypes Values>
QueryPlan<Values> compile(std::span<const QueryView<Values>> roots)
{
    using QueryIndex = Index<Query<Values>>;
    if (roots.empty())
        return QueryPlan<Values> {};
    const auto& source = roots.front().get_repository();
    auto repository = source.get_factory().create_shared();
    QueryBuilder<Values> builder;
    std::vector<std::optional<QueryView<Values>>> copies(source.size());
    std::vector<QueryView<Values>> children;
    for (const auto query : reachable(roots))
    {
        children.clear();
        for_each_child(query, [&](QueryView<Values> child) { children.push_back(*copies[child.get_index().get_value()]); });
        copies[query.get_index().get_value()] = detail::clone_query(query, std::span<const QueryView<Values>>(children), *repository, builder);
    }
    std::vector<QueryIndex> outputs;
    outputs.reserve(roots.size());
    for (const auto root : roots)
        outputs.push_back(copies[root.get_index().get_value()]->get_index());
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
