/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SEMANTICS_BOUND_QUERY_EVALUATION_HPP_
#define YGG_DATABASE_SEMANTICS_BOUND_QUERY_EVALUATION_HPP_

#include "yggdrasil/containers/unordered_multi_map.hpp"
#include "yggdrasil/database/semantics/bound/conjunctive_plan.hpp"
#include "yggdrasil/database/semantics/bound/union_normal_form.hpp"
#include "yggdrasil/database/semantics/details/join_index.hpp"
#include "yggdrasil/database/semantics/relation_pool.hpp"
#include "yggdrasil/database/semantics/operations.hpp"
#include "yggdrasil/database/semantics/relation_view.hpp"

#include <algorithm>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace ygg::database::bound
{

/// Evaluation of a query with some of its output columns bound to given values, over owned snapshots of its inputs.
///
/// The query is compiled into union normal form (union_normal_form.hpp). Each set of bound columns gets one
/// ConjunctivePlan per branch (conjunctive_plan.hpp), evaluated by index nested-loop joins over a hash index of each
/// input on the columns bound at its step; a negation rejects a row if one of its branches matches. replace() keeps the
/// indexes of a changed input current row by row, as the incremental join's indexes are (incremental/join.hpp).
///
/// Inputs are borrowed during bind() only. incremental::bound::QueryEvaluator wraps one to keep it bound across input
/// changes, and runs its plans with variables bound by changed rows (plans(), bind_atom(), run_plan()).
template<ColumnTypes Values>
class QueryEvaluator
{
public:
    /// `static_slots` marks inputs that never change after the first bind().
    explicit QueryEvaluator(QueryView<Values> query, const std::vector<bool>& static_slots = {}) :
        m_query(union_normal_form(query)),
        m_columns(query.columns().begin(), query.columns().end())
    {
        m_frame.resize(m_query.frame_size);
        m_static.assign(input_count(), false);
        for (size_t slot = 0; slot < std::min(static_slots.size(), m_static.size()); ++slot)
            m_static[slot] = static_slots[slot];
        for (const auto& columns : m_query.input_columns)
            m_inputs.push_back(m_pool.get_or_allocate(std::span<const ColumnLayout>(columns)));
        m_bound.assign(input_count(), false);
    }

    size_t input_count() const noexcept { return m_query.input_columns.size(); }
    std::span<const ColumnLayout> columns() const noexcept { return m_columns; }
    const Builder<Relation<Values>>& input(size_t slot) const { return *m_inputs.at(slot); }
    bool bound() const noexcept { return std::ranges::find(m_bound, false) == m_bound.end(); }
    /// Increases whenever an input changes.
    uint64_t version() const noexcept { return m_version; }

    bool is_bound(size_t slot) const noexcept { return m_bound[slot]; }
    bool is_static(size_t slot) const noexcept { return m_static[slot]; }

    /// Binds the current inputs: input(slot) returns slot's relation view. Static slots are read on the first bind
    /// only.
    template<SlotRelations<Values> Input>
    void bind(const Input& input)
    {
        for (size_t slot = 0; slot < input_count(); ++slot)
            if (!m_bound[slot] || !m_static[slot])
                bind_slot(slot, input(slot));
    }

    /// Replaces the rows of one input slot.
    template<RelationViewConcept<Values> V>
    void bind_slot(size_t slot, const V& rows)
    {
        database::detail::require_plan_columns(rows.columns().span(), m_query.input_columns[slot]);
        auto& snapshot = *m_inputs[slot];
        snapshot.clear();
        for (size_t r = 0; r < rows.size(); ++r)
            snapshot.insert(rows.row(r));
        m_bound[slot] = true;
        invalidate(slot);
    }

    /// Applies a change to a bound input slot: `removed` rows are in it, `added` rows are not. Built indexes of the slot
    /// are updated in place. Every row is checked before any is applied, so a change that throws leaves the slot, its
    /// indexes and version() unchanged; an empty change does not increase version().
    template<RelationViewConcept<Values> A, RelationViewConcept<Values> R>
    void replace(size_t slot, const A& added, const R& removed)
    {
        if (added.empty() && removed.empty())
            return;
        auto& snapshot = *m_inputs[slot];
        const auto columns = std::span<const ColumnLayout>(m_query.input_columns[slot]);
        for (size_t r = 0; r < removed.size(); ++r)
            if (!snapshot.contains(Row<Values>(removed.row(r), columns)))
                throw std::invalid_argument("bound::QueryEvaluator: removed row is absent from the input.");
        for (size_t r = 0; r < added.size(); ++r)
            if (snapshot.contains(Row<Values>(added.row(r), columns)))
                throw std::invalid_argument("bound::QueryEvaluator: added row is already present in the input.");
        for (size_t r = 0; r < removed.size(); ++r)
        {
            const auto position = snapshot.find(Row<Values>(removed.row(r), columns));
            const auto last = to_uint_t(snapshot.size() - 1);
            for_each_index(slot, [&](KeyIndex& entry) { entry.rows.erase(database::detail::join_key_hash(snapshot.row(*position), entry.keys), *position); });
            snapshot.erase(*position);
            if (*position != last)
                for_each_index(slot,
                               [&](KeyIndex& entry) { entry.rows.replace(database::detail::join_key_hash(snapshot.row(*position), entry.keys), last, *position); });
        }
        for (size_t r = 0; r < added.size(); ++r)
        {
            const auto position = snapshot.insert(Row<Values>(added.row(r), columns));
            for_each_index(slot, [&](KeyIndex& entry) { entry.rows.insert(database::detail::join_key_hash(added.row(r), entry.keys), position); });
        }
        ++m_version;
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
    template<RowPredicate OnRow>
    void evaluate(size_t binding, std::span<const std::byte> values, const OnRow& on_row)
    {
        auto& prepared = m_bindings.at(binding);
        if (prepared.plans.empty() && !m_query.branches.empty())
            prepared.plans = plans(bound_variables(prepared.positions));
        for (const auto& plan : prepared.plans)
        {
            auto offset = size_t(0);
            for (const auto position : prepared.positions)
            {
                const auto size = m_columns[position].size;
                std::ranges::copy(values.subspan(offset, size), variable(m_query.output[position]).begin());
                offset += size;
            }
            if (!run_plan(plan, on_row))
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
            resolve(plan);
        }
        return compiled;
    }

    /// Binds the atom's variables to an input row; false if the row repeats a variable with different values.
    bool bind_atom(const ConjunctiveAtom& atom, std::span<const std::byte> row)
    {
        const auto& columns = m_query.input_columns[atom.slot];
        for (size_t c = 0; c < atom.vars.size(); ++c)
        {
            const auto bytes = database::detail::column_bytes(row, columns[c]);
            if (size_t(std::ranges::find(atom.vars, atom.vars[c]) - atom.vars.begin()) == c)
                std::ranges::copy(bytes, variable(atom.vars[c]).begin());
            else if (!std::ranges::equal(bytes, variable(atom.vars[c])))
                return false;
        }
        return true;
    }

    /// Runs a plan whose initially bound variables are set, calling on_row(row) for each output row; returns false
    /// once on_row returned false.
    template<RowPredicate OnRow>
    bool run_plan(const ConjunctivePlan& plan, const OnRow& on_row)
    {
        return !accepts(plan, plan.filters) || run(plan, 0, [&] { return on_row(output_row()); });
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
        std::vector<ColumnSlice> keys;           ///< The key columns in the slot's rows.
        UnorderedMultiMap<hash_t, uint_t> rows;  ///< Row positions by key hash, once built.
        bool built = false;
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
    uint64_t m_version = 0;

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

    /// The slot's rows were replaced: its indexes are rebuilt, in place, when next used.
    void invalidate(size_t slot)
    {
        for (auto& entry : m_indexes)
            if (entry.slot == slot)
                entry.built = false;
        ++m_version;
    }

    /// Calls f on each built index of the slot that has key columns.
    template<typename F>
        requires std::invocable<const F&, KeyIndex&>
    void for_each_index(size_t slot, const F& f)
    {
        for (auto& entry : m_indexes)
            if (entry.slot == slot && entry.built && !entry.positions.empty())
                f(entry);
    }

    // ---- Plans --------------------------------------------------------------------------------------------------

    size_t index_id(size_t slot, std::span<const std::pair<size_t, uint32_t>> key)
    {
        auto positions = std::vector<size_t>();
        for (const auto& [c, var] : key)
            positions.push_back(c);
        for (size_t id = 0; id < m_indexes.size(); ++id)
            if (m_indexes[id].slot == slot && m_indexes[id].positions == positions)
                return id;
        auto& entry = m_indexes.emplace_back();
        entry.slot = slot;
        for (const auto position : positions)
            entry.keys.push_back(database::detail::column_slice(m_query.input_columns[slot][position]));
        entry.positions = std::move(positions);
        return m_indexes.size() - 1;
    }

    /// Resolves each step's lookup structure, recursively into negations.
    void resolve(ConjunctivePlan& plan)
    {
        for (auto& step : plan.steps)
        {
            auto& lookup = m_lookups.emplace_back();
            lookup.index = index_id(step.slot, step.key);
            for (const auto& [c, var] : step.key)
            {
                const auto& variable = m_query.variables[var];
                lookup.probe.push_back(ColumnSlice { m_query.input_columns[step.slot][c].type, variable.offset, variable.size });
            }
            step.index = m_lookups.size() - 1;
        }
        for (auto& plans : plan.negation_plans)
            for (auto& inner : plans)
                resolve(inner);
    }

    // ---- Execution ----------------------------------------------------------------------------------------------

    bool accepts(const ConjunctivePlan& plan, const ConjunctiveFilters& filters)
    {
        for (const auto& [lhs, rhs] : filters.equal)
            if (!std::ranges::equal(variable(lhs), variable(rhs)))
                return false;
        for (const auto& [var, constant] : filters.value)
            if (!std::ranges::equal(variable(var), constant))
                return false;
        // A negation rejects the row if any of its branches matches (an anti-join, its outer variables bound).
        for (const auto i : filters.negations)
            for (const auto& inner : plan.negation_plans[i])
                if (accepts(inner, inner.filters) && !run(inner, 0, [] { return false; }))
                    return false;
        return true;
    }

    /// Depth-first over the plan's steps; returns false once `emit` asked to stop. An existence step stops at its first
    /// row that reaches the rest of the plan.
    template<typename Emit>
        requires std::predicate<const Emit&>
    bool run(const ConjunctivePlan& plan, size_t step_index, const Emit& emit)
    {
        if (step_index == plan.steps.size())
            return emit();
        const auto& step = plan.steps[step_index];
        assert(step.index != ConjunctiveStep::unresolved);
        const auto& rows = *m_inputs[step.slot];
        const auto& columns = m_query.input_columns[step.slot];
        auto proceed = true;  // False once emit asked to stop.
        const auto visit = [&](size_t r)
        {
            const auto row = rows.row(r);
            for (const auto& [c, var] : step.assign)
                std::ranges::copy(database::detail::column_bytes(row, columns[c]), variable(var).begin());
            for (const auto& [c, var] : step.compare)
                if (!std::ranges::equal(database::detail::column_bytes(row, columns[c]), variable(var)))
                    return true;
            if (!accepts(plan, step.filters))
                return true;
            proceed = run(plan, step_index + 1, emit);
            return proceed && !step.exists;
        };
        const auto& lookup = m_lookups[step.index];
        auto& entry = m_indexes[lookup.index];
        if (entry.positions.empty())
        {
            for (size_t r = 0; r < rows.size(); ++r)
                if (!visit(r))
                    return proceed;
            return true;
        }
        if (!entry.built)
        {
            entry.rows.clear();
            entry.rows.reserve(rows.size());
            for (size_t r = 0; r < rows.size(); ++r)
                entry.rows.insert(database::detail::join_key_hash(rows.row(r), entry.keys), to_uint_t(r));
            entry.built = true;
        }
        // The key's variables stay bound while this step iterates, so the frame is the probe.
        const auto probe = std::span<const std::byte>(m_frame);
        for (const auto r : entry.rows.values(database::detail::join_key_hash(probe, lookup.probe)))
            if (database::detail::join_keys_equal(rows.row(r), entry.keys, probe, lookup.probe) && !visit(r))
                return proceed;
        return true;
    }

};

}  // namespace ygg::database::bound

#endif
