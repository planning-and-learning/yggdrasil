/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_EVALUATION_FULL_HPP_
#define YGG_DATABASE_EVALUATION_FULL_HPP_

#include "yggdrasil/database/operators/distance/distance.hpp"
#include "yggdrasil/database/operators/generic_join/generic_join.hpp"
#include "yggdrasil/database/operators/relational/join.hpp"
#include "yggdrasil/database/operators/relational/project.hpp"
#include "yggdrasil/database/operators/relational/select.hpp"
#include "yggdrasil/database/operators/relational/semijoin.hpp"
#include "yggdrasil/database/operators/relational/set.hpp"
#include "yggdrasil/database/query/query.hpp"

#include <algorithm>
#include <concepts>
#include <ranges>
#include <variant>

namespace ygg::database
{
namespace detail
{
bool query_accepts(QueryView<QuerySelectEqualTag> selection, std::span<const std::byte> row);

bool query_accepts(QueryView<QuerySelectValueTag> selection, std::span<const std::byte> row);

/// Distances are evaluated only with uint_t columns.

/// Validates each input binding of the plan; inputs must not alias the evaluator's storage.
template<RelationViewRange R, std::predicate<const void*> IsInternal>
void validate_bindings(const QueryPlan& plan, const R& bindings, IsInternal is_internal)
{
    for (size_t i = 0; i < plan.node_count(); ++i)
    {
        const auto query = plan[Index<Query<>>(to_uint_t(i))];
        if (!is<QueryInputTag>(query))
            continue;
        const auto slot = as<QueryInputTag>(query).get_input_slot();
        if (slot >= std::ranges::size(bindings))
            throw std::invalid_argument("Query: missing input binding.");
        require_plan_columns(bindings[slot].columns().span(), query.columns());
        if (is_internal(bindings[slot].get_storage_address()))
            throw std::invalid_argument("Query: input aliases evaluator storage.");
    }
}

struct QueryDistanceWorkspace
{
    DistanceWorkspace workspace;
    explicit QueryDistanceWorkspace(const DistancePlan& plan) : workspace(plan) {}
    [[nodiscard]] size_t memory_usage() const noexcept { return workspace.memory_usage(); }
};

/// Operators whose result is a function of the operands' results alone.
template<class Tag>
concept StatelessQueryTag =
    std::same_as<Tag, QueryInputTag> || std::same_as<Tag, QueryEmptyTag> || std::same_as<Tag, QueryRenameTag> || std::same_as<Tag, QuerySelectEqualTag>
    || std::same_as<Tag, QuerySelectValueTag> || std::same_as<Tag, QueryUnionTag> || std::same_as<Tag, QueryDifferenceTag>;

/// Evaluates a stateless operator from the bound inputs and the operands' results.
template<StatelessQueryTag Tag, RelationViewRange R, std::invocable<QueryView<>> GetResult>
void evaluate_stateless(QueryView<Tag> operation, const R& bindings, GetResult result, Builder<Relation>& out)
{
    if constexpr (std::same_as<Tag, QueryInputTag>)
        database::assign(out, bindings[operation.get_input_slot()]);
    else if constexpr (std::same_as<Tag, QueryEmptyTag>)
        out.clear();
    else if constexpr (std::same_as<Tag, QueryRenameTag>)
    {
        database::assign(out, result(operation.get_arg()));
        out.rename(operation.columns());
    }
    else if constexpr (std::same_as<Tag, QuerySelectEqualTag>)
        database::select_equal_columns(result(operation.get_arg()), operation.get_lhs_column(), operation.get_rhs_column(), out);
    else if constexpr (std::same_as<Tag, QuerySelectValueTag>)
        database::select(result(operation.get_arg()), [&](Row row) { return query_accepts(operation, row.bytes()); }, out);
    else if constexpr (std::same_as<Tag, QueryUnionTag>)
        database::union_(result(operation.get_lhs()), result(operation.get_rhs()), out);
    else
        database::difference(result(operation.get_lhs()), result(operation.get_rhs()), out);
}
}  // namespace detail

/// Reusable full DAG evaluation. Inputs are borrowed only during evaluate().
/// Results are owned here and borrowed until the next evaluate() or destruction.
/// Schema/alias errors preserve results; failures during execution invalidate them.
class QueryEvaluator
{
    using NodeId = Index<Query<>>;
    using DistanceState = detail::QueryDistanceWorkspace;
    using State = std::variant<std::monostate, GenericJoinWorkspace, DistanceState>;
    struct Node
    {
        Builder<Relation> result;
        State state;
        explicit Node(QueryView<> query) :
            result(query.columns()),
            state(ygg::visit(
                []<typename Tag>(QueryView<Tag> operation) -> State
                {
                    if constexpr (std::same_as<Tag, QueryGenericJoinTag>)
                        return GenericJoinWorkspace(operation.get_plan());
                    else if constexpr (std::same_as<Tag, QueryDistanceTag>)
                        return DistanceState(operation.get_plan());
                    else
                        return std::monostate {};
                },
                query.get_variant()))
        {
        }
    };
    QueryPlan m_plan;
    std::vector<Node> m_nodes;
    Workspace m_workspace;
    bool m_ready = false;

public:
    explicit QueryEvaluator(QueryPlan plan) : m_plan(std::move(plan))
    {
        m_nodes.reserve(m_plan.node_count());
        for (size_t i = 0; i < m_plan.node_count(); ++i)
            m_nodes.emplace_back(m_plan[NodeId(to_uint_t(i))]);
    }
    template<RelationViewRange R>
    void evaluate(const R& bindings)
    {
        detail::validate_bindings(m_plan,
                                  bindings,
                                  [&](const void* address)
                                  { return std::ranges::any_of(m_nodes, [&](const Node& node) { return node.result.get_storage_address() == address; }); });
        m_ready = false;
        for (size_t i = 0; i < m_nodes.size(); ++i)
        {
            auto& state = m_nodes[i];
            auto& out = state.result;
            const auto child = [&](QueryView<> query) -> const auto& { return m_nodes[query.get_index().get_value()].result; };
            ygg::visit(
                [&]<typename Tag>(QueryView<Tag> operation)
                {
                    if constexpr (detail::StatelessQueryTag<Tag>)
                        detail::evaluate_stateless(operation, bindings, child, out);
                    else if constexpr (std::same_as<Tag, QueryJoinTag>)
                        database::join(child(operation.get_lhs()), child(operation.get_rhs()), operation.get_plan(), out, m_workspace);
                    else if constexpr (std::same_as<Tag, QuerySemijoinTag>)
                        database::semijoin(child(operation.get_lhs()), child(operation.get_rhs()), operation.get_plan(), out, m_workspace);
                    else if constexpr (std::same_as<Tag, QueryAntijoinTag>)
                        database::antijoin(child(operation.get_lhs()), child(operation.get_rhs()), operation.get_plan(), out, m_workspace);
                    else if constexpr (std::same_as<Tag, QueryProjectTag>)
                        database::project(child(operation.get_arg()), operation.get_plan(), out, m_workspace);
                    else if constexpr (std::same_as<Tag, QueryGenericJoinTag>)
                    {
                        const auto inputs =
                            operation.get_inputs() | std::views::transform([&](NodeId id) -> const auto& { return m_nodes[id.get_value()].result; });
                        generic_join(inputs, operation.get_plan(), out, std::get<GenericJoinWorkspace>(state.state));
                    }
                    else
                    {
                        static_assert(std::same_as<Tag, QueryDistanceTag>);
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
    [[nodiscard]] const Builder<Relation>& get_result(size_t root = 0) const&
    {
        if (!m_ready)
            throw std::logic_error("Query: evaluate before reading results.");
        if (root >= m_plan.root_count())
            throw std::out_of_range("Query: invalid root.");
        return m_nodes.at(m_plan.roots()[root].get_value()).result;
    }
    [[nodiscard]] const Builder<Relation>& get_result(size_t = 0) const&& = delete;
    /// Retained runtime storage; excludes the immutable query plan.
    [[nodiscard]] size_t memory_usage() const noexcept;
};
}  // namespace ygg::database
#endif
