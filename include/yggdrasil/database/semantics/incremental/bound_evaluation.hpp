/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SEMANTICS_INCREMENTAL_BOUND_EVALUATION_HPP_
#define YGG_DATABASE_SEMANTICS_INCREMENTAL_BOUND_EVALUATION_HPP_

#include "yggdrasil/containers/associative_containers.hpp"
#include "yggdrasil/database/semantics/bound_evaluation.hpp"

#include <algorithm>
#include <cstddef>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace ygg::database::incremental
{

/// The rows bind() added to and removed from an input slot.
template<ColumnTypes Values>
struct InputChange
{
    size_t slot = 0;
    Builder<Relation<Values>> added;
    Builder<Relation<Values>> removed;
};

/// A BoundQueryEvaluator kept across input changes. bind() diffs the inputs against the bound snapshots, and seed()
/// finds the rows a change can add by delta rules for unions of conjunctive queries (Gupta & Mumick, "Maintenance of
/// Materialized Views: Problems, Techniques, and Applications", IEEE Data Eng. Bull. 1995): a branch is evaluated
/// with the changed atom's variables bound to each added row of a positive occurrence, or to each removed row of a
/// negated one. The result is a superset of the added rows, each of which exists in the new state.
template<ColumnTypes Values>
class BoundQueryEvaluator
{
public:
    explicit BoundQueryEvaluator(QueryView<Values> query, const std::vector<bool>& static_slots = {}) : m_base(query, static_slots) {}

    size_t input_count() const noexcept { return m_base.input_count(); }
    std::span<const ColumnLayout> columns() const noexcept { return m_base.columns(); }
    const Builder<Relation<Values>>& input(size_t slot) const { return m_base.input(slot); }
    bool bound() const noexcept { return m_base.bound(); }
    uint32_t version() const noexcept { return m_base.version(); }
    size_t prepare(std::vector<size_t> positions) { return m_base.prepare(std::move(positions)); }
    template<typename OnRow>
    void evaluate(size_t binding, std::span<const std::byte> values, OnRow&& on_row)
    {
        m_base.evaluate(binding, values, on_row);
    }

    /// Binds the current inputs: input(slot) returns slot's relation view. Static slots are read on the first bind
    /// only; after it, slots not marked in `maybe_changed` are assumed unchanged and are not read either. Returns the
    /// changes of slots bound before.
    template<typename Input>
        requires std::invocable<Input&, size_t> && RelationViewConcept<std::invoke_result_t<Input&, size_t>, Values>
    std::vector<InputChange<Values>> bind(Input&& input, const std::vector<bool>& maybe_changed = {})
    {
        auto changes = std::vector<InputChange<Values>>();
        for (size_t slot = 0; slot < input_count(); ++slot)
        {
            if (!m_base.is_bound(slot))
            {
                m_base.bind_slot(slot, input(slot));
                continue;
            }
            if (m_base.is_static(slot) || (!maybe_changed.empty() && (slot >= maybe_changed.size() || !maybe_changed[slot])))
                continue;
            auto change = diff(slot, input(slot));
            if (change.added.empty() && change.removed.empty())
                continue;
            m_base.replace(slot, change.added, change.removed);
            changes.push_back(std::move(change));
        }
        return changes;
    }

    /// Calls on_row(row) for every output row the change can have added (with repetitions and rows that existed
    /// before). Returns false if a changed row binds variables outside its branch's atoms, which delta rules cannot
    /// express; then the added rows are unknown.
    template<typename OnRow>
    bool seed(const InputChange<Values>& change, OnRow&& on_row)
    {
        for (size_t b = 0; b < m_base.query().branches.size(); ++b)
            if (!seed_branch(b, change, on_row))
                return false;
        return true;
    }

private:
    database::BoundQueryEvaluator<Values> m_base;
    UnorderedMap<std::vector<bool>, std::vector<ConjunctivePlan>> m_seed_plans;  ///< By the variables bound on arrival.

    template<typename V>
    InputChange<Values> diff(size_t slot, const V& rows) const
    {
        const auto columns = std::span<const ColumnLayout>(m_base.query().input_columns[slot]);
        database::detail::require_plan_columns(rows.columns().span(), columns);
        auto change = InputChange<Values> { slot, Builder<Relation<Values>>(columns), Builder<Relation<Values>>(columns) };
        const auto& snapshot = m_base.input(slot);
        for (size_t r = 0; r < rows.size(); ++r)
            if (!snapshot.contains(rows.row(r)))
                change.added.insert(rows.row(r));
        for (size_t r = 0; r < snapshot.size(); ++r)
            if (!rows.contains(snapshot.row(r)))
                change.removed.insert(snapshot.row(r));
        return change;
    }

    /// The plan of a branch with the atom's variables bound; valid until the next call.
    const ConjunctivePlan& seed_plan(size_t branch, const ConjunctiveAtom& atom)
    {
        auto bound = std::vector<bool>(m_base.query().variables.size(), false);
        for (const auto var : atom.vars)
            bound[var] = true;
        auto found = m_seed_plans.find(bound);
        if (found == m_seed_plans.end())
            found = m_seed_plans.emplace(bound, m_base.plans(bound)).first;
        return found->second[branch];
    }

    template<typename OnRow>
    bool seed_branch(size_t b, const InputChange<Values>& change, OnRow& on_row)
    {
        const auto& query = m_base.query();
        const auto& top = query.branches[b];
        auto top_vars = std::vector<bool>(query.variables.size(), false);
        for (const auto& atom : top.atoms)
            for (const auto var : atom.vars)
                top_vars[var] = true;
        const auto walk = [&](auto&& self, const ConjunctiveBranch& branch, bool negated) -> bool
        {
            const auto& rows = negated ? change.removed : change.added;
            for (const auto& atom : branch.atoms)
            {
                if (atom.slot != change.slot || rows.empty())
                    continue;
                if (!std::ranges::all_of(atom.vars, [&](uint32_t var) { return bool(top_vars[var]); }))
                    return false;
                const auto& plan = seed_plan(b, atom);
                for (size_t r = 0; r < rows.size(); ++r)
                    if (m_base.bind_atom(atom, rows.row(r)))
                        m_base.run_plan(b, plan, on_row);
            }
            for (const auto& negation : branch.negations)
                for (const auto& inner : negation.branches)
                    if (!self(self, inner, !negated))
                        return false;
            return true;
        };
        return walk(walk, top, false);
    }
};

}  // namespace ygg::database::incremental

#endif
