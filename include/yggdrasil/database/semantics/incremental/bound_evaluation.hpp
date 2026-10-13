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
#include <stdexcept>
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

/// A BoundQueryEvaluator kept across input changes. bind() diffs the inputs against the bound snapshots, or apply()
/// takes a caller's exact change, and seed() finds the rows a change can add by delta rules for unions of conjunctive
/// queries (Gupta & Mumick, "Maintenance of Materialized Views: Problems, Techniques, and Applications", IEEE Data Eng.
/// Bull. 1995): a branch is evaluated with the changed atom's variables bound to each added row of a positive
/// occurrence, or to each removed row of a negated one. The result is a superset of the added rows, each of which
/// exists in the new state.
///
/// version() increases only on changes that can remove output rows: removed rows of an input that occurs positively, or
/// added rows of one that occurs negated (under an odd number of negations). Unions of conjunctive queries with
/// negation are monotone in their positive atoms and antitone in their negated ones (Abiteboul, Hull & Vianu,
/// "Foundations of Databases", 1995), so other changes keep every output row, and checks of earlier rows stay valid.
template<ColumnTypes Values>
class BoundQueryEvaluator
{
public:
    explicit BoundQueryEvaluator(QueryView<Values> query, const std::vector<bool>& static_slots = {}) : m_base(query, static_slots)
    {
        for (size_t slot = 0; slot < input_count(); ++slot)
        {
            const auto columns = std::span<const ColumnLayout>(m_base.query().input_columns[slot]);
            m_changes.push_back(InputChange<Values> { slot, Builder<Relation<Values>>(columns), Builder<Relation<Values>>(columns) });
        }
        m_changed.reserve(input_count());
        m_positive.assign(input_count(), false);
        m_negative.assign(input_count(), false);
        const auto mark = [&](auto&& self, const ConjunctiveBranch& branch, bool negated) -> void
        {
            for (const auto& atom : branch.atoms)
                (negated ? m_negative : m_positive)[atom.slot] = true;
            for (const auto& negation : branch.negations)
                for (const auto& inner : negation.branches)
                    self(self, inner, !negated);
        };
        for (const auto& branch : m_base.query().branches)
            mark(mark, branch, false);
    }

    size_t input_count() const noexcept { return m_base.input_count(); }
    std::span<const ColumnLayout> columns() const noexcept { return m_base.columns(); }
    const Builder<Relation<Values>>& input(size_t slot) const { return m_base.input(slot); }
    bool bound() const noexcept { return m_base.bound(); }
    bool is_static(size_t slot) const noexcept { return m_base.is_static(slot); }
    /// Increases whenever a change can remove output rows (see the class comment).
    uint32_t version() const noexcept { return m_version; }
    size_t prepare(std::vector<size_t> positions) { return m_base.prepare(std::move(positions)); }
    template<typename OnRow>
    void evaluate(size_t binding, std::span<const std::byte> values, const OnRow& on_row)
    {
        m_base.evaluate(binding, values, on_row);
    }

    /// Binds the current inputs: input(slot) returns slot's relation view. Static slots are read on the first bind
    /// only; after it, slots not marked in `maybe_changed` are assumed unchanged and are not read either. Returns the
    /// slots bound before that changed, whose changes change(slot) holds; both are valid until the next bind().
    template<typename Input>
        requires std::invocable<const Input&, size_t> && RelationViewConcept<std::invoke_result_t<const Input&, size_t>, Values>
    std::span<const size_t> bind(const Input& input, const std::vector<bool>& maybe_changed = {})
    {
        m_changed.clear();
        for (size_t slot = 0; slot < input_count(); ++slot)
        {
            if (!m_base.is_bound(slot))
            {
                m_base.bind_slot(slot, input(slot));
                continue;
            }
            if (m_base.is_static(slot) || (!maybe_changed.empty() && (slot >= maybe_changed.size() || !maybe_changed[slot])))
                continue;
            auto& change = m_changes[slot];
            diff(input(slot), change);
            if (change.added.empty() && change.removed.empty())
                continue;
            apply(slot, change.added, change.removed);
            m_changed.push_back(slot);
        }
        return m_changed;
    }
    /// The change of a slot that the last bind() returned.
    const InputChange<Values>& change(size_t slot) const { return m_changes.at(slot); }

    /// Applies an exact change to a bound, non-static slot: `removed` rows are in it, `added` rows are not.
    template<RelationViewConcept<Values> A, RelationViewConcept<Values> R>
    void apply(size_t slot, const A& added, const R& removed)
    {
        database::detail::require_plan_columns(added.columns().span(), m_base.query().input_columns[slot]);
        database::detail::require_plan_columns(removed.columns().span(), m_base.query().input_columns[slot]);
        if (!m_base.is_bound(slot) || m_base.is_static(slot))
            throw std::invalid_argument("BoundQueryEvaluator: changes apply to bound, non-static slots only.");
        m_base.replace(slot, added, removed);
        if ((m_positive[slot] && !removed.empty()) || (m_negative[slot] && !added.empty()))
            ++m_version;
    }

    /// Calls on_row(row) for every output row the slot's change (applied before) can have added, with repetitions and
    /// rows that existed before. Returns false if a changed row binds variables outside its branch's atoms, which delta
    /// rules cannot express; then the added rows are unknown.
    template<RelationViewConcept<Values> A, RelationViewConcept<Values> R, typename OnRow>
    bool seed(size_t slot, const A& added, const R& removed, const OnRow& on_row)
    {
        for (size_t b = 0; b < m_base.query().branches.size(); ++b)
            if (!seed_branch(b, slot, added, removed, on_row))
                return false;
        return true;
    }

private:
    database::BoundQueryEvaluator<Values> m_base;
    UnorderedMap<std::vector<bool>, std::vector<ConjunctivePlan>> m_seed_plans;  ///< By the variables bound on arrival.
    std::vector<InputChange<Values>> m_changes;  ///< By slot; reused across bind() calls.
    std::vector<size_t> m_changed;               ///< The slots the last bind() changed.
    std::vector<bool> m_positive;                ///< By slot: whether it occurs under an even number of negations.
    std::vector<bool> m_negative;                ///< By slot: whether it occurs under an odd number of negations.
    uint32_t m_version = 0;
    std::vector<bool> m_bound;                   ///< Scratch of seed_plan() and seed_branch(), kept to avoid allocations.
    std::vector<bool> m_top_vars;

    /// Replaces `change` by the difference of `rows` from the slot's snapshot.
    template<typename V>
    void diff(const V& rows, InputChange<Values>& change) const
    {
        database::detail::require_plan_columns(rows.columns().span(), change.added.columns().span());
        change.added.clear();
        change.removed.clear();
        const auto& snapshot = m_base.input(change.slot);
        for (size_t r = 0; r < rows.size(); ++r)
            if (!snapshot.contains(rows.row(r)))
                change.added.insert(rows.row(r));
        for (size_t r = 0; r < snapshot.size(); ++r)
            if (!rows.contains(snapshot.row(r)))
                change.removed.insert(snapshot.row(r));
    }

    /// The plan of a branch with the atom's variables bound; valid until the next call.
    const ConjunctivePlan& seed_plan(size_t branch, const ConjunctiveAtom& atom)
    {
        m_bound.assign(m_base.query().variables.size(), false);
        for (const auto var : atom.vars)
            m_bound[var] = true;
        auto found = m_seed_plans.find(m_bound);
        if (found == m_seed_plans.end())
            found = m_seed_plans.emplace(m_bound, m_base.plans(m_bound)).first;
        return found->second[branch];
    }

    template<typename A, typename R, typename OnRow>
    bool seed_branch(size_t b, size_t slot, const A& added, const R& removed, const OnRow& on_row)
    {
        const auto& query = m_base.query();
        const auto& top = query.branches[b];
        auto& top_vars = m_top_vars;
        top_vars.assign(query.variables.size(), false);
        for (const auto& atom : top.atoms)
            for (const auto var : atom.vars)
                top_vars[var] = true;
        const auto seed_atom = [&](const ConjunctiveAtom& atom, const auto& rows) -> bool
        {
            if (atom.slot != slot || rows.empty())
                return true;
            if (!std::ranges::all_of(atom.vars, [&](uint32_t var) { return bool(top_vars[var]); }))
                return false;
            const auto& plan = seed_plan(b, atom);
            for (size_t r = 0; r < rows.size(); ++r)
                if (m_base.bind_atom(atom, rows.row(r)))
                    m_base.run_plan(b, plan, on_row);
            return true;
        };
        const auto walk = [&](auto&& self, const ConjunctiveBranch& branch, bool negated) -> bool
        {
            for (const auto& atom : branch.atoms)
                if (!(negated ? seed_atom(atom, removed) : seed_atom(atom, added)))
                    return false;
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
