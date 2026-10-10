/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_SEMANTICS_QUERY_EVALUATION_HPP_
#define YGG_DATABASE_SEMANTICS_QUERY_EVALUATION_HPP_

#include "yggdrasil/database/syntax/query.hpp"

#include <ranges>
#include <variant>

namespace ygg::database
{
namespace detail
{
template<ColumnTypes Values>
bool query_accepts(QueryView<Values, QuerySelectEqualTag> selection, std::span<const std::byte> row)
{
    const auto columns = selection.columns();
    return std::ranges::equal(column_bytes(row, columns[selection.get_lhs_position()]), column_bytes(row, columns[selection.get_rhs_position()]));
}

template<ColumnTypes Values>
bool query_accepts(QueryView<Values, QuerySelectValueTag> selection, std::span<const std::byte> row)
{
    return std::ranges::equal(column_bytes(row, selection.columns()[selection.get_position()]), selection.get_constant());
}

template<ColumnTypes Values, bool Enabled = ColumnValueFor<uint_t, Values>>
struct QueryDistanceWorkspace
{
    explicit QueryDistanceWorkspace(const DistancePlan<Values>&) { throw std::invalid_argument("Query: distance requires uint_t columns."); }
    size_t memory_usage() const noexcept { return 0; }
};

template<ColumnTypes Values>
struct QueryDistanceWorkspace<Values, true>
{
    DistanceWorkspace<Values> workspace;
    explicit QueryDistanceWorkspace(const DistancePlan<Values>& plan) : workspace(plan) {}
    size_t memory_usage() const noexcept { return workspace.memory_usage(); }
};

template<ColumnTypes Values>
size_t query_workspace_memory(const Workspace<Values>& workspace) noexcept
{
    return workspace.columns.capacity() * sizeof(ColumnLayout)
           + (workspace.lhs_keys.capacity() + workspace.rhs_keys.capacity() + workspace.rhs_payload.capacity()) * sizeof(ColumnSlice) + workspace.row.capacity()
           + workspace.join_index.memory_usage();
}
}  // namespace detail

/// Reusable full DAG evaluation. Inputs are borrowed only during evaluate().
/// Results are owned here and borrowed until the next evaluate() or destruction.
/// Schema/alias errors preserve results; failures during execution invalidate them.
template<ColumnTypes Values = DefaultColumnTypes>
class QueryEvaluator
{
    using NodeId = Index<Query<Values>>;
    using DistanceState = detail::QueryDistanceWorkspace<Values>;
    using State = std::variant<std::monostate, GenericJoinWorkspace<Values>, DistanceState>;
    struct Node
    {
        Builder<Relation<Values>> result;
        State state;
        explicit Node(QueryView<Values> query) :
            result(query.columns()),
            state(ygg::visit(
                []<typename Concrete>(Concrete operation) -> State
                {
                    if constexpr (std::same_as<Concrete, QueryView<Values, QueryGenericJoinTag>>)
                        return GenericJoinWorkspace<Values>(operation.get_plan());
                    else if constexpr (std::same_as<Concrete, QueryView<Values, QueryDistanceTag>>)
                        return DistanceState(operation.get_plan());
                    else
                        return std::monostate {};
                },
                query.get_variant()))
        {
        }
    };
    QueryPlan<Values> m_plan;
    std::vector<Node> m_nodes;
    Workspace<Values> m_workspace;
    bool m_ready = false;

public:
    explicit QueryEvaluator(QueryPlan<Values> plan) : m_plan(std::move(plan))
    {
        m_nodes.reserve(m_plan.node_count());
        for (size_t i = 0; i < m_plan.node_count(); ++i)
            m_nodes.emplace_back(m_plan[NodeId(to_uint_t(i))]);
    }
    template<RelationViewRange<Values> R>
    void evaluate(const R& bindings)
    {
        for (size_t i = 0; i < m_plan.node_count(); ++i)
            ygg::visit(
                [&]<typename Concrete>(Concrete operation)
                {
                    if constexpr (std::same_as<Concrete, QueryView<Values, QueryInputTag>>)
                    {
                        if (operation.get_input_slot() >= std::ranges::size(bindings))
                            throw std::invalid_argument("Query: missing input binding.");
                        const auto& input = bindings[operation.get_input_slot()];
                        detail::require_plan_columns(input.columns().span(), operation.columns());
                        for (const auto& state : m_nodes)
                            if (input.get_storage_address() == state.result.get_storage_address())
                                throw std::invalid_argument("Query: input aliases an evaluator result.");
                    }
                },
                m_plan[NodeId(to_uint_t(i))].get_variant());
        m_ready = false;
        for (size_t i = 0; i < m_nodes.size(); ++i)
        {
            auto& state = m_nodes[i];
            auto& out = state.result;
            const auto child = [&](QueryView<Values> query) -> const auto& { return m_nodes[query.get_index().get_value()].result; };
            ygg::visit(
                [&]<typename Concrete>(Concrete operation)
                {
                    if constexpr (std::same_as<Concrete, QueryView<Values, QueryInputTag>>)
                        assign(out, bindings[operation.get_input_slot()]);
                    else if constexpr (std::same_as<Concrete, QueryView<Values, QueryEmptyTag>>)
                        out.clear();
                    else if constexpr (std::same_as<Concrete, QueryView<Values, QueryJoinTag>>)
                        database::join(child(operation.get_lhs()), child(operation.get_rhs()), operation.get_plan(), out, m_workspace);
                    else if constexpr (std::same_as<Concrete, QueryView<Values, QueryProjectTag>>)
                        database::project(child(operation.get_arg()), operation.get_plan(), out, m_workspace);
                    else if constexpr (std::same_as<Concrete, QueryView<Values, QueryRenameTag>>)
                    {
                        assign(out, child(operation.get_arg()));
                        out.rename(operation.columns());
                    }
                    else if constexpr (std::same_as<Concrete, QueryView<Values, QuerySelectEqualTag>>)
                        select_equal_columns(child(operation.get_arg()), operation.get_lhs_column(), operation.get_rhs_column(), out);
                    else if constexpr (std::same_as<Concrete, QueryView<Values, QuerySelectValueTag>>)
                        select(child(operation.get_arg()), [&](Row<Values> row) { return detail::query_accepts(operation, row.bytes()); }, out);
                    else if constexpr (std::same_as<Concrete, QueryView<Values, QueryUnionTag>>)
                        database::union_(child(operation.get_lhs()), child(operation.get_rhs()), out);
                    else if constexpr (std::same_as<Concrete, QueryView<Values, QueryDifferenceTag>>)
                        database::difference(child(operation.get_lhs()), child(operation.get_rhs()), out);
                    else if constexpr (std::same_as<Concrete, QueryView<Values, QueryGenericJoinTag>>)
                    {
                        const auto inputs = operation.get_inputs()
                                            | std::views::transform([&](NodeId id) -> const auto& { return m_nodes[id.get_value()].result; });
                        generic_join<Values>(inputs, operation.get_plan(), out, std::get<GenericJoinWorkspace<Values>>(state.state));
                    }
                    else if constexpr (std::same_as<Concrete, QueryView<Values, QueryDistanceTag>>)
                    {
                        if constexpr (ColumnValueFor<uint_t, Values>)
                            database::distance(child(operation.get_sources()),
                                               child(operation.get_edges()),
                                               child(operation.get_targets()),
                                               operation.get_plan(),
                                               out,
                                               std::get<DistanceState>(state.state).workspace);
                    }
                },
                m_plan[NodeId(to_uint_t(i))].get_variant());
        }
        m_ready = true;
    }
    const Builder<Relation<Values>>& get_result(size_t root = 0) const&
    {
        if (!m_ready)
            throw std::logic_error("Query: evaluate before reading results.");
        if (root >= m_plan.root_count())
            throw std::out_of_range("Query: invalid root.");
        return m_nodes.at(m_plan.roots()[root].get_value()).result;
    }
    const Builder<Relation<Values>>& get_result(size_t = 0) const&& = delete;
    /// Retained runtime storage; excludes the immutable query plan.
    size_t memory_usage() const noexcept
    {
        size_t result = detail::query_workspace_memory(m_workspace);
        for (const auto& node : m_nodes)
        {
            result += node.result.memory_usage();
            std::visit(
                [&]<typename Prepared>(const Prepared& state)
                {
                    if constexpr (!std::same_as<Prepared, std::monostate>)
                        result += state.memory_usage();
                },
                node.state);
        }
        return result;
    }
};
}  // namespace ygg::database
#endif
