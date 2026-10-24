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
/// Whether the query's operation has the tag.
template<class Tag>
bool is(QueryView<> query)
{ return query.get_variant().template is<Index<Query<Tag>>>(); }
/// The query's operation; requires is<Tag>(query).
template<class Tag>
QueryView<Tag> as(QueryView<> query)
{ return query.get_variant().template get<Index<Query<Tag>>>(); }

namespace detail
{
/// Rebinds a query's operation to schema-identical children in the destination.
QueryView<> clone_query(QueryView<> source, std::span<const QueryView<>> children, QueryRepository& destination, QueryBuilder& builder);
}  // namespace detail

/// An immutable owning snapshot of the same typed query records used to build queries.
/// Copies share its stable repository; repository indices are topological, while roots retain caller order.
class QueryPlan
{
    using QueryIndex = Index<Query<>>;
    std::shared_ptr<const QueryRepository> m_repository;
    std::vector<QueryIndex> m_roots;

public:
    QueryPlan() : m_repository(QueryRepositoryFactory {}.create_shared()) {}
    QueryPlan(std::shared_ptr<const QueryRepository> repository, std::vector<QueryIndex> roots) : m_repository(std::move(repository)), m_roots(std::move(roots))
    {
    }

    [[nodiscard]] size_t node_count() const noexcept { return m_repository->size(); }
    [[nodiscard]] size_t root_count() const noexcept { return m_roots.size(); }
    [[nodiscard]] std::span<const QueryIndex> roots() const& noexcept { return m_roots; }
    [[nodiscard]] std::span<const QueryIndex> roots() const&& = delete;
    [[nodiscard]] const QueryRepository& get_repository() const& noexcept { return *m_repository; }
    [[nodiscard]] const QueryRepository& get_repository() const&& = delete;
    QueryView<> operator[](QueryIndex id) const&
    {
        if (id.get_value() >= node_count())
            throw std::out_of_range("Query plan: invalid query index.");
        return QueryView<>(id, *m_repository);
    }
    QueryView<> operator[](QueryIndex id) const&& = delete;
};

/// The queries reachable from roots of one repository, each once, children first.
std::vector<QueryView<>> reachable(std::span<const QueryView<>> roots);

}  // namespace ygg::database

// copy() and compile(), which need everything above.
#include "yggdrasil/database/syntax/copy.hpp"

#endif
