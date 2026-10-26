/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_SEMANTICS_INCREMENTAL_FIXPOINT_HPP_
#define YGG_DATABASE_SEMANTICS_INCREMENTAL_FIXPOINT_HPP_

#include "yggdrasil/database/semantics/incremental/delta.hpp"
#include "yggdrasil/database/semantics/incremental/nested.hpp"
#include "yggdrasil/database/syntax/query.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <variant>
#include <vector>

namespace ygg::database::incremental
{
/// Per query of a plan, whether it is evaluated inside a fixpoint: a recursion leaf, or a query other than a fixpoint
/// that reads one. Fixpoints and everything else are evaluated by the enclosing (root) circuit.
std::vector<bool> nested_queries(const QueryPlan& plan);

/// The incremental fixpoint of one recursive component [Budiu23, circuit 6.1]: relation v is distinct(body v), the
/// bodies read the relations of the previous iteration through recursion leaves, and the queries they read outside the
/// component (imports) change only at iteration 0. One update runs iterations until no operator has a change or pending
/// work; each relation's change is the sum of its changes over the iterations. The first update from empty imports is
/// semi-naive evaluation (§5.1).
class FixpointCircuit
{
public:
    explicit FixpointCircuit(QueryView<QueryFixpointTag> fixpoint);
    FixpointCircuit(const FixpointCircuit&) = delete;
    FixpointCircuit& operator=(const FixpointCircuit&) = delete;
    FixpointCircuit(FixpointCircuit&&) = delete;
    FixpointCircuit& operator=(FixpointCircuit&&) = delete;
    ~FixpointCircuit();

    [[nodiscard]] size_t scope() const noexcept { return m_scope; }
    [[nodiscard]] std::span<const Index<Query<>>> bodies() const noexcept { return m_bodies; }
    /// The queries outside the component that its bodies read.
    [[nodiscard]] std::span<const QueryView<>> imports() const noexcept { return m_imports; }

    /// Forgets the history: the next update starts from empty imports and relations.
    void reset();
    /// Evaluates the fixpoint anew; `result(i)` is the value of import i.
    template<typename F>
    void initialize(const F& result)
    {
        reset();
        for (size_t i = 0; i < m_imports.size(); ++i)
        {
            m_import_changes[i].clear();
            m_import_changes[i].add(result(i), 1);
        }
        run();
    }
    /// Runs one update; `change(i)` is the (added, removed) change of import i.
    template<typename F>
    void update(const F& change)
    {
        for (size_t i = 0; i < m_imports.size(); ++i)
        {
            const auto& [added, removed] = change(i);
            m_import_changes[i].clear();
            m_import_changes[i].add(added, 1);
            m_import_changes[i].add(removed, -1);
        }
        run();
    }

    [[nodiscard]] const Builder<Relation>& result(size_t variable) const& { return m_relations.at(variable).result; }
    [[nodiscard]] const Builder<Relation>& result(size_t) const&& = delete;
    [[nodiscard]] const Delta& delta(size_t variable) const& { return m_relations.at(variable).delta; }
    [[nodiscard]] const Delta& delta(size_t) const&& = delete;
    [[nodiscard]] size_t memory_usage() const noexcept;

private:
    struct Semijoin;
    struct GenericJoin;
    /// An operator inside the component: its change at the current iteration, the changes it reads, and its state
    /// (none for linear operators).
    struct Node
    {
        QueryView<> query;
        ZSet change;
        std::vector<const ZSet*> operands;
        std::variant<std::monostate, std::unique_ptr<NestedJoin>, std::unique_ptr<Semijoin>, std::unique_ptr<GenericJoin>> state;
    };
    /// A relation of the component: distinct over its body, and its value and change at the end of an update.
    struct Variable
    {
        const ZSet* body = nullptr;
        NestedDistinct distinct;
        ZSet change;
        ZSet previous;
        ZSet total;
        Builder<Relation> result;
        Delta delta;
        explicit Variable(std::span<const ColumnLayout> columns);
    };

    size_t m_scope;
    std::vector<Index<Query<>>> m_bodies;
    std::vector<QueryView<>> m_imports;
    std::vector<ZSet> m_import_changes;
    std::vector<Node> m_nodes;
    std::vector<Variable> m_relations;
    ZSet m_empty;

    void step(Node& node, Iteration iteration);
    [[nodiscard]] bool pending_after(Iteration iteration) const noexcept;
    void run();
};
}  // namespace ygg::database::incremental

#endif
