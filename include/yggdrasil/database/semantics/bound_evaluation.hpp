/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SEMANTICS_BOUND_EVALUATION_HPP_
#define YGG_DATABASE_SEMANTICS_BOUND_EVALUATION_HPP_

#include "yggdrasil/database/semantics/conjunctive_plan.hpp"
#include "yggdrasil/database/semantics/join_index.hpp"
#include "yggdrasil/database/semantics/relation_pool.hpp"
#include "yggdrasil/database/semantics/operations.hpp"
#include "yggdrasil/database/semantics/relation_view.hpp"
#include "yggdrasil/database/syntax/translations/union_normal_form.hpp"

#include <algorithm>
#include <cstddef>
#include <deque>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace ygg::database
{

/// Evaluation of a query with some of its output columns bound to given values, over owned snapshots of its inputs.
///
/// The query is compiled into union normal form (union_normal_form.hpp). Each set of bound columns gets one
/// ConjunctivePlan per branch (conjunctive_plan.hpp), evaluated by index nested-loop joins over a JoinIndex of each
/// input on the columns bound at its step; a negation rejects a row if one of its branches matches.
///
/// Inputs are borrowed during bind() only. incremental::BoundQueryEvaluator keeps it across input changes and runs its
/// plans with variables bound by changed rows (plans(), bind_atom(), run_plan()).
template<ColumnTypes Values>
class BoundQueryEvaluator
{
public:
    /// `static_slots` marks inputs that never change after the first bind().
    explicit BoundQueryEvaluator(QueryView<Values> query, const std::vector<bool>& static_slots = {}) :
        m_query(union_normal_form(query)),
        m_columns(query.columns().begin(), query.columns().end())
    {
        m_frame.resize(m_query.frame_size);
        m_static.assign(input_count(), false);
        for (size_t slot = 0; slot < std::min(static_slots.size(), m_static.size()); ++slot)
            m_static[slot] = static_slots[slot];
        // Pooled snapshots have the storage identity a JoinIndex requires.
        for (const auto& columns : m_query.input_columns)
            m_inputs.push_back(m_pool.get_or_allocate(std::span<const ColumnLayout>(columns)));
        m_bound.assign(input_count(), false);
    }

    size_t input_count() const noexcept { return m_query.input_columns.size(); }
    std::span<const ColumnLayout> columns() const noexcept { return m_columns; }
    const Builder<Relation<Values>>& input(size_t slot) const { return *m_inputs.at(slot); }
    bool bound() const noexcept { return std::ranges::find(m_bound, false) == m_bound.end(); }
    /// Increases whenever an input changes.
    uint32_t version() const noexcept { return m_version; }

    bool is_bound(size_t slot) const noexcept { return m_bound[slot]; }
    bool is_static(size_t slot) const noexcept { return m_static[slot]; }

    /// Binds the current inputs: input(slot) returns slot's relation view. Static slots are read on the first bind
    /// only.
    template<typename Input>
        requires std::invocable<Input&, size_t> && RelationViewConcept<std::invoke_result_t<Input&, size_t>, Values>
    void bind(Input&& input)
    {
        for (size_t slot = 0; slot < input_count(); ++slot)
            if (!m_bound[slot] || !m_static[slot])
                bind_slot(slot, input(slot));
    }

    /// Replaces the rows of one input slot.
    template<RelationViewConcept<Values> V>
    void bind_slot(size_t slot, const V& rows)
    {
        detail::require_plan_columns(rows.columns().span(), m_query.input_columns[slot]);
        auto& snapshot = *m_inputs[slot];
        snapshot.clear();
        for (size_t r = 0; r < rows.size(); ++r)
            snapshot.insert(rows.row(r));
        m_bound[slot] = true;
        invalidate(slot);
    }

    /// Applies a change to a bound input slot: `removed` rows are in it, `added` rows are not.
    void replace(size_t slot, const Builder<Relation<Values>>& added, const Builder<Relation<Values>>& removed)
    {
        auto& snapshot = *m_inputs[slot];
        for (size_t r = 0; r < removed.size(); ++r)
            snapshot.erase(*snapshot.find(removed.row(r)));
        for (size_t r = 0; r < added.size(); ++r)
            snapshot.insert(added.row(r));
        invalidate(slot);
    }

    /// Evaluation with the output columns at `positions` bound, in that order; returns its handle. The plans are
    /// compiled at the first evaluation, when the inputs' sizes are known.
    size_t prepare(std::vector<size_t> positions)
    {
        for (size_t binding = 0; binding < m_bindings.size(); ++binding)
            if (m_bindings[binding].positions == positions)
                return binding;
        m_bindings.push_back(Binding { std::move(positions), {} });
        return m_bindings.size() - 1;
    }

    /// Calls on_row(row) for each output row whose prepared columns hold `values` (their bytes, concatenated in the
    /// prepared order); stops when on_row returns false.
    template<typename OnRow>
    void evaluate(size_t binding, std::span<const std::byte> values, OnRow&& on_row)
    {
        auto& prepared = m_bindings.at(binding);
        if (prepared.plans.empty() && !m_query.branches.empty())
            prepared.plans = plans(bound_variables(prepared.positions));
        for (size_t b = 0; b < m_query.branches.size(); ++b)
        {
            auto offset = size_t(0);
            for (const auto position : prepared.positions)
            {
                const auto size = m_columns[position].size;
                std::ranges::copy(values.subspan(offset, size), variable(m_query.output[position]).begin());
                offset += size;
            }
            if (!run_plan(b, prepared.plans[b], on_row))
                return;
        }
    }

    const UnionNormalForm& query() const noexcept { return m_query; }

    /// One plan per branch, with the variables marked in `bound` bound on arrival, for the current inputs' sizes.
    std::vector<ConjunctivePlan> plans(const std::vector<bool>& bound)
    {
        auto measured = std::vector<std::optional<RelationStatistics>>(input_count());
        const auto statistics = [&](size_t slot) -> const RelationStatistics&
        {
            if (!measured[slot])
                measured[slot] = measure_relation<Values>(*m_inputs[slot]);
            return *measured[slot];
        };
        auto compiled = std::vector<ConjunctivePlan>();
        for (const auto& branch : m_query.branches)
        {
            auto& plan = compiled.emplace_back(plan_branch(m_query, branch, bound, statistics));
            resolve(branch, plan);
        }
        return compiled;
    }

    /// Binds the atom's variables to an input row; false if the row repeats a variable with different values.
    bool bind_atom(const ConjunctiveAtom& atom, std::span<const std::byte> row)
    {
        const auto& columns = m_query.input_columns[atom.slot];
        for (size_t c = 0; c < atom.vars.size(); ++c)
        {
            const auto bytes = detail::column_bytes(row, columns[c]);
            if (size_t(std::ranges::find(atom.vars, atom.vars[c]) - atom.vars.begin()) == c)
                std::ranges::copy(bytes, variable(atom.vars[c]).begin());
            else if (!std::ranges::equal(bytes, variable(atom.vars[c])))
                return false;
        }
        return true;
    }

    /// Runs a plan of branch `b` whose initially bound variables are set, calling on_row(row) for each output row;
    /// returns false once on_row returned false.
    template<typename OnRow>
    bool run_plan(size_t b, const ConjunctivePlan& plan, OnRow&& on_row)
    {
        const auto& branch = m_query.branches[b];
        return !accepts(branch, plan, plan.filters) || run(branch, plan, 0, [&] { return on_row(output_row()); });
    }

private:

    struct Binding
    {
        std::vector<size_t> positions;
        std::vector<ConjunctivePlan> plans;
    };
    /// The rows of an input slot keyed by some of its columns; with no key columns, a scan.
    struct KeyIndex
    {
        size_t slot = 0;
        std::vector<size_t> positions;
        std::vector<ColumnSlice> keys;  ///< The key columns in the slot's rows.
        std::optional<JoinIndex<Values>> index;
    };
    /// A step's lookup: its index and where the key's variables are in the frame.
    struct Lookup
    {
        size_t index = 0;
        std::vector<ColumnSlice> probe;
    };

    UnionNormalForm m_query;
    std::vector<ColumnLayout> m_columns;
    RelationPool<Values> m_pool;  ///< Owns the input snapshots; declared before them, so it outlives them.
    std::vector<std::byte> m_frame;                ///< The variables' values while a branch runs.
    std::vector<bool> m_static;
    std::vector<UniqueObjectPoolPtr<Builder<Relation<Values>>>> m_inputs;
    std::vector<bool> m_bound;
    std::deque<KeyIndex> m_indexes;
    std::deque<Lookup> m_lookups;  ///< By ConjunctiveStep::index.
    std::vector<Binding> m_bindings;
    std::vector<std::byte> m_row;
    uint32_t m_version = 0;

    std::span<std::byte> variable(uint32_t var) noexcept { return std::span(m_frame).subspan(m_query.variables[var].offset, m_query.variables[var].size); }

    std::span<const std::byte> output_row()
    {
        m_row.clear();
        for (const auto var : m_query.output)
        {
            const auto bytes = variable(var);
            m_row.insert(m_row.end(), bytes.begin(), bytes.end());
        }
        return m_row;
    }

    std::vector<bool> bound_variables(std::span<const size_t> positions) const
    {
        auto bound = std::vector<bool>(m_query.variables.size(), false);
        for (const auto position : positions)
            bound[m_query.output[position]] = true;
        return bound;
    }

    void invalidate(size_t slot)
    {
        for (auto& entry : m_indexes)
            if (entry.slot == slot)
                entry.index.reset();
        ++m_version;
    }

    // ---- Plans --------------------------------------------------------------------------------------------------

    size_t index_id(size_t slot, const std::vector<size_t>& positions)
    {
        for (size_t id = 0; id < m_indexes.size(); ++id)
            if (m_indexes[id].slot == slot && m_indexes[id].positions == positions)
                return id;
        auto& entry = m_indexes.emplace_back();
        entry.slot = slot;
        entry.positions = positions;
        for (const auto position : positions)
            entry.keys.push_back(detail::column_slice(m_query.input_columns[slot][position]));
        return m_indexes.size() - 1;
    }

    /// Resolves each step's lookup structure, recursively into negations.
    void resolve(const ConjunctiveBranch& branch, ConjunctivePlan& plan)
    {
        for (auto& step : plan.steps)
        {
            const auto& atom = branch.atoms[step.atom];
            auto& lookup = m_lookups.emplace_back();
            lookup.index = index_id(atom.slot, step.positions);
            for (const auto position : step.positions)
            {
                const auto& variable = m_query.variables[atom.vars[position]];
                lookup.probe.push_back(ColumnSlice { m_query.input_columns[atom.slot][position].type, variable.offset, variable.size });
            }
            step.index = m_lookups.size() - 1;
        }
        for (size_t i = 0; i < plan.negation_plans.size(); ++i)
            for (size_t b = 0; b < plan.negation_plans[i].size(); ++b)
                resolve(branch.negations[i].branches[b], plan.negation_plans[i][b]);
    }

    // ---- Execution ----------------------------------------------------------------------------------------------

    bool accepts(const ConjunctiveBranch& branch, const ConjunctivePlan& plan, const ConjunctiveFilters& filters)
    {
        for (const auto i : filters.equal)
            if (!std::ranges::equal(variable(branch.equal[i].first), variable(branch.equal[i].second)))
                return false;
        for (const auto i : filters.value)
            if (!std::ranges::equal(variable(branch.value[i].first), branch.value[i].second))
                return false;
        // A negation rejects the row if any of its branches matches (an anti-join, its outer variables bound).
        for (const auto i : filters.negations)
            for (size_t b = 0; b < branch.negations[i].branches.size(); ++b)
            {
                const auto& inner = branch.negations[i].branches[b];
                const auto& inner_plan = plan.negation_plans[i][b];
                if (accepts(inner, inner_plan, inner_plan.filters) && !run(inner, inner_plan, 0, [] { return false; }))
                    return false;
            }
        return true;
    }

    /// Depth-first over the plan's steps; returns false once `emit` asked to stop.
    template<typename Emit>
    bool run(const ConjunctiveBranch& branch, const ConjunctivePlan& plan, size_t step_index, Emit&& emit)
    {
        if (step_index == plan.steps.size())
            return emit();
        const auto& step = plan.steps[step_index];
        const auto& atom = branch.atoms[step.atom];
        const auto& rows = *m_inputs[atom.slot];
        const auto& columns = m_query.input_columns[atom.slot];
        const auto visit = [&](size_t r)
        {
            const auto row = rows.row(r);
            for (const auto& [c, var] : step.assign)
                std::ranges::copy(detail::column_bytes(row, columns[c]), variable(var).begin());
            for (const auto& [c, var] : step.compare)
                if (!std::ranges::equal(detail::column_bytes(row, columns[c]), variable(var)))
                    return true;
            return !accepts(branch, plan, step.filters) || run(branch, plan, step_index + 1, emit);
        };
        const auto& lookup = m_lookups[step.index];
        auto& entry = m_indexes[lookup.index];
        if (entry.positions.empty())
        {
            for (size_t r = 0; r < rows.size(); ++r)
                if (!visit(r))
                    return false;
            return true;
        }
        if (!entry.index)
            entry.index.emplace(rows, entry.keys);
        // The key's variables stay bound while this step iterates, so the frame is the probe.
        return entry.index->for_each_match(rows, std::span<const std::byte>(m_frame), lookup.probe, visit);
    }

};

}  // namespace ygg::database

#endif
