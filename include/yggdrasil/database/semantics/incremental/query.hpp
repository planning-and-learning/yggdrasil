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
    explicit QueryDistance(QueryView<Values, QueryDistanceTag> operation) : QueryValue<Values>(operation.columns())
    {
        throw std::invalid_argument("Query: distance requires uint_t columns.");
    }
};
template<ColumnTypes Values>
struct QueryDistance<Values, true> : DistanceEvaluator<Values>
{
    explicit QueryDistance(QueryView<Values, QueryDistanceTag> operation) : DistanceEvaluator<Values>(operation.get_plan()) {}
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
    QueryPlan<Values> m_plan;
    std::vector<State> m_nodes;
    Workspace<Values> m_workspace;
    std::vector<const void*> m_storage_addresses;
    bool m_ready = false;

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
    template<RelationViewConcept<Values> V>
    void require_external(const V& input) const
    {
        if (std::ranges::find(m_storage_addresses, input.get_storage_address()) != m_storage_addresses.end())
            throw std::invalid_argument("Incremental query: input aliases evaluator storage.");
    }
    template<typename Concrete, RelationViewRange<Values> R>
    void initialize_value(Value& value, Concrete operation, const R& inputs)
    {
        value.result.clear();
        value.delta.clear();
        if constexpr (std::same_as<Concrete, QueryView<Values, QueryInputTag>>)
            assign(value.result, inputs[operation.get_input_slot()]);
        else if constexpr (std::same_as<Concrete, QueryView<Values, QueryRenameTag>>)
        {
            assign(value.result, result(operation.get_arg()));
            value.result.rename(operation.columns());
        }
        else if constexpr (std::same_as<Concrete, QueryView<Values, QuerySelectEqualTag>>)
            select_equal_columns(result(operation.get_arg()), operation.get_lhs_column(), operation.get_rhs_column(), value.result);
        else if constexpr (std::same_as<Concrete, QueryView<Values, QuerySelectValueTag>>)
            select(result(operation.get_arg()), [&](Row<Values> row) { return database::detail::query_accepts(operation, row.bytes()); }, value.result);
        else if constexpr (std::same_as<Concrete, QueryView<Values, QueryUnionTag>>)
            database::union_(result(operation.get_lhs()), result(operation.get_rhs()), value.result);
        else if constexpr (std::same_as<Concrete, QueryView<Values, QueryDifferenceTag>>)
            database::difference(result(operation.get_lhs()), result(operation.get_rhs()), value.result);
        else
            static_assert(std::same_as<Concrete, QueryView<Values, QueryEmptyTag>>);
    }
    template<typename Concrete, RelationDeltaRange<Values> R>
    void update_value(Value& value, Concrete operation, const R& changes)
    {
        value.delta.clear();
        if constexpr (std::same_as<Concrete, QueryView<Values, QueryEmptyTag>>)
            return;
        else if constexpr (std::same_as<Concrete, QueryView<Values, QueryUnionTag>> || std::same_as<Concrete, QueryView<Values, QueryDifferenceTag>>)
        {
            const auto& lhs = result(operation.get_lhs());
            const auto& rhs = result(operation.get_rhs());
            const auto reconcile = [&](const auto& rows)
            {
                for (size_t i = 0; i < rows.size(); ++i)
                {
                    const Row<Values> row(rows.row(i), operation.columns());
                    const bool present = std::same_as<Concrete, QueryView<Values, QueryUnionTag>> ? lhs.contains(row) || rhs.contains(row) :
                                                                                                    lhs.contains(row) && !rhs.contains(row);
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
                    if constexpr (std::same_as<Concrete, QueryView<Values, QuerySelectEqualTag>>
                                  || std::same_as<Concrete, QueryView<Values, QuerySelectValueTag>>)
                        if (!database::detail::query_accepts(operation, row))
                            continue;
                    value.set(row, present);
                }
            };
            if constexpr (std::same_as<Concrete, QueryView<Values, QueryInputTag>>)
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
                [&]<typename Concrete>(Concrete operation)
                {
                    if constexpr (std::same_as<Concrete, QueryView<Values, QueryProjectTag>>)
                        m_nodes.emplace_back(std::in_place_type<ProjectionEvaluator<Values>>, operation.get_plan());
                    else if constexpr (std::same_as<Concrete, QueryView<Values, QueryJoinTag>>)
                        m_nodes.emplace_back(std::in_place_type<JoinEvaluator<Values>>, operation.get_plan());
                    else if constexpr (std::same_as<Concrete, QueryView<Values, QueryDistanceTag>>)
                        m_nodes.emplace_back(std::in_place_type<Distance>, operation);
                    else if constexpr (std::same_as<Concrete, QueryView<Values, QueryGenericJoinTag>>)
                        m_nodes.emplace_back(std::in_place_type<GenericJoinEvaluator<Values>>, operation.get_plan());
                    else
                        m_nodes.emplace_back(std::in_place_type<Value>, operation.columns());
                },
                m_plan[NodeId(to_uint_t(i))].get_variant());
    }
    template<RelationViewRange<Values> R>
    void initialize(const R& bindings)
    {
        for (size_t i = 0; i < m_plan.node_count(); ++i)
            ygg::visit(
                [&]<typename Concrete>(Concrete operation)
                {
                    if constexpr (std::same_as<Concrete, QueryView<Values, QueryInputTag>>)
                    {
                        if (operation.get_input_slot() >= std::ranges::size(bindings))
                            throw std::invalid_argument("Incremental query: missing input binding.");
                        database::detail::require_plan_columns(bindings[operation.get_input_slot()].columns().span(), operation.columns());
                        require_external(bindings[operation.get_input_slot()]);
                    }
                },
                m_plan[NodeId(to_uint_t(i))].get_variant());
        m_ready = false;
        for (size_t i = 0; i < m_nodes.size(); ++i)
            ygg::visit(
                [&]<typename Concrete>(Concrete operation)
                {
                    if constexpr (std::same_as<Concrete, QueryView<Values, QueryProjectTag>>)
                        std::get<ProjectionEvaluator<Values>>(m_nodes[i]).initialize(result(operation.get_arg()), m_workspace);
                    else if constexpr (std::same_as<Concrete, QueryView<Values, QueryJoinTag>>)
                        std::get<JoinEvaluator<Values>>(m_nodes[i]).initialize(result(operation.get_lhs()), result(operation.get_rhs()), m_workspace);
                    else if constexpr (std::same_as<Concrete, QueryView<Values, QueryDistanceTag>>)
                    {
                        if constexpr (ColumnValueFor<uint_t, Values>)
                            std::get<Distance>(m_nodes[i])
                                .initialize(result(operation.get_sources()), result(operation.get_edges()), result(operation.get_targets()));
                    }
                    else if constexpr (std::same_as<Concrete, QueryView<Values, QueryGenericJoinTag>>)
                    {
                        const auto inputs = operation.get_inputs() | std::views::transform([&](NodeId id) -> const auto& { return result(id); });
                        std::get<GenericJoinEvaluator<Values>>(m_nodes[i]).initialize(inputs);
                    }
                    else
                        initialize_value(std::get<Value>(m_nodes[i]), operation, bindings);
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
        m_ready = true;
    }
    template<RelationDeltaRange<Values> R>
    void update(const R& changes)
    {
        if (!m_ready)
            throw std::logic_error("Incremental query: initialize before updating.");
        // Validate the whole external batch before changing any input baseline.
        for (size_t i = 0; i < m_plan.node_count(); ++i)
            ygg::visit(
                [&]<typename Concrete>(Concrete operation)
                {
                    if constexpr (std::same_as<Concrete, QueryView<Values, QueryInputTag>>)
                    {
                        if (operation.get_input_slot() >= std::ranges::size(changes))
                            throw std::invalid_argument("Incremental query: missing input delta.");
                        const auto& [added, removed] = changes[operation.get_input_slot()];
                        detail::require_delta<Values>(added, removed, operation.columns());
                        require_external(added);
                        require_external(removed);
                        const auto& baseline = result(NodeId(to_uint_t(i)));
                        for (size_t row = 0; row < added.size(); ++row)
                            if (baseline.contains(added.row(row)))
                                throw std::invalid_argument("Incremental query: added row is already present.");
                        for (size_t row = 0; row < removed.size(); ++row)
                            if (!baseline.contains(removed.row(row)))
                                throw std::invalid_argument("Incremental query: removed row is absent.");
                    }
                },
                m_plan[NodeId(to_uint_t(i))].get_variant());
        m_ready = false;
        for (size_t i = 0; i < m_nodes.size(); ++i)
            ygg::visit(
                [&]<typename Concrete>(Concrete operation)
                {
                    if constexpr (std::same_as<Concrete, QueryView<Values, QueryProjectTag>>)
                        std::get<ProjectionEvaluator<Values>>(m_nodes[i])
                            .update(delta(operation.get_arg()).added, delta(operation.get_arg()).removed, m_workspace);
                    else if constexpr (std::same_as<Concrete, QueryView<Values, QueryJoinTag>>)
                        std::get<JoinEvaluator<Values>>(m_nodes[i])
                            .update(delta(operation.get_lhs()).added,
                                    delta(operation.get_lhs()).removed,
                                    delta(operation.get_rhs()).added,
                                    delta(operation.get_rhs()).removed,
                                    m_workspace);
                    else if constexpr (std::same_as<Concrete, QueryView<Values, QueryDistanceTag>>)
                    {
                        if constexpr (ColumnValueFor<uint_t, Values>)
                            std::get<Distance>(m_nodes[i])
                                .update(delta(operation.get_sources()).added,
                                        delta(operation.get_sources()).removed,
                                        delta(operation.get_edges()).added,
                                        delta(operation.get_edges()).removed,
                                        delta(operation.get_targets()).added,
                                        delta(operation.get_targets()).removed);
                    }
                    else if constexpr (std::same_as<Concrete, QueryView<Values, QueryGenericJoinTag>>)
                    {
                        const auto deltas = operation.get_inputs()
                                            | std::views::transform(
                                                [&](NodeId id) { return std::pair<const Builder<Relation<Values>>&, const Builder<Relation<Values>>&>(delta(id).added, delta(id).removed); });
                        std::get<GenericJoinEvaluator<Values>>(m_nodes[i]).update(deltas);
                    }
                    else
                        update_value(std::get<Value>(m_nodes[i]), operation, changes);
                },
                m_plan[NodeId(to_uint_t(i))].get_variant());
        m_ready = true;
    }
    const Builder<Relation<Values>>& get_result(size_t root = 0) const&
    {
        if (!m_ready)
            throw std::logic_error("Incremental query: initialize before reading results.");
        if (root >= m_plan.root_count())
            throw std::out_of_range("Incremental query: invalid root.");
        return result(m_plan.roots()[root]);
    }
    const Builder<Relation<Values>>& get_result(size_t = 0) const&& = delete;
    const Delta<Values>& get_delta(size_t root = 0) const&
    {
        if (!m_ready)
            throw std::logic_error("Incremental query: initialize before reading changes.");
        if (root >= m_plan.root_count())
            throw std::out_of_range("Incremental query: invalid root.");
        return delta(m_plan.roots()[root]);
    }
    const Delta<Values>& get_delta(size_t = 0) const&& = delete;
    /// Retained runtime storage; excludes the immutable query plan.
    size_t memory_usage() const noexcept
    {
        size_t result = database::detail::query_workspace_memory(m_workspace) + m_storage_addresses.capacity() * sizeof(const void*);
        for (const auto& node : m_nodes)
            result += std::visit([](const auto& state) { return state.memory_usage(); }, node);
        return result;
    }
};
}  // namespace ygg::database::incremental
#endif
