/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "yggdrasil/database/semantics/incremental/fixpoint.hpp"

#include "yggdrasil/core/config.hpp"
#include "yggdrasil/database/semantics/operations.hpp"
#include "yggdrasil/database/semantics/query_evaluation.hpp"

#include <algorithm>
#include <map>
#include <stdexcept>
#include <variant>

namespace ygg::database::incremental
{
std::vector<bool> nested_queries(const QueryPlan& plan)
{
    auto result = std::vector<bool>(plan.node_count());
    for (size_t i = 0; i < plan.node_count(); ++i)
    {
        const auto query = plan[Index<Query<>>(to_uint_t(i))];
        if (is<QueryRecursionTag>(query))
            result[i] = true;
        else if (!is<QueryFixpointTag>(query))
            for_each_child(query, [&](QueryView<> child) { result[i] = result[i] || result[child.get_index().get_value()]; });
    }
    return result;
}

/// lhs ⋉ rhs: the lhs rows with a match among distinct(π_keys rhs), and lhs ▷ rhs = lhs − lhs ⋉ rhs [Budiu23, §7.5];
/// a difference is the antijoin on all columns.
struct FixpointCircuit::Semijoin
{
    ProjectionPlan keys;
    ZSet projected;
    NestedDistinct distinct;
    ZSet distinct_change;
    NestedJoin join;
    ZSet joined;
    bool anti;
    std::vector<std::byte> row;

    Semijoin(std::span<const ColumnLayout> lhs, std::span<const ColumnLayout> rhs, bool anti_) :
        keys(rhs, shared_labels(lhs, rhs)),
        projected(keys.output_columns().span()),
        distinct(keys.output_columns().span()),
        distinct_change(keys.output_columns().span()),
        join(JoinPlan(lhs, keys.output_columns().span())),
        joined(lhs),
        anti(anti_)
    {
    }

    static std::vector<Index<Column>> shared_labels(std::span<const ColumnLayout> lhs, std::span<const ColumnLayout> rhs)
    {
        auto result = std::vector<Index<Column>>();
        for (const auto& column : rhs)
            if (std::ranges::find(lhs, column.label, &ColumnLayout::label) != lhs.end())
                result.push_back(column.label);
        return result;
    }

    void step(const ZSet& lhs, const ZSet& rhs, Iteration iteration, ZSet& output)
    {
        projected.clear();
        for (size_t i = 0; i < rhs.size(); ++i)
        {
            row.clear();
            database::detail::append_fields(row, rhs.row(i), keys.positions());
            projected.add(row, rhs.weight(i));
        }
        distinct.step(projected, iteration, distinct_change);
        join.step(lhs, distinct_change, iteration, joined);
        output.clear();
        if (anti)
            output.add(lhs);
        output.add(joined, anti ? -1 : 1);
    }
    void commit()
    {
        distinct.commit();
        join.commit();
    }
    void clear() noexcept
    {
        distinct.clear();
        join.clear();
    }
    [[nodiscard]] bool pending_after(Iteration iteration) const noexcept { return distinct.pending_after(iteration) || join.pending_after(iteration); }
    [[nodiscard]] size_t memory_usage() const noexcept
    { return distinct.memory_usage() + join.memory_usage() + projected.memory_usage() + joined.memory_usage(); }
};

/// The natural join of several inputs as a chain of binary nested joins, projected onto the output order.
struct FixpointCircuit::GenericJoin
{
    std::vector<NestedJoin> joins;
    std::vector<ZSet> stages;
    ProjectionPlan output;
    std::vector<std::byte> row;

    explicit GenericJoin(QueryView<QueryGenericJoinTag> query)
    {
        const auto inputs = query.get_inputs();
        auto columns = std::vector<ColumnLayout>();
        const auto first = QueryView<>(inputs.front(), query.get_repository()).columns();
        columns.assign(first.begin(), first.end());
        joins.reserve(inputs.size());
        stages.reserve(inputs.size());
        for (size_t k = 1; k < inputs.size(); ++k)
        {
            auto plan = JoinPlan(columns, QueryView<>(inputs[k], query.get_repository()).columns());
            const auto joined = plan.output_columns().span();
            columns.assign(joined.begin(), joined.end());
            joins.emplace_back(std::move(plan));
            stages.emplace_back(joins.back().columns());
        }
        output = ProjectionPlan(columns, query.get_output_order());
    }

    void step(std::span<const ZSet* const> inputs, Iteration iteration, ZSet& result)
    {
        const ZSet* current = inputs.front();
        for (size_t k = 0; k < joins.size(); ++k)
        {
            joins[k].step(*current, *inputs[k + 1], iteration, stages[k]);
            current = &stages[k];
        }
        result.clear();
        for (size_t i = 0; i < current->size(); ++i)
        {
            row.clear();
            database::detail::append_fields(row, current->row(i), output.positions());
            result.add(row, current->weight(i));
        }
    }
    void commit()
    {
        for (auto& join : joins)
            join.commit();
    }
    void clear() noexcept
    {
        for (auto& join : joins)
            join.clear();
    }
    [[nodiscard]] bool pending_after(Iteration iteration) const noexcept
    {
        return std::ranges::any_of(joins, [&](const NestedJoin& join) { return join.pending_after(iteration); });
    }
    [[nodiscard]] size_t memory_usage() const noexcept
    {
        size_t result = 0;
        for (const auto& join : joins)
            result += join.memory_usage();
        for (const auto& stage : stages)
            result += stage.memory_usage();
        return result;
    }
};

FixpointCircuit::Variable::Variable(std::span<const ColumnLayout> columns) :
    distinct(columns),
    change(columns),
    previous(columns),
    total(columns),
    result(columns),
    delta(columns)
{
}

FixpointCircuit::FixpointCircuit(QueryView<QueryFixpointTag> fixpoint) :
    m_scope(fixpoint.get_scope()),
    m_bodies(fixpoint.get_bodies().begin(), fixpoint.get_bodies().end())
{
    const auto& repository = fixpoint.get_repository();
    auto roots = std::vector<QueryView<>>();
    for (const auto body : m_bodies)
        roots.emplace_back(body, repository);
    const auto queries = reachable(roots);
    // The component: the queries that read one of its recursion leaves, not through another fixpoint.
    auto inside = std::map<size_t, size_t>();
    auto imported = std::map<size_t, size_t>();
    auto nested = std::vector<QueryView<>>();
    for (const auto query : queries)
    {
        bool reads = is<QueryRecursionTag>(query) && as<QueryRecursionTag>(query).get_scope() == m_scope;
        if (!is<QueryFixpointTag>(query))
            for_each_child(query, [&](QueryView<> child) { reads = reads || inside.contains(child.get_index().get_value()); });
        if (reads)
        {
            inside.emplace(query.get_index().get_value(), nested.size());
            nested.push_back(query);
        }
    }
    const auto import = [&](QueryView<> query)
    {
        const auto [it, inserted] = imported.try_emplace(query.get_index().get_value(), m_imports.size());
        if (inserted)
            m_imports.push_back(query);
        return it->second;
    };
    for (const auto query : nested)
        for_each_child(query,
                       [&](QueryView<> child)
                       {
                           if (!inside.contains(child.get_index().get_value()))
                               import(child);
                       });
    for (const auto body : roots)
        if (!inside.contains(body.get_index().get_value()))
            import(body);
    // Operands point into these vectors: they are complete before any pointer is taken.
    m_import_changes.reserve(m_imports.size());
    for (const auto query : m_imports)
        m_import_changes.emplace_back(query.columns());
    m_relations.reserve(roots.size());
    for (const auto body : roots)
        m_relations.emplace_back(body.columns());
    m_nodes.reserve(nested.size());
    for (const auto query : nested)
    {
        auto& node = m_nodes.emplace_back(Node { query, ZSet(query.columns()), {}, std::monostate {} });
        if (is<QueryRecursionTag>(query))
            node.operands.push_back(&m_relations.at(as<QueryRecursionTag>(query).get_variable()).previous);
        for_each_child(query,
                       [&](QueryView<> child)
                       {
                           const auto index = child.get_index().get_value();
                           const auto it = inside.find(index);
                           node.operands.push_back(it != inside.end() ? &m_nodes[it->second].change : &m_import_changes[imported.at(index)]);
                       });
        ygg::visit(
            [&]<typename Tag>(QueryView<Tag> operation)
            {
                if constexpr (std::same_as<Tag, QueryJoinTag>)
                    node.state = std::make_unique<NestedJoin>(operation.get_plan());
                else if constexpr (std::same_as<Tag, QuerySemijoinTag> || std::same_as<Tag, QueryAntijoinTag>)
                    node.state = std::make_unique<Semijoin>(operation.get_lhs().columns(), operation.get_rhs().columns(), std::same_as<Tag, QueryAntijoinTag>);
                else if constexpr (std::same_as<Tag, QueryDifferenceTag>)
                    node.state = std::make_unique<Semijoin>(operation.get_lhs().columns(), operation.get_rhs().columns(), true);
                else if constexpr (std::same_as<Tag, QueryGenericJoinTag>)
                    node.state = std::make_unique<GenericJoin>(operation);
                else if constexpr (std::same_as<Tag, QueryAggregateTag> || std::same_as<Tag, QueryDistanceTag>)
                    throw std::invalid_argument("Fixpoint: aggregates and distances cannot read their own component's relations.");
            },
            query.get_variant());
    }
    for (size_t v = 0; v < roots.size(); ++v)
    {
        const auto index = roots[v].get_index().get_value();
        const auto it = inside.find(index);
        m_relations[v].body = it != inside.end() ? &m_nodes[it->second].change : &m_import_changes[imported.at(index)];
    }
}

FixpointCircuit::~FixpointCircuit() = default;

void FixpointCircuit::reset()
{
    for (auto& node : m_nodes)
        std::visit(
            [&]<typename State>(State& state)
            {
                if constexpr (!std::same_as<State, std::monostate>)
                    state->clear();
            },
            node.state);
    for (auto& relation : m_relations)
    {
        relation.distinct.clear();
        relation.result.clear();
        relation.delta.clear();
    }
}

void FixpointCircuit::step(Node& node, Iteration iteration)
{
    const auto& operands = node.operands;
    ygg::visit(
        [&]<typename Tag>(QueryView<Tag> operation)
        {
            auto& change = node.change;
            if constexpr (std::same_as<Tag, QueryJoinTag>)
                std::get<std::unique_ptr<NestedJoin>>(node.state)->step(*operands[0], *operands[1], iteration, change);
            else if constexpr (std::same_as<Tag, QuerySemijoinTag> || std::same_as<Tag, QueryAntijoinTag> || std::same_as<Tag, QueryDifferenceTag>)
                std::get<std::unique_ptr<Semijoin>>(node.state)->step(*operands[0], *operands[1], iteration, change);
            else if constexpr (std::same_as<Tag, QueryGenericJoinTag>)
                std::get<std::unique_ptr<GenericJoin>>(node.state)->step(operands, iteration, change);
            else
            {
                change.clear();
                if constexpr (std::same_as<Tag, QueryRecursionTag> || std::same_as<Tag, QueryRenameTag>)
                    for (size_t i = 0; i < operands[0]->size(); ++i)
                        change.add(operands[0]->row(i), operands[0]->weight(i));
                else if constexpr (std::same_as<Tag, QuerySelectEqualTag> || std::same_as<Tag, QuerySelectValueTag>)
                {
                    for (size_t i = 0; i < operands[0]->size(); ++i)
                        if (database::detail::query_accepts(operation, operands[0]->row(i)))
                            change.add(operands[0]->row(i), operands[0]->weight(i));
                }
                else if constexpr (std::same_as<Tag, QueryProjectTag>)
                {
                    auto row = std::vector<std::byte>();
                    for (size_t i = 0; i < operands[0]->size(); ++i)
                    {
                        row.clear();
                        database::detail::append_fields(row, operands[0]->row(i), operation.get_plan().positions());
                        change.add(row, operands[0]->weight(i));
                    }
                }
                else if constexpr (std::same_as<Tag, QueryUnionTag>)
                {
                    change.add(*operands[0]);
                    change.add(*operands[1]);
                }
                else
                    static_assert(std::same_as<Tag, QueryEmptyTag> || std::same_as<Tag, QueryInputTag> || std::same_as<Tag, QueryFixpointTag>
                                      || std::same_as<Tag, QueryAggregateTag> || std::same_as<Tag, QueryDistanceTag>,
                                  "unhandled query operator");
            }
        },
        node.query.get_variant());
}

namespace
{
/// Calls f on the state of an operator that has one; get_if rather than std::visit, which throws for a valueless variant.
template<typename... States, typename F>
void visit_state(const std::variant<std::monostate, States...>& state, const F& f) noexcept
{ ((std::get_if<States>(&state) ? f(**std::get_if<States>(&state)) : void()), ...); }
}  // namespace

bool FixpointCircuit::pending_after(Iteration iteration) const noexcept
{
    const auto pending = [&](const Node& node)
    {
        bool result = false;
        visit_state(node.state, [&](const auto& state) { result = state.pending_after(iteration); });
        return result;
    };
    return std::ranges::any_of(m_nodes, pending)
           || std::ranges::any_of(m_relations, [&](const Variable& relation) { return relation.distinct.pending_after(iteration); });
}

void FixpointCircuit::run()
{
    for (Iteration iteration = 0;; ++iteration)
    {
        for (auto& node : m_nodes)
            step(node, iteration);
        bool changed = false;
        for (auto& relation : m_relations)
        {
            relation.distinct.step(*relation.body, iteration, relation.change);
            relation.total.add(relation.change);
            changed = changed || !relation.change.empty();
        }
        for (auto& relation : m_relations)
        {
            relation.previous.clear();
            relation.previous.add(relation.change);
        }
        // The imports are constant over the iterations: they change at iteration 0 only.
        if (iteration == 0)
            for (auto& change : m_import_changes)
                change.clear();
        if (!changed && !pending_after(iteration))
            break;
    }
    for (auto& node : m_nodes)
        std::visit(
            [&]<typename State>(State& state)
            {
                if constexpr (!std::same_as<State, std::monostate>)
                    state->commit();
            },
            node.state);
    for (auto& relation : m_relations)
    {
        relation.distinct.commit();
        relation.previous.clear();
        relation.delta.clear();
        for (size_t i = 0; i < relation.total.size(); ++i)
        {
            const auto row = relation.total.row(i);
            if (relation.total.weight(i) > 0)
            {
                relation.delta.added.insert(row);
                relation.result.insert(row);
            }
            else
            {
                relation.delta.removed.insert(row);
                const auto position = relation.result.find(row);
                YGG_ASSUME(position.has_value());  // a removed row is in the result
                relation.result.erase(*position);
            }
        }
        relation.total.clear();
    }
}

size_t FixpointCircuit::memory_usage() const noexcept
{
    size_t result = 0;
    for (const auto& node : m_nodes)
    {
        result += node.change.memory_usage();
        visit_state(node.state, [&](const auto& state) { result += state.memory_usage(); });
    }
    for (const auto& relation : m_relations)
        result += relation.distinct.memory_usage() + relation.result.memory_usage() + relation.delta.memory_usage();
    for (const auto& change : m_import_changes)
        result += change.memory_usage();
    return result;
}
}  // namespace ygg::database::incremental
