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

/// A relaxation of a query over inputs of which only some are known.
template<ColumnTypes Values = DefaultColumnTypes>
struct Relaxation
{
    std::optional<QueryView<Values>> query;  ///< Over inputs 0, ..., slots.size() - 1; nullopt if nothing is constrained.
    std::vector<size_t> slots;               ///< The original input slot of each relaxed input.
};

namespace detail
{

template<ColumnTypes Values, std::predicate<size_t> Known>
class Relaxer
{
public:
    Relaxer(QueryRepository<Values>& repository, QueryBuilder<Values>& builder, Known known) : m_build(repository, builder), m_known(std::move(known)) {}

    /// See relax() below.
    std::optional<QueryView<Values>> relax(QueryView<Values> query)
    {
        const auto key = query.get_index().get_value();
        if (const auto it = m_relaxed.find(key); it != m_relaxed.end())
            return it->second;
        const auto result = ygg::visit([&]<typename Tag>(QueryView<Values, Tag> concrete) { return relax(concrete); }, query.get_variant());
        m_relaxed.emplace(key, result);
        return result;
    }

    /// The query itself over the relaxed inputs, or nullopt if it reads an unknown input.
    std::optional<QueryView<Values>> copy(QueryView<Values> query)
    {
        if (!std::ranges::all_of(input_slots(std::span<const QueryView<Values>>(&query, 1)), m_known))
            return std::nullopt;
        return database::copy(query, [&](size_t slot) { return renumbered(slot); }, m_build.repository(), m_build.builder());
    }

    std::vector<size_t> slots() && { return std::move(m_slots); }

private:
    OperatorBuilder<Values> m_build;
    Known m_known;
    std::vector<size_t> m_slots;
    std::map<size_t, size_t> m_renumbered;  ///< Original slot -> relaxed input.
    std::map<uint_t, std::optional<QueryView<Values>>> m_relaxed;

    /// The relaxed input of a known slot.
    size_t renumbered(size_t slot)
    {
        const auto [found, inserted] = m_renumbered.emplace(slot, m_slots.size());
        if (inserted)
            m_slots.push_back(slot);
        return found->second;
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
    /// The labels of `query` also in `columns`, in the order of `query`.
    static std::vector<Index<Column>> shared_labels(QueryView<Values> query, std::span<const ColumnLayout> columns)
    {
        auto labels = std::vector<Index<Column>>();
        for (const auto& column : query.columns())
            if (contains_column(columns, column.label))
                labels.push_back(column.label);
        return labels;
    }

    template<typename Tag>
    std::optional<QueryView<Values>> relax(QueryView<Values, Tag> concrete)
    {
        if constexpr (std::same_as<Tag, QueryInputTag>)
            return input(concrete);
        else if constexpr (std::same_as<Tag, QueryEmptyTag>)
            return m_build.empty(concrete.columns());
        else if constexpr (std::same_as<Tag, QueryJoinTag>)
            return join(relax(concrete.get_lhs()), relax(concrete.get_rhs()));
        else if constexpr (std::same_as<Tag, QueryGenericJoinTag>)
        {
            auto result = std::optional<QueryView<Values>>();
            for (const auto input : concrete.get_inputs())
                result = join(result, relax(QueryView<Values>(input, concrete.get_repository())));
            if (!result)
                return std::nullopt;
            const auto columns = result->columns();
            auto labels = std::vector<Index<Column>>();
            for (const auto& column : concrete.columns())
                if (contains_column(columns, column.label))
                    labels.push_back(column.label);
            return m_build.project(*result, labels);
        }
        else if constexpr (std::same_as<Tag, QueryProjectTag>)
        {
            const auto arg = relax(concrete.get_arg());
            if (!arg)
                return std::nullopt;
            auto labels = std::vector<Index<Column>>();
            for (const auto label : concrete.get_labels())
                if (contains_column(arg->columns(), label))
                    labels.push_back(label);
            return m_build.project(*arg, labels);
        }
        else if constexpr (std::same_as<Tag, QueryRenameTag>)
        {
            const auto arg = relax(concrete.get_arg());
            if (!arg)
                return std::nullopt;
            // The relaxed columns keep their labels' positions in the argument's schema.
            const auto before = concrete.get_arg().columns();
            const auto after = concrete.columns();
            auto labels = std::vector<Index<Column>>();
            for (const auto& column : arg->columns())
                labels.push_back(after[column_index(before, column.label)].label);
            return m_build.template relabel<QueryRenameTag>(*arg, labels);
        }
        else if constexpr (std::same_as<Tag, QuerySelectEqualTag> || std::same_as<Tag, QuerySelectValueTag>)
        {
            const auto arg = relax(concrete.get_arg());
            if (!arg)
                return std::nullopt;
            const auto& data = concrete.get_data();
            const auto selectable = [&]
            {
                if constexpr (std::same_as<Tag, QuerySelectEqualTag>)
                    return contains_column(arg->columns(), data.lhs_column) && contains_column(arg->columns(), data.rhs_column);
                else
                    return contains_column(arg->columns(), data.column);
            }();
            // A selection on an unconstrained column is dropped.
            if (!selectable)
                return arg;
            return m_build.template make<Tag>(
                [&](auto& selected)
                {
                    selected = data;
                    selected.arg = arg->get_index();
                });
        }
        else if constexpr (std::same_as<Tag, QueryUnionTag>)
        {
            const auto lhs = relax(concrete.get_lhs());
            const auto rhs = relax(concrete.get_rhs());
            if (!lhs || !rhs)
                return std::nullopt;
            // Only labels constrained on both sides stay constrained.
            const auto common = shared_labels(*lhs, rhs->columns());
            return m_build.template binary<QueryUnionTag>(m_build.project(*lhs, common), m_build.project(*rhs, common));
        }
        else if constexpr (std::same_as<Tag, QueryDifferenceTag>)
        {
            const auto lhs = relax(concrete.get_lhs());
            if (!lhs)
                return std::nullopt;
            // Subtracting rows only shrinks the result; that is exact only for a known rhs and a fully constrained lhs.
            const auto rhs = copy(concrete.get_rhs());
            if (rhs && std::ranges::equal(column_labels(lhs->columns()), column_labels(concrete.columns())))
                return m_build.template binary<QueryDifferenceTag>(*lhs, *rhs);
            return lhs;
        }
        else if constexpr (std::same_as<Tag, QuerySemijoinTag>)
        {
            const auto lhs = relax(concrete.get_lhs());
            if (!lhs)
                return std::nullopt;
            const auto rhs = relax(concrete.get_rhs());
            if (!rhs)
                return lhs;
            return m_build.template binary<QuerySemijoinTag>(*lhs, *rhs);
        }
        else if constexpr (std::same_as<Tag, QueryAntijoinTag>)
        {
            const auto lhs = relax(concrete.get_lhs());
            if (!lhs)
                return std::nullopt;
            // Exact for a known rhs whose shared labels the relaxed lhs constrains.
            const auto rhs = copy(concrete.get_rhs());
            const auto shared = shared_labels(concrete.get_rhs(), concrete.get_lhs().columns());
            if (rhs && std::ranges::all_of(shared, [&](Index<Column> label) { return contains_column(lhs->columns(), label); }))
                return m_build.template binary<QueryAntijoinTag>(*lhs, *rhs);
            return lhs;
        }
        else
        {
            static_assert(std::same_as<Tag, QueryDistanceTag>, "unhandled query operator");
            throw std::invalid_argument("Relaxation: distances are not first-order queries.");
        }
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
};

}  // namespace detail

/// The relaxation Q̂ of a query Q whose inputs not marked by `known(slot)` may hold any rows.
///
/// Contract: Q̂ reads only the known inputs, its schema S' is a subset of Q's schema, and for all inputs I
///     Q(I) ⊆ Q̂(I) × U^(schema(Q) \ S'),
/// where U is the universe of values. Labels outside S' are unconstrained; nullopt (written ⊤ below) means no label
/// is constrained. With every input known, Q̂ = Q.
///
/// Translation, by induction on Q (R̂ = relax(R); R̃ = R itself if it reads only known inputs, otherwise ⊤):
///
///     Q                    Q̂
///     -------------------  ---------------------------------------------------------------------------------
///     input s              s if known(s), otherwise ⊤
///     empty                empty
///     A ⋈ B                Â ⋈ B̂, where a ⊤ side drops out (⊤ is the identity of ⋈)
///     generic join         the ⋈ of the relaxed inputs, projected onto the output labels in it
///     π_L(A)               π_(L ∩ S'_A)(Â), possibly nullary; ⊤ if Â = ⊤
///     ρ(A)                 Â with its columns renamed as in ρ; ⊤ if Â = ⊤
///     σ(A)                 σ(Â) if Â constrains the selected columns, otherwise Â; ⊤ if Â = ⊤
///     A ∪ B                π_C(Â) ∪ π_C(B̂) with C = S'_A ∩ S'_B, possibly nullary; ⊤ if Â or B̂ = ⊤
///     A \ B                Â \ B̃ if B̃ ≠ ⊤ and S'_A = schema(A), otherwise Â
///     A ⋉ B                Â ⋉ B̂, or Â if B̂ = ⊤
///     A ▷ B                Â ▷ B̃ if B̃ ≠ ⊤ and Â constrains the labels A shares with B, otherwise Â
///     distance             not first-order; rejected
///
/// Each rule keeps the contract: a ⊤ operand of ⋈ or ⋉ admits every row; a union may only stay constrained where
/// both sides are; subtracting an over-approximated or unknown set may remove too much, so only an exactly known
/// subtrahend is kept; a selection is checkable only on constrained columns.
///
/// Literature: Q̂ over-approximates the possible answers of Q in the possible-world semantics of incomplete
/// databases, the unknown inputs being arbitrary relations (Imieliński & Lipski, "Incomplete Information in
/// Relational Databases", JACM 1984; Abiteboul, Hull & Vianu, "Foundations of Databases", 1995, ch. 19; computing
/// such approximations by query rewriting: Guagliardo & Libkin, "Making SQL Queries Correct on Incomplete
/// Databases: A Feasibility Study", PODS 2016). In heuristic search, the same construction relaxes a problem by
/// dropping the constraints it cannot evaluate in advance (Pearl, "Heuristics", 1984, ch. 4). The rules above are
/// our construction for yggdrasil's operators.
///
/// The relaxed query is built in `repository`; shared subqueries stay shared.
template<ColumnTypes Values, std::predicate<size_t> Known>
Relaxation<Values> relax(QueryView<Values> query, Known&& known, QueryRepository<Values>& repository, QueryBuilder<Values>& builder)
{
    auto relaxer = detail::Relaxer<Values, std::remove_cvref_t<Known>>(repository, builder, std::forward<Known>(known));
    auto result = relaxer.relax(query);
    return Relaxation<Values> { result, std::move(relaxer).slots() };
}

}  // namespace ygg::database

#endif
