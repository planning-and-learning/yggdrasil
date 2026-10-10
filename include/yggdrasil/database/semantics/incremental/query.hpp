/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_SEMANTICS_INCREMENTAL_QUERY_HPP_
#define YGG_DATABASE_SEMANTICS_INCREMENTAL_QUERY_HPP_

#include "yggdrasil/database/semantics/incremental/distance.hpp"
#include "yggdrasil/database/semantics/incremental/generic_join.hpp"
#include "yggdrasil/database/semantics/incremental/join.hpp"
#include "yggdrasil/database/semantics/incremental/projection.hpp"
#include "yggdrasil/database/semantics/query_evaluation.hpp"

#include <ranges>
#include <type_traits>
#include <utility>
#include <variant>

namespace ygg::database::incremental
{
namespace detail
{
template<ColumnTypes Values>
struct QueryValue
{
    Builder<Relation<Values>> result;
    Delta<Values> delta;
    explicit QueryValue(std::span<const ColumnLayout> columns) : result(columns), delta(columns) {}
    const auto& get_result() const noexcept { return result; }
    const auto& get_delta() const noexcept { return delta; }
    size_t memory_usage() const noexcept { return result.memory_usage() + delta.memory_usage(); }

    void set(std::span<const std::byte> bytes, bool present)
    {
        const Row<Values> row(bytes, result.columns().span());
        const auto position = result.find(row);
        if (present && !position)
        {
            result.insert(row);
            delta.added.insert(row);
        }
        else if (!present && position)
        {
            delta.removed.insert(row);
            result.erase(*position);
        }
    }
};

template<ColumnTypes Values, bool Enabled = ColumnValueFor<uint_t, Values>>
struct QueryDistance : QueryValue<Values>
{
    explicit QueryDistance(const DistancePlan<Values>& plan) : QueryValue<Values>(plan.output_columns().span()) { database::detail::unsupported_distance(); }
};
template<ColumnTypes Values>
struct QueryDistance<Values, true> : DistanceEvaluator<Values>
{
    using DistanceEvaluator<Values>::DistanceEvaluator;
};
}  // namespace detail

/// Maintains a prepared DAG from exact input deltas. Shared nodes update once.
/// initialize() borrows inputs only during the call; update() receives actual,
/// disjoint added/removed sets. Reversing every pair undoes a complete batch.
/// Input validation leaves results intact; execution failures require initialize().
template<ColumnTypes Values = DefaultColumnTypes>
class QueryEvaluator
{
    using NodeId = Index<Query<Values>>;
    using Value = detail::QueryValue<Values>;
    using Distance = detail::QueryDistance<Values>;
    using State = std::variant<Value, ProjectionEvaluator<Values>, JoinEvaluator<Values>, Distance, GenericJoinEvaluator<Values>>;
    /// The state maintaining an operator; stateless operators keep only a value.
    template<class Tag>
    using NodeState = std::conditional_t<std::same_as<Tag, QueryProjectTag>,
                                         ProjectionEvaluator<Values>,
                                         std::conditional_t<std::same_as<Tag, QueryJoinTag>,
                                                            JoinEvaluator<Values>,
                                                            std::conditional_t<std::same_as<Tag, QueryDistanceTag>,
                                                                               Distance,
                                                                               std::conditional_t<std::same_as<Tag, QueryGenericJoinTag>, GenericJoinEvaluator<Values>, Value>>>>;
    QueryPlan<Values> m_plan;
    std::vector<State> m_nodes;
    Workspace<Values> m_workspace;
    std::vector<const void*> m_storage_addresses;
    bool m_initialized = false;

    const Builder<Relation<Values>>& result(NodeId id) const
    {
        return std::visit([](const auto& state) -> const Builder<Relation<Values>>& { return state.get_result(); }, m_nodes[id.get_value()]);
    }
    const Builder<Relation<Values>>& result(QueryView<Values> query) const { return result(query.get_index()); }
    const Delta<Values>& delta(NodeId id) const
    {
        return std::visit([](const auto& state) -> const Delta<Values>& { return state.get_delta(); }, m_nodes[id.get_value()]);
    }
    const Delta<Values>& delta(QueryView<Values> query) const { return delta(query.get_index()); }
    bool is_internal(const void* address) const { return std::ranges::find(m_storage_addresses, address) != m_storage_addresses.end(); }
    template<class Tag, RelationDeltaRange<Values> R>
    void update_value(Value& value, QueryView<Values, Tag> operation, const R& changes)
    {
        value.delta.clear();
        if constexpr (std::same_as<Tag, QueryEmptyTag>)
            return;
        else if constexpr (std::same_as<Tag, QueryUnionTag> || std::same_as<Tag, QueryDifferenceTag>)
        {
            const auto& lhs = result(operation.get_lhs());
            const auto& rhs = result(operation.get_rhs());
            const auto reconcile = [&](const auto& rows)
            {
                for (size_t i = 0; i < rows.size(); ++i)
                {
                    const Row<Values> row(rows.row(i), operation.columns());
                    const bool present = std::same_as<Tag, QueryUnionTag> ? lhs.contains(row) || rhs.contains(row) : lhs.contains(row) && !rhs.contains(row);
                    value.set(row.bytes(), present);
                }
            };
            for_each_child(operation,
                           [&](QueryView<Values> child)
                           {
                               reconcile(delta(child).added);
                               reconcile(delta(child).removed);
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
                    value.set(row, present);
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
                apply(delta(operation.get_arg()).removed, false);
                apply(delta(operation.get_arg()).added, true);
            }
        }
    }

public:
    explicit QueryEvaluator(QueryPlan<Values> plan) : m_plan(std::move(plan))
    {
        m_nodes.reserve(m_plan.node_count());
        for (size_t i = 0; i < m_plan.node_count(); ++i)
            ygg::visit(
                [&]<typename Tag>(QueryView<Values, Tag> operation)
                {
                    if constexpr (std::same_as<NodeState<Tag>, Value>)
                        m_nodes.emplace_back(std::in_place_type<Value>, operation.columns());
                    else
                        m_nodes.emplace_back(std::in_place_type<NodeState<Tag>>, operation.get_plan());
                },
                m_plan[NodeId(to_uint_t(i))].get_variant());
    }
    template<RelationViewRange<Values> R>
    void initialize(const R& bindings)
    {
        database::detail::validate_bindings(m_plan, bindings, [&](const void* address) { return is_internal(address); });
        m_initialized = false;
        for (size_t i = 0; i < m_nodes.size(); ++i)
            ygg::visit(
                [&]<typename Tag>(QueryView<Values, Tag> operation)
                {
                    auto& state = std::get<NodeState<Tag>>(m_nodes[i]);
                    if constexpr (std::same_as<Tag, QueryProjectTag>)
                        state.initialize(result(operation.get_arg()), m_workspace);
                    else if constexpr (std::same_as<Tag, QueryJoinTag>)
                        state.initialize(result(operation.get_lhs()), result(operation.get_rhs()), m_workspace);
                    else if constexpr (std::same_as<Tag, QueryDistanceTag>)
                    {
                        if constexpr (ColumnValueFor<uint_t, Values>)
                            state.initialize(result(operation.get_sources()), result(operation.get_edges()), result(operation.get_targets()));
                    }
                    else if constexpr (std::same_as<Tag, QueryGenericJoinTag>)
                        state.initialize(operation.get_inputs() | std::views::transform([&](NodeId id) -> const auto& { return result(id); }));
                    else
                    {
                        state.delta.clear();
                        database::detail::evaluate_stateless(operation, bindings, [&](QueryView<Values> query) -> const auto& { return result(query); }, state.result);
                    }
                },
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
    template<RelationDeltaRange<Values> R>
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
            detail::require_delta<Values>(added, removed, query.columns());
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
                [&]<typename Tag>(QueryView<Values, Tag> operation)
                {
                    auto& state = std::get<NodeState<Tag>>(m_nodes[i]);
                    // Deltas of child nodes are exact by construction, so they skip validation.
                    if constexpr (std::same_as<Tag, QueryProjectTag>)
                        state.apply(delta(operation.get_arg()).change(), m_workspace);
                    else if constexpr (std::same_as<Tag, QueryJoinTag>)
                        state.apply(delta(operation.get_lhs()).change(), delta(operation.get_rhs()).change(), m_workspace);
                    else if constexpr (std::same_as<Tag, QueryDistanceTag>)
                    {
                        if constexpr (ColumnValueFor<uint_t, Values>)
                            state.apply(delta(operation.get_sources()).change(), delta(operation.get_edges()).change(), delta(operation.get_targets()).change());
                    }
                    else if constexpr (std::same_as<Tag, QueryGenericJoinTag>)
                        state.apply(operation.get_inputs() | std::views::transform([&](NodeId id) { return delta(id).change(); }));
                    else
                        update_value(state, operation, changes);
                },
                m_plan[NodeId(to_uint_t(i))].get_variant());
        m_initialized = true;
    }
    const Builder<Relation<Values>>& get_result(size_t root = 0) const&
    {
        detail::require_initialized(m_initialized);
        if (root >= m_plan.root_count())
            throw std::out_of_range("Incremental query: invalid root.");
        return result(m_plan.roots()[root]);
    }
    const Builder<Relation<Values>>& get_result(size_t = 0) const&& = delete;
    const Delta<Values>& get_delta(size_t root = 0) const&
    {
        detail::require_initialized(m_initialized);
        if (root >= m_plan.root_count())
            throw std::out_of_range("Incremental query: invalid root.");
        return delta(m_plan.roots()[root]);
    }
    const Delta<Values>& get_delta(size_t = 0) const&& = delete;
    /// Retained runtime storage; excludes the immutable query plan.
    size_t memory_usage() const noexcept
    {
        size_t result = m_workspace.memory_usage() + m_storage_addresses.capacity() * sizeof(const void*);
        for (const auto& node : m_nodes)
            result += std::visit([](const auto& state) { return state.memory_usage(); }, node);
        return result;
    }
};
}  // namespace ygg::database::incremental
#endif
