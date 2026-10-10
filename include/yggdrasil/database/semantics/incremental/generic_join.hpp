/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SEMANTICS_INCREMENTAL_GENERIC_JOIN_HPP_
#define YGG_DATABASE_SEMANTICS_INCREMENTAL_GENERIC_JOIN_HPP_

#include "yggdrasil/database/semantics/generic_join.hpp"
#include "yggdrasil/database/semantics/incremental/delta.hpp"

namespace ygg::database::incremental
{
/// First-order join delta rules (Gupta et al., SIGMOD 1993, Section 4;
/// https://doi.org/10.1145/170035.170066), evaluated using Generic Join.
/// Owns a separate mutable trie for each input occurrence, including aliases.
/// All removals precede insertions, so full-join deltas are exact and disjoint.
/// This is incremental evaluation, not a dynamic worst-case optimality claim.
/// Invalid changes preserve state; mutation failures require initialize().
template<ColumnTypes Values = DefaultColumnTypes>
class GenericJoinEvaluator
{
    GenericJoinPlan<Values> m_plan;
    GenericJoinWorkspace<Values> m_workspace;
    database::detail::GenericJoinTrie m_changed;
    Builder<Relation<Values>> m_result;
    Delta<Values> m_delta;
    bool m_initialized = false;

    void require_initialized() const
    {
        if (!m_initialized)
            throw std::logic_error("Incremental generic join: initialize before use.");
    }

    template<RelationViewConcept<Values> V>
    void require_unaliased(const V& input) const
    {
        const auto* address = input.get_storage_address();
        if (address == m_result.get_storage_address() || address == m_delta.added.get_storage_address() || address == m_delta.removed.get_storage_address())
            throw std::invalid_argument("Incremental generic join: input aliases result or delta.");
    }

public:
    explicit GenericJoinEvaluator(GenericJoinPlan<Values> plan) :
        m_plan(std::move(plan)),
        m_workspace(m_plan),
        m_result(m_plan.output_columns().span()),
        m_delta(m_plan.output_columns().span())
    {
    }

    template<RelationViewRange<Values> R>
    void initialize(const R& inputs)
    {
        if (std::ranges::size(inputs) != m_plan.input_count())
            throw std::invalid_argument("Incremental generic join: wrong input count.");
        for (size_t input = 0; input < m_plan.input_count(); ++input)
        {
            database::detail::require_plan_columns(inputs[input].columns().span(), m_plan.input_columns(input));
            require_unaliased(inputs[input]);
        }
        m_initialized = false;
        generic_join(inputs, m_plan, m_result, m_workspace);
        m_changed.clear();
        m_delta.clear();
        m_initialized = true;
    }

    /// Actual changes per input occurrence; swap added/removed in every slot to undo.
    /// Borrowed inputs must not alias this evaluator's result or previous delta.
    template<RelationDeltaRange<Values> R>
    void update(const R& changes)
    {
        require_initialized();
        if (std::ranges::size(changes) != m_plan.input_count())
            throw std::invalid_argument("Incremental generic join: wrong input count.");
        for (size_t input = 0; input < m_plan.input_count(); ++input)
        {
            const auto& [added, removed] = changes[input];
            detail::require_delta<Values>(added, removed, m_plan.input_columns(input));
            require_unaliased(added);
            require_unaliased(removed);
            const auto keys = m_plan.input_keys(input);
            const auto& current = m_workspace.trie(input);
            const auto require_actual = [&](const auto& changed, bool removing)
            {
                for (size_t row = 0; row < changed.size(); ++row)
                    if (current.contains(changed.row(row), keys) != removing)
                        throw std::invalid_argument("Incremental generic join: change does not describe an actual input set change.");
            };
            require_actual(removed, true);
            require_actual(added, false);
        }
        m_initialized = false;
        m_delta.clear();
        // Added and removed views may have different types, so never select one with ?:.
        const auto apply = [&](size_t input, const auto& changed, bool removing)
        {
            if (changed.empty())
                return;
            const auto keys = m_plan.input_keys(input);
            auto& current = m_workspace.trie(input);
            m_changed.clear();
            for (size_t row = 0; row < changed.size(); ++row)
                m_changed.insert(changed.row(row), keys);
            m_workspace.append(removing ? m_delta.removed : m_delta.added, input, &m_changed);
            for (size_t row = 0; row < changed.size(); ++row)
                if (removing)
                    current.erase(changed.row(row), keys);
                else
                    current.insert(changed.row(row), keys);
        };
        for (size_t input = 0; input < m_plan.input_count(); ++input)
            apply(input, std::get<1>(changes[input]), true);
        for (size_t input = 0; input < m_plan.input_count(); ++input)
            apply(input, std::get<0>(changes[input]), false);
        for (size_t row = 0; row < m_delta.removed.size(); ++row)
        {
            const auto position = m_result.find(Row<Values>(m_delta.removed.row(row), m_plan.output_columns().span()));
            assert(position);
            m_result.erase(*position);
        }
        for (size_t row = 0; row < m_delta.added.size(); ++row)
            m_result.insert(Row<Values>(m_delta.added.row(row), m_plan.output_columns().span()));
        m_initialized = true;
    }

    const Builder<Relation<Values>>& get_result() const&
    {
        require_initialized();
        return m_result;
    }
    const Builder<Relation<Values>>& get_result() const&& = delete;
    const Delta<Values>& get_delta() const&
    {
        require_initialized();
        return m_delta;
    }
    const Delta<Values>& get_delta() const&& = delete;
    size_t memory_usage() const noexcept { return m_workspace.memory_usage() + m_changed.memory_usage() + m_result.memory_usage() + m_delta.memory_usage(); }
};
}  // namespace ygg::database::incremental

#endif
