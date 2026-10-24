/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_SYNTAX_QUERY_HPP_
#define YGG_DATABASE_SYNTAX_QUERY_HPP_

#include "yggdrasil/core/config.hpp"
#include "yggdrasil/database/syntax/query_construction.hpp"

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <initializer_list>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <utility>
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

namespace ygg::database
{

/// The input slots read by the queries reachable from `roots`, each once, in increasing order.
std::vector<size_t> input_slots(std::span<const QueryView<>> roots);

/// Copies the queries reachable from `roots` into `destination`, children first, each once, and returns the copy of
/// each root. An input's slot s becomes slot_of(s).
template<typename SlotOf>
    requires std::convertible_to<std::invoke_result_t<SlotOf&, size_t>, size_t>
std::vector<QueryView<>> copy(std::span<const QueryView<>> roots, SlotOf slot_of, QueryRepository& destination, QueryBuilder& builder)
{
    if (roots.empty())
        return {};
    auto copies = std::vector<std::optional<QueryView<>>>(roots.front().get_repository().size());
    auto children = std::vector<QueryView<>>();
    for (const auto query : reachable(roots))
    {
        auto& copied = copies[query.get_index().get_value()];
        if (is<QueryInputTag>(query))
        {
            auto data = checkout<Query<QueryInputTag>>(builder);
            data->input_slot = slot_of(as<QueryInputTag>(query).get_input_slot());
            const auto columns = query.columns();
            data->columns.set(columns.begin(), columns.end());
            copied = insert_query(destination, builder, *data);
            continue;
        }
        children.clear();
        for_each_child(query, [&](QueryView<> child) { children.push_back(*copies[child.get_index().get_value()]); });
        copied = detail::clone_query(query, std::span<const QueryView<>>(children), destination, builder);
    }
    auto result = std::vector<QueryView<>>();
    for (const auto root : roots)
    {
        const auto& copy = copies[root.get_index().get_value()];
        YGG_ASSUME(copy.has_value());  // the loop above copied every query reachable from a root
        result.push_back(*copy);
    }
    return result;
}

/// Copies the queries reachable from `root` into `destination`; see the overload for several roots.
template<typename SlotOf>
    requires std::convertible_to<std::invoke_result_t<SlotOf&, size_t>, size_t>
QueryView<> copy(QueryView<> root, SlotOf&& slot_of, QueryRepository& destination, QueryBuilder& builder)
{ return database::copy(std::span<const QueryView<>>(&root, 1), std::forward<SlotOf>(slot_of), destination, builder).front(); }

/// Copies the queries reachable from the roots into a fresh owning snapshot.
QueryPlan compile(std::span<const QueryView<>> roots);

template<std::ranges::contiguous_range R>
auto compile(const R& roots)
{ return compile(std::span<const std::ranges::range_value_t<R>>(roots)); }

QueryPlan compile(std::initializer_list<QueryView<>> roots);
}  // namespace ygg::database

#endif
