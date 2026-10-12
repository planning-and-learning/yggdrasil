/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SYNTAX_TRANSLATIONS_RELAXATION_HPP_
#define YGG_DATABASE_SYNTAX_TRANSLATIONS_RELAXATION_HPP_

#include "yggdrasil/database/syntax/copy.hpp"
#include "yggdrasil/database/syntax/operator_builder.hpp"
#include "yggdrasil/database/syntax/query.hpp"

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace ygg::database
{

/// Relaxations of queries over inputs of which only some are known.
template<ColumnTypes Values = DefaultColumnTypes>
struct Relaxation
{
    std::vector<std::optional<QueryView<Values>>> queries;  ///< Per root; nullopt if nothing is constrained.
    std::vector<size_t> slots;                              ///< The original input slot of each relaxed input, shared by all roots.
};

namespace detail
{

template<ColumnTypes Values, std::predicate<size_t> Known>
class Relaxer
{
    using Labels = std::vector<Index<Column>>;

public:
    Relaxer(QueryRepository<Values>& repository, QueryBuilder<Values>& builder, Known known) : m_build(repository, builder), m_known(std::move(known)) {}

    /// The relaxation of `query` restricted to the needed labels `needed` (see relax() below).
    std::optional<QueryView<Values>> relax(QueryView<Values> query, const Labels& needed)
    {
        auto key = std::pair { query.get_index().get_value(), std::vector<uint_t>() };
        for (const auto label : needed)
            key.second.push_back(label.get_value());
        std::ranges::sort(key.second);
        if (const auto it = m_relaxed.find(key); it != m_relaxed.end())
            return it->second;
        auto result = ygg::visit([&]<typename Tag>(QueryView<Values, Tag> concrete) { return relax(concrete, needed); }, query.get_variant());
        // Labels nobody needs are eliminated here, in the order of `needed`; with none needed, only the emptiness remains.
        if (result)
            result = m_build.project(*result, intersection(needed, labels(*result)));
        m_relaxed.emplace(std::move(key), result);
        return result;
    }

    std::vector<size_t> slots() && { return std::move(m_slots); }

private:
    OperatorBuilder<Values> m_build;
    Known m_known;
    std::vector<size_t> m_slots;
    std::map<size_t, size_t> m_renumbered;  ///< Original slot -> relaxed input.
    std::map<std::pair<uint_t, std::vector<uint_t>>, std::optional<QueryView<Values>>> m_relaxed;

    static Labels labels(QueryView<Values> query) { return column_labels(query.columns()); }
    static bool contains(const Labels& labels, Index<Column> label) { return std::ranges::find(labels, label) != labels.end(); }
    /// The labels of `lhs` also in `rhs`, in the order of `lhs`.
    static Labels intersection(const Labels& lhs, const Labels& rhs)
    {
        auto result = Labels();
        for (const auto label : lhs)
            if (contains(rhs, label))
                result.push_back(label);
        return result;
    }
    /// `lhs` followed by the labels of `rhs` not in it.
    static Labels united(Labels lhs, const Labels& rhs)
    {
        for (const auto label : rhs)
            if (!contains(lhs, label))
                lhs.push_back(label);
        return lhs;
    }
    static bool subset(const Labels& lhs, const Labels& rhs)
    {
        return std::ranges::all_of(lhs, [&](Index<Column> label) { return contains(rhs, label); });
    }

    /// The relaxed input of a known slot.
    size_t renumbered(size_t slot)
    {
        const auto [found, inserted] = m_renumbered.emplace(slot, m_slots.size());
        if (inserted)
            m_slots.push_back(slot);
        return found->second;
    }
    /// The query itself over the relaxed inputs, or nullopt if it reads an unknown input.
    std::optional<QueryView<Values>> copy(QueryView<Values> query)
    {
        if (!std::ranges::all_of(input_slots(std::span<const QueryView<Values>>(&query, 1)), m_known))
            return std::nullopt;
        return database::copy(query, [&](size_t slot) { return renumbered(slot); }, m_build.repository(), m_build.builder());
    }
    std::optional<QueryView<Values>> input(QueryView<Values, QueryInputTag> concrete)
    {
        const auto slot = concrete.get_input_slot();
        if (!m_known(slot))
            return std::nullopt;
        const auto columns = concrete.columns();
        return m_build.template make<QueryInputTag>(
            [&](auto& data)
            {
                data.input_slot = renumbered(slot);
                data.columns.set(columns.begin(), columns.end());
            });
    }
    /// A ⋈ B, where an unconstrained side drops out.
    std::optional<QueryView<Values>> join(std::optional<QueryView<Values>> lhs, std::optional<QueryView<Values>> rhs)
    {
        if (!lhs)
            return rhs;
        if (!rhs)
            return lhs;
        return m_build.join(*lhs, *rhs);
    }

    template<typename Tag>
    std::optional<QueryView<Values>> relax(QueryView<Values, Tag> concrete, const Labels& needed)
    {
        if constexpr (std::same_as<Tag, QueryInputTag>)
            return input(concrete);
        else if constexpr (std::same_as<Tag, QueryEmptyTag>)
            return m_build.empty(concrete.columns());
        else if constexpr (std::same_as<Tag, QueryJoinTag> || std::same_as<Tag, QueryGenericJoinTag>)
        {
            auto conjuncts = std::vector<QueryView<Values>>();
            if constexpr (std::same_as<Tag, QueryJoinTag>)
                conjuncts = { concrete.get_lhs(), concrete.get_rhs() };
            else
                for (const auto input : concrete.get_inputs())
                    conjuncts.emplace_back(input, concrete.get_repository());
            // A conjunct needs its labels that are needed above or shared with another conjunct (early elimination).
            auto result = std::optional<QueryView<Values>>();
            for (size_t k = 0; k < conjuncts.size(); ++k)
            {
                auto others = needed;
                for (size_t j = 0; j < conjuncts.size(); ++j)
                    if (j != k)
                        others = united(std::move(others), labels(conjuncts[j]));
                result = join(result, relax(conjuncts[k], intersection(labels(conjuncts[k]), others)));
            }
            return result;
        }
        else if constexpr (std::same_as<Tag, QueryProjectTag>)
            return relax(concrete.get_arg(), needed);
        else if constexpr (std::same_as<Tag, QueryRenameTag>)
        {
            const auto before = concrete.get_arg().columns();
            const auto after = concrete.columns();
            auto inner = Labels();
            for (const auto label : needed)
                inner.push_back(before[column_index(after, label)].label);
            const auto arg = relax(concrete.get_arg(), inner);
            if (!arg)
                return std::nullopt;
            // The relaxed columns keep their labels' positions in the argument's schema.
            auto renamed = Labels();
            for (const auto& column : arg->columns())
                renamed.push_back(after[column_index(before, column.label)].label);
            return m_build.template relabel<QueryRenameTag>(*arg, renamed);
        }
        else if constexpr (std::same_as<Tag, QuerySelectEqualTag> || std::same_as<Tag, QuerySelectValueTag>)
        {
            const auto& data = concrete.get_data();
            const auto selected = [&]
            {
                if constexpr (std::same_as<Tag, QuerySelectEqualTag>)
                    return Labels { data.lhs_column, data.rhs_column };
                else
                    return Labels { data.column };
            }();
            const auto arg = relax(concrete.get_arg(), united(needed, selected));
            if (!arg)
                return std::nullopt;
            // A selection on an unconstrained column is dropped.
            if (!subset(selected, labels(*arg)))
                return arg;
            return m_build.template make<Tag>(
                [&](auto& filtered)
                {
                    filtered = data;
                    filtered.arg = arg->get_index();
                });
        }
        else if constexpr (std::same_as<Tag, QueryUnionTag>)
        {
            const auto lhs = relax(concrete.get_lhs(), needed);
            const auto rhs = relax(concrete.get_rhs(), needed);
            if (!lhs || !rhs)
                return std::nullopt;
            // Only labels constrained on both sides stay constrained.
            const auto common = intersection(labels(*lhs), labels(*rhs));
            return m_build.template binary<QueryUnionTag>(m_build.project(*lhs, common), m_build.project(*rhs, common));
        }
        else if constexpr (std::same_as<Tag, QueryDifferenceTag>)
        {
            const auto lhs = relax(concrete.get_lhs(), needed);
            if (!lhs)
                return std::nullopt;
            // Subtracting is kept only when exact: every label needed and constrained, and a known rhs.
            const auto schema = labels(concrete.get_lhs());
            if (subset(schema, needed) && subset(schema, labels(*lhs)))
                if (const auto rhs = copy(concrete.get_rhs()))
                    return m_build.template binary<QueryDifferenceTag>(*lhs, m_build.project(*rhs, labels(*lhs)));
            return lhs;
        }
        else if constexpr (std::same_as<Tag, QuerySemijoinTag>)
        {
            const auto shared = intersection(labels(concrete.get_lhs()), labels(concrete.get_rhs()));
            const auto lhs = relax(concrete.get_lhs(), united(needed, shared));
            if (!lhs)
                return std::nullopt;
            const auto rhs = relax(concrete.get_rhs(), shared);
            if (!rhs)
                return lhs;
            return m_build.template binary<QuerySemijoinTag>(*lhs, *rhs);
        }
        else if constexpr (std::same_as<Tag, QueryAntijoinTag>)
        {
            const auto lhs = relax(concrete.get_lhs(), needed);
            if (!lhs)
                return std::nullopt;
            // Kept only as a filter on needed labels the relaxed lhs constrains, with a known rhs; filtering on labels
            // eliminated above would require all their values (relational division).
            const auto shared = intersection(labels(concrete.get_lhs()), labels(concrete.get_rhs()));
            if (subset(shared, needed) && subset(shared, labels(*lhs)))
                if (const auto rhs = copy(concrete.get_rhs()))
                    return m_build.template binary<QueryAntijoinTag>(*lhs, *rhs);
            return lhs;
        }
        else
        {
            static_assert(std::same_as<Tag, QueryDistanceTag>, "unhandled query operator");
            throw std::invalid_argument("Relaxation: distances are not first-order queries.");
        }
    }
};

}  // namespace detail

/// The relaxations Q̂ of queries Q whose inputs not marked by `known(slot)` may hold any rows.
///
/// Contract: Q̂ reads only the known inputs, its schema S' is a subset of Q's schema, and for all inputs I
///     Q(I) ⊆ Q̂(I) × U^(schema(Q) \ S'),
/// where U is the universe of values. Labels outside S' are unconstrained; nullopt (written ⊤ below) means no label
/// is constrained. All roots share one numbering of the known inputs, so they can be compiled together.
///
/// Translation, by induction on Q, of R̂_N = relax(R) restricted to the needed labels N (initially all of the root's
/// labels; pass π_L(Q) to need only L). Every result is finally projected onto its labels in N, so a subquery with
/// nothing needed becomes nullary, a test of its non-emptiness. S_R is R's schema, S'_R that of R̂, shared = S_A ∩ S_B,
/// and R̃ is R itself if it reads only known inputs, otherwise ⊤:
///
///     Q                    Q̂ under N
///     -------------------  ---------------------------------------------------------------------------------
///     input s              s if known(s), otherwise ⊤
///     empty                empty
///     A ⋈ B, generic join  ⋈ of the conjuncts, each under its labels needed in N or by another conjunct;
///                          a ⊤ conjunct drops out (⊤ is the identity of ⋈)
///     π_L(A)               Â_N
///     ρ(A)                 Â_ρ⁻¹(N), renamed as in ρ; ⊤ if Â = ⊤
///     σ(A)                 σ(Â_(N ∪ selected)) if Â constrains the selected columns, otherwise Â_(N ∪ selected)
///     A ∪ B                π_C(Â_N) ∪ π_C(B̂_N) with C = S'_A ∩ S'_B, possibly nullary; ⊤ if a side is ⊤
///     A \ B                Â_N \ B̃ if N ⊇ S_A = S'_A and B̃ ≠ ⊤, otherwise Â_N
///     A ⋉ B                Â_(N ∪ shared) ⋉ B̂_shared, or Â if B̂ = ⊤
///     A ▷ B                Â_N ▷ B̃ if shared ⊆ N ∩ S'_A and B̃ ≠ ⊤, otherwise Â_N
///     distance             not first-order; rejected
///
/// Each rule keeps the contract: a ⊤ operand of ⋈ or ⋉ admits every row; eliminating a label no one needs is exact
/// (π_L(A ⋈ B) = π_L(A) ⋈ π_∅(B) when B shares nothing with A or L); a union may only stay constrained where both
/// sides are; subtracting an over-approximated or unknown set may remove too much, so only an exactly known subtrahend
/// over needed labels is kept; a selection is checkable only on constrained columns. With every input known and no
/// difference or antijoin below a projection that eliminates labels it shares, Q̂ = Q.
///
/// Literature: Q̂ over-approximates the possible answers of Q in the possible-world semantics of incomplete
/// databases, the unknown inputs being arbitrary relations (Imieliński & Lipski, "Incomplete Information in
/// Relational Databases", JACM 1984; Abiteboul, Hull & Vianu, "Foundations of Databases", 1995, ch. 19; computing
/// such approximations by query rewriting: Guagliardo & Libkin, "Making SQL Queries Correct on Incomplete
/// Databases: A Feasibility Study", PODS 2016). In heuristic search, the same construction relaxes a problem by
/// dropping the constraints it cannot evaluate in advance (Pearl, "Heuristics", 1984, ch. 4); relaxing π_L(Q) is the
/// projection of pattern databases, which drops conditions on variables outside the pattern (Edelkamp, "Planning with
/// Pattern Databases", ECP 2001; Haslum, Botea, Helmert, Bonet & Koenig, "Domain-Independent Construction of Pattern
/// Database Heuristics for Cost-Optimal Planning", AAAI 2007), while antijoins within the pattern stay as in
/// constrained pattern databases (Haslum, Bonet & Geffner, "New Admissible Heuristics for Domain-Independent
/// Planning", AAAI 2005). Labels are eliminated as early as possible, as in early quantification (Burch, Clarke &
/// Long, "Symbolic Model Checking with Partitioned Transition Relations", VLSI 1991) and variable elimination (Dechter,
/// "Bucket Elimination", AIJ 1999; Abo Khamis, Ngo & Rudra, "FAQ: Questions Asked Frequently", PODS 2016); an exact
/// antijoin on eliminated labels would need relational division (Codd, "Relational Completeness of Data Base
/// Sublanguages", 1972). The rules above are our construction for yggdrasil's operators.
///
/// The relaxed queries are built in `repository`; shared subqueries stay shared.
template<ColumnTypes Values, std::predicate<size_t> Known>
Relaxation<Values> relax(std::span<const QueryView<Values>> roots, Known&& known, QueryRepository<Values>& repository, QueryBuilder<Values>& builder)
{
    auto relaxer = detail::Relaxer<Values, std::remove_cvref_t<Known>>(repository, builder, std::forward<Known>(known));
    auto queries = std::vector<std::optional<QueryView<Values>>>();
    for (const auto root : roots)
        queries.push_back(relaxer.relax(root, column_labels(root.columns())));
    return Relaxation<Values> { std::move(queries), std::move(relaxer).slots() };
}

/// The relaxation of one query; see the overload for several roots.
template<ColumnTypes Values, std::predicate<size_t> Known>
Relaxation<Values> relax(QueryView<Values> query, Known&& known, QueryRepository<Values>& repository, QueryBuilder<Values>& builder)
{
    return relax(std::span<const QueryView<Values>>(&query, 1), std::forward<Known>(known), repository, builder);
}

}  // namespace ygg::database

#endif
