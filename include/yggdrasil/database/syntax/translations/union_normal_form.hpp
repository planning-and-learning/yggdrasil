/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SYNTAX_TRANSLATIONS_UNION_NORMAL_FORM_HPP_
#define YGG_DATABASE_SYNTAX_TRANSLATIONS_UNION_NORMAL_FORM_HPP_

#include "yggdrasil/database/syntax/query.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace ygg::database
{

/// One input occurrence of a conjunctive branch: the variable bound by each of its columns.
struct ConjunctiveAtom
{
    size_t slot = 0;
    std::vector<uint32_t> vars;
};

struct ConjunctiveBranch;

/// Rows whose `outer` variables match some branch are rejected (a difference's right side).
struct ConjunctiveNegation
{
    std::vector<uint32_t> outer;
    std::vector<ConjunctiveBranch> branches;
};

/// A conjunctive branch: one conjunctive query (a rule body) of the union, with its negated subqueries.
struct ConjunctiveBranch
{
    std::vector<ConjunctiveAtom> atoms;
    std::vector<std::pair<uint32_t, uint32_t>> equal;
    std::vector<std::pair<uint32_t, std::vector<std::byte>>> value;
    std::vector<ConjunctiveNegation> negations;
};

struct QueryVariable
{
    size_t offset = 0;
    size_t size = 0;
};

/// A query as a union of conjunctive queries with nested negation (UCQ¬; Abiteboul, Hull & Vianu, "Foundations of
/// Databases", 1995, ch. 5), its branches over shared variables. Differences and antijoins stay nested as negated
/// branches. Every variable of a branch is bound by one of its atoms, so a negation
/// only filters rows once its outer variables are bound (relational algebra is safe-range).
struct UnionNormalForm
{
    std::vector<QueryVariable> variables;               ///< Byte ranges of the variables in a frame of `frame_size`.
    size_t frame_size = 0;
    std::vector<std::vector<ColumnLayout>> input_columns;  ///< The schema of each input slot.
    std::vector<uint32_t> output;                       ///< The variable of each of the query's columns.
    std::vector<ConjunctiveBranch> branches;
};

namespace detail
{

/// Flattens a query node into branches; `fresh` allocates the variables of projected-away columns.
template<ColumnTypes Values>
class UnionNormalFormBuilder
{
    UnionNormalForm m_result;  ///< Variables, frame size and input schemas so far.

    static std::vector<ConjunctiveBranch> product(const std::vector<ConjunctiveBranch>& lhs, const std::vector<ConjunctiveBranch>& rhs)
    {
        std::vector<ConjunctiveBranch> result;
        for (const auto& l : lhs)
            for (const auto& r : rhs)
            {
                auto& merged = result.emplace_back(l);
                merged.atoms.insert(merged.atoms.end(), r.atoms.begin(), r.atoms.end());
                merged.equal.insert(merged.equal.end(), r.equal.begin(), r.equal.end());
                merged.value.insert(merged.value.end(), r.value.begin(), r.value.end());
                merged.negations.insert(merged.negations.end(), r.negations.begin(), r.negations.end());
            }
        return result;
    }

    /// The child's variables: its columns found in `parent` take the parent's variable, others a fresh one.
    std::vector<uint32_t> by_label(std::span<const ColumnLayout> child, std::span<const ColumnLayout> parent, std::span<const uint32_t> mapping)
    {
        std::vector<uint32_t> result;
        for (const auto& column : child)
        {
            const auto found = std::ranges::find(parent, column.label, &ColumnLayout::label);
            result.push_back(found != parent.end() ? mapping[size_t(found - parent.begin())] : fresh(column));
        }
        return result;
    }

public:
    /// The union normal form of a query.
    UnionNormalForm build(QueryView<Values> query) &&
    {
        for (const auto& column : query.columns())
            m_result.output.push_back(fresh(column));
        m_result.branches = flatten(query, m_result.output);
        return std::move(m_result);
    }

    uint32_t fresh(const ColumnLayout& column)
    {
        m_result.variables.push_back(QueryVariable { m_result.frame_size, column.size });
        m_result.frame_size += column.size;
        return uint32_t(m_result.variables.size() - 1);
    }

    /// `mapping` holds the variable of each of the query's columns, by position.
    std::vector<ConjunctiveBranch> flatten(QueryView<Values> query, std::span<const uint32_t> mapping)
    {
        const auto columns = query.columns();
        return ygg::visit(
            [&]<typename Tag>(QueryView<Values, Tag> concrete) -> std::vector<ConjunctiveBranch>
            {
                if constexpr (std::same_as<Tag, QueryInputTag>)
                {
                    const auto slot = concrete.get_input_slot();
                    if (m_result.input_columns.size() <= slot)
                        m_result.input_columns.resize(slot + 1);
                    if (!m_result.input_columns[slot].empty() && !std::ranges::equal(m_result.input_columns[slot], columns))
                        throw std::invalid_argument("Union normal form: an input slot has two schemas.");
                    m_result.input_columns[slot].assign(columns.begin(), columns.end());
                    auto branch = ConjunctiveBranch {};
                    branch.atoms.push_back(ConjunctiveAtom { concrete.get_input_slot(), std::vector<uint32_t>(mapping.begin(), mapping.end()) });
                    return { std::move(branch) };
                }
                else if constexpr (std::same_as<Tag, QueryEmptyTag>)
                    return {};
                else if constexpr (std::same_as<Tag, QueryJoinTag> || std::same_as<Tag, QuerySemijoinTag>)
                {
                    // A semijoin's rhs-only columns are not in the output, so they get fresh variables.
                    const auto lhs = concrete.get_lhs();
                    const auto rhs = concrete.get_rhs();
                    return product(flatten(lhs, by_label(lhs.columns(), columns, mapping)), flatten(rhs, by_label(rhs.columns(), columns, mapping)));
                }
                else if constexpr (std::same_as<Tag, QueryGenericJoinTag>)
                {
                    // Labels outside the output are shared by all inputs that name them.
                    std::vector<ColumnLayout> labels(columns.begin(), columns.end());
                    std::vector<uint32_t> variables(mapping.begin(), mapping.end());
                    auto result = std::vector<ConjunctiveBranch>(1);
                    const auto& repository = concrete.get_repository();
                    for (const auto input : concrete.get_inputs())
                    {
                        const auto child = QueryView<Values>(input, repository);
                        for (const auto& column : child.columns())
                            if (std::ranges::find(labels, column.label, &ColumnLayout::label) == labels.end())
                            {
                                labels.push_back(column);
                                variables.push_back(fresh(column));
                            }
                        result = product(std::move(result), flatten(child, by_label(child.columns(), labels, variables)));
                    }
                    return result;
                }
                else if constexpr (std::same_as<Tag, QueryProjectTag>)
                {
                    const auto arg = concrete.get_arg();
                    return flatten(arg, by_label(arg.columns(), columns, mapping));
                }
                else if constexpr (std::same_as<Tag, QueryRenameTag>)
                    return flatten(concrete.get_arg(), mapping);
                else if constexpr (std::same_as<Tag, QuerySelectEqualTag> || std::same_as<Tag, QuerySelectValueTag>)
                {
                    auto branches = flatten(concrete.get_arg(), mapping);
                    for (auto& branch : branches)
                    {
                        if constexpr (std::same_as<Tag, QuerySelectEqualTag>)
                            branch.equal.emplace_back(mapping[concrete.get_lhs_position()], mapping[concrete.get_rhs_position()]);
                        else
                        {
                            const auto constant = concrete.get_constant();
                            branch.value.emplace_back(mapping[concrete.get_position()], std::vector<std::byte>(constant.begin(), constant.end()));
                        }
                    }
                    return branches;
                }
                else if constexpr (std::same_as<Tag, QueryUnionTag>)
                {
                    const auto lhs = concrete.get_lhs();
                    const auto rhs = concrete.get_rhs();
                    auto result = flatten(lhs, by_label(lhs.columns(), columns, mapping));
                    auto right = flatten(rhs, by_label(rhs.columns(), columns, mapping));
                    result.insert(result.end(), std::make_move_iterator(right.begin()), std::make_move_iterator(right.end()));
                    return result;
                }
                else if constexpr (std::same_as<Tag, QueryDifferenceTag> || std::same_as<Tag, QueryAntijoinTag>)
                {
                    // An antijoin's rhs-only columns get fresh variables inside the negation.
                    const auto lhs = concrete.get_lhs();
                    const auto rhs = concrete.get_rhs();
                    auto result = flatten(lhs, by_label(lhs.columns(), columns, mapping));
                    auto negation = ConjunctiveNegation { std::vector<uint32_t>(mapping.begin(), mapping.end()),
                                                     flatten(rhs, by_label(rhs.columns(), columns, mapping)) };
                    for (auto& branch : result)
                        branch.negations.push_back(negation);
                    return result;
                }
                else
                {
                    static_assert(std::same_as<Tag, QueryDistanceTag>, "unhandled query operator");
                    throw std::invalid_argument("Union normal form: distances are not first-order queries.");
                }
            },
            query.get_variant());
    }
};

}  // namespace detail

template<ColumnTypes Values>
UnionNormalForm union_normal_form(QueryView<Values> query)
{
    return detail::UnionNormalFormBuilder<Values> {}.build(query);
}

}  // namespace ygg::database

#endif
