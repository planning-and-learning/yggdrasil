/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_SEMANTICS_INCREMENTAL_QUERY_HPP_
#define YGG_DATABASE_SEMANTICS_INCREMENTAL_QUERY_HPP_

#include "yggdrasil/core/type_list.hpp"
#include "yggdrasil/database/semantics/incremental/distance.hpp"
#include "yggdrasil/database/semantics/incremental/generic_join.hpp"
#include "yggdrasil/database/semantics/incremental/join.hpp"
#include "yggdrasil/database/semantics/incremental/projection.hpp"
#include "yggdrasil/database/semantics/incremental/semijoin.hpp"
#include "yggdrasil/database/semantics/query_evaluation.hpp"

#include <ranges>
#include <type_traits>
#include <utility>
#include <variant>

namespace ygg::database::incremental
{
class QueryEvaluator;

namespace detail
{
struct QueryValue
{
    Builder<Relation> result;
    Delta delta;
    explicit QueryValue(std::span<const ColumnLayout> columns) : result(columns), delta(columns) {}
    const auto& get_result() const noexcept { return result; }
    const auto& get_delta() const noexcept { return delta; }
    size_t memory_usage() const noexcept { return result.memory_usage() + delta.memory_usage(); }

    void set(std::span<const std::byte> bytes, bool present);
};

struct QueryDistance : DistanceEvaluator
{
    using DistanceEvaluator::DistanceEvaluator;
};

/// How a query node of one operator is maintained: its state, built from the operation,
/// initialized from the operands' results, and updated from their exact deltas. The
/// evaluator provides result(query) and delta(query). Stateless operators keep only a value.
/// Child deltas are exact by construction, so updates skip validation.
template<typename Tag>
struct NodeRules
{
    using State = QueryValue;

    static State make(QueryView<Tag> operation) { return State(operation.columns()); }

    template<RelationViewRange R, std::same_as<QueryEvaluator> Graph>
    static void initialize(State& state, QueryView<Tag> operation, const Graph& graph, const R& bindings, Workspace&)
    {
        state.delta.clear();
        database::detail::evaluate_stateless(operation, bindings, [&](QueryView<> query) -> const auto& { return graph.result(query); }, state.result);
    }

    template<RelationDeltaRange R, std::same_as<QueryEvaluator> Graph>
    static void update(State& state, QueryView<Tag> operation, const Graph& graph, const R& changes, Workspace&)
    {
        state.delta.clear();
        if constexpr (std::same_as<Tag, QueryEmptyTag>)
            return;
        else if constexpr (std::same_as<Tag, QueryUnionTag> || std::same_as<Tag, QueryDifferenceTag>)
        {
            const auto& lhs = graph.result(operation.get_lhs());
            const auto& rhs = graph.result(operation.get_rhs());
            const auto reconcile = [&](const auto& rows)
            {
                for (size_t i = 0; i < rows.size(); ++i)
                {
                    const Row row(rows.row(i), operation.columns());
                    const bool present = std::same_as<Tag, QueryUnionTag> ? lhs.contains(row) || rhs.contains(row) : lhs.contains(row) && !rhs.contains(row);
                    state.set(row.bytes(), present);
                }
            };
            for_each_child(operation,
                           [&](QueryView<> child)
                           {
                               reconcile(graph.delta(child).added);
                               reconcile(graph.delta(child).removed);
                           });
        }
        else
        {
            const auto apply = [&](const auto& rows, bool present)
            {
                for (size_t i = 0; i < rows.size(); ++i)
                {
                    const auto row = rows.row(i);
                    if constexpr (std::same_as<Tag, QuerySelectEqualTag> || std::same_as<Tag, QuerySelectValueTag>)
                        if (!database::detail::query_accepts(operation, row))
                            continue;
                    state.set(row, present);
                }
            };
            if constexpr (std::same_as<Tag, QueryInputTag>)
            {
                const auto& [added, removed] = changes[operation.get_input_slot()];
                apply(removed, false);
                apply(added, true);
            }
            else
            {
                apply(graph.delta(operation.get_arg()).removed, false);
                apply(graph.delta(operation.get_arg()).added, true);
            }
        }
    }
};

template<>
struct NodeRules<QueryProjectTag>
{
    using State = ProjectionEvaluator;

    static State make(QueryView<QueryProjectTag> operation) { return State(operation.get_plan()); }
    template<RelationViewRange R, std::same_as<QueryEvaluator> Graph>
    static void initialize(State& state, QueryView<QueryProjectTag> operation, const Graph& graph, const R&, Workspace& workspace)
    {
        state.initialize(graph.result(operation.get_arg()), workspace);
    }
    template<RelationDeltaRange R, std::same_as<QueryEvaluator> Graph>
    static void update(State& state, QueryView<QueryProjectTag> operation, const Graph& graph, const R&, Workspace& workspace)
    {
        state.apply(graph.delta(operation.get_arg()).change(), workspace);
    }
};

template<>
struct NodeRules<QueryJoinTag>
{
    using State = JoinEvaluator;

    static State make(QueryView<QueryJoinTag> operation) { return State(operation.get_plan()); }
    template<RelationViewRange R, std::same_as<QueryEvaluator> Graph>
    static void initialize(State& state, QueryView<QueryJoinTag> operation, const Graph& graph, const R&, Workspace& workspace)
    {
        state.initialize(graph.result(operation.get_lhs()), graph.result(operation.get_rhs()), workspace);
    }
    template<RelationDeltaRange R, std::same_as<QueryEvaluator> Graph>
    static void update(State& state, QueryView<QueryJoinTag> operation, const Graph& graph, const R&, Workspace& workspace)
    {
        state.apply(graph.delta(operation.get_lhs()).change(), graph.delta(operation.get_rhs()).change(), workspace);
    }
};

template<typename Tag>
    requires(std::same_as<Tag, QuerySemijoinTag> || std::same_as<Tag, QueryAntijoinTag>)
struct NodeRules<Tag>
{
    using State = SemijoinEvaluator;

    static State make(QueryView<Tag> operation) { return State(operation.get_plan(), std::same_as<Tag, QueryAntijoinTag>); }
    template<RelationViewRange R, std::same_as<QueryEvaluator> Graph>
    static void initialize(State& state, QueryView<Tag> operation, const Graph& graph, const R&, Workspace&)
    {
        state.initialize(graph.result(operation.get_lhs()), graph.result(operation.get_rhs()));
    }
    template<RelationDeltaRange R, std::same_as<QueryEvaluator> Graph>
    static void update(State& state, QueryView<Tag> operation, const Graph& graph, const R&, Workspace&)
    {
        state.apply(graph.delta(operation.get_lhs()).change(), graph.delta(operation.get_rhs()).change());
    }
};

template<>
struct NodeRules<QueryGenericJoinTag>
{
    using State = GenericJoinEvaluator;

    static State make(QueryView<QueryGenericJoinTag> operation) { return State(operation.get_plan()); }
    template<RelationViewRange R, std::same_as<QueryEvaluator> Graph>
    static void initialize(State& state, QueryView<QueryGenericJoinTag> operation, const Graph& graph, const R&, Workspace&)
    {
        state.initialize(operation.get_inputs() | std::views::transform([&](Index<Query<>> input) -> const auto& { return graph.result(input); }));
    }
    template<RelationDeltaRange R, std::same_as<QueryEvaluator> Graph>
    static void update(State& state, QueryView<QueryGenericJoinTag> operation, const Graph& graph, const R&, Workspace&)
    {
        state.apply(operation.get_inputs() | std::views::transform([&](Index<Query<>> input) { return graph.delta(input).change(); }));
    }
};

template<>
struct NodeRules<QueryDistanceTag>
{
    using State = QueryDistance;

    static State make(QueryView<QueryDistanceTag> operation) { return State(operation.get_plan()); }
    template<RelationViewRange R, std::same_as<QueryEvaluator> Graph>
    static void initialize(State& state, QueryView<QueryDistanceTag> operation, const Graph& graph, const R&, Workspace&)
    {
        state.initialize(graph.result(operation.get_sources()), graph.result(operation.get_edges()), graph.result(operation.get_targets()));
    }
    template<RelationDeltaRange R, std::same_as<QueryEvaluator> Graph>
    static void update(State& state, QueryView<QueryDistanceTag> operation, const Graph& graph, const R&, Workspace&)
    {
        state.apply(graph.delta(operation.get_sources()).change(), graph.delta(operation.get_edges()).change(), graph.delta(operation.get_targets()).change());
    }
};

/// The node of one operator; the tag keeps the states of different operators distinct.
template<typename Tag>
struct QueryNode
{
    typename NodeRules<Tag>::State state;
};
}  // namespace detail

/// Maintains a prepared DAG from exact input deltas. Shared nodes update once.
/// initialize() borrows inputs only during the call; update() receives actual,
/// disjoint added/removed sets. Reversing every pair undoes a complete batch.
/// Input validation leaves results intact; execution failures require initialize().
class QueryEvaluator
{
    using NodeId = Index<Query<>>;
    template<typename Tag>
    using Rules = detail::NodeRules<Tag>;
    using Node = ApplyTypeListT<std::variant, MapTypeListT<detail::QueryNode, QueryConstructorTags>>;
    QueryPlan m_plan;
    std::vector<Node> m_nodes;
    Workspace m_workspace;
    std::vector<const void*> m_storage_addresses;
    bool m_initialized = false;

    template<typename>
    friend struct detail::NodeRules;

    const Builder<Relation>& result(NodeId id) const
    {
        return std::visit([](const auto& node) -> const Builder<Relation>& { return node.state.get_result(); }, m_nodes[id.get_value()]);
    }
    const Builder<Relation>& result(QueryView<> query) const { return result(query.get_index()); }
    const Delta& delta(NodeId id) const
    {
        return std::visit([](const auto& node) -> const Delta& { return node.state.get_delta(); }, m_nodes[id.get_value()]);
    }
    const Delta& delta(QueryView<> query) const { return delta(query.get_index()); }
    bool is_internal(const void* address) const { return std::ranges::find(m_storage_addresses, address) != m_storage_addresses.end(); }

public:
    explicit QueryEvaluator(QueryPlan plan) : m_plan(std::move(plan))
    {
        m_nodes.reserve(m_plan.node_count());
        for (size_t i = 0; i < m_plan.node_count(); ++i)
            ygg::visit([&]<typename Tag>(QueryView<Tag> operation)
                       { m_nodes.emplace_back(std::in_place_type<detail::QueryNode<Tag>>, Rules<Tag>::make(operation)); },
                       m_plan[NodeId(to_uint_t(i))].get_variant());
    }
    template<RelationViewRange R>
    void initialize(const R& bindings)
    {
        database::detail::validate_bindings(m_plan, bindings, [&](const void* address) { return is_internal(address); });
        m_initialized = false;
        for (size_t i = 0; i < m_nodes.size(); ++i)
            ygg::visit(
                [&]<typename Tag>(QueryView<Tag> operation)
                { Rules<Tag>::initialize(std::get<detail::QueryNode<Tag>>(m_nodes[i]).state, operation, *this, bindings, m_workspace); },
                m_plan[NodeId(to_uint_t(i))].get_variant());
        // Fixed-schema builders retain their storage identity, including after a failed update.
        m_storage_addresses.clear();
        for (size_t i = 0; i < m_nodes.size(); ++i)
        {
            const NodeId id(to_uint_t(i));
            m_storage_addresses.push_back(result(id).get_storage_address());
            m_storage_addresses.push_back(delta(id).added.get_storage_address());
            m_storage_addresses.push_back(delta(id).removed.get_storage_address());
        }
        m_initialized = true;
    }
    template<RelationDeltaRange R>
    void update(const R& changes)
    {
        detail::require_initialized(m_initialized);
        // Validate the whole external batch before changing any input baseline.
        for (size_t i = 0; i < m_plan.node_count(); ++i)
        {
            const auto query = m_plan[NodeId(to_uint_t(i))];
            if (!is<QueryInputTag>(query))
                continue;
            const auto slot = as<QueryInputTag>(query).get_input_slot();
            if (slot >= std::ranges::size(changes))
                throw std::invalid_argument("Incremental query: missing input delta.");
            const auto& [added, removed] = changes[slot];
            detail::require_delta(added, removed, query.columns());
            if (is_internal(added.get_storage_address()) || is_internal(removed.get_storage_address()))
                throw std::invalid_argument("Incremental query: input aliases evaluator storage.");
            const auto& baseline = result(query);
            for (size_t row = 0; row < added.size(); ++row)
                if (baseline.contains(added.row(row)))
                    throw std::invalid_argument("Incremental query: added row is already present.");
            for (size_t row = 0; row < removed.size(); ++row)
                if (!baseline.contains(removed.row(row)))
                    throw std::invalid_argument("Incremental query: removed row is absent.");
        }
        m_initialized = false;
        for (size_t i = 0; i < m_nodes.size(); ++i)
            ygg::visit(
                [&]<typename Tag>(QueryView<Tag> operation)
                { Rules<Tag>::update(std::get<detail::QueryNode<Tag>>(m_nodes[i]).state, operation, *this, changes, m_workspace); },
                m_plan[NodeId(to_uint_t(i))].get_variant());
        m_initialized = true;
    }
    const Builder<Relation>& get_result(size_t root = 0) const&
    {
        detail::require_initialized(m_initialized);
        if (root >= m_plan.root_count())
            throw std::out_of_range("Incremental query: invalid root.");
        return result(m_plan.roots()[root]);
    }
    const Builder<Relation>& get_result(size_t = 0) const&& = delete;
    const Delta& get_delta(size_t root = 0) const&
    {
        detail::require_initialized(m_initialized);
        if (root >= m_plan.root_count())
            throw std::out_of_range("Incremental query: invalid root.");
        return delta(m_plan.roots()[root]);
    }
    const Delta& get_delta(size_t = 0) const&& = delete;
    /// Retained runtime storage; excludes the immutable query plan.
    size_t memory_usage() const noexcept
    {
        size_t result = m_workspace.memory_usage() + m_storage_addresses.capacity() * sizeof(const void*);
        // get_if rather than std::visit, which throws for a valueless variant; nodes are never valueless.
        const auto node_usage = []<typename... Nodes>(const std::variant<Nodes...>& node) noexcept
        { return ((std::get_if<Nodes>(&node) ? std::get_if<Nodes>(&node)->state.memory_usage() : size_t { 0 }) + ...); };
        for (const auto& node : m_nodes)
            result += node_usage(node);
        return result;
    }
};
}  // namespace ygg::database::incremental
#endif
