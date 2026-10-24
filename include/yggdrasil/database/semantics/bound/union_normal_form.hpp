/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SEMANTICS_BOUND_UNION_NORMAL_FORM_HPP_
#define YGG_DATABASE_SEMANTICS_BOUND_UNION_NORMAL_FORM_HPP_

#include "yggdrasil/database/syntax/query.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace ygg::database::bound
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
    std::vector<QueryVariable> variables;  ///< Byte ranges of the variables in a frame of `frame_size`.
    size_t frame_size = 0;
    std::vector<std::vector<ColumnLayout>> input_columns;  ///< The schema of each input slot.
    std::vector<uint32_t> output;                          ///< The variable of each of the query's columns.
    std::vector<ConjunctiveBranch> branches;
};

namespace detail
{

/// Flattens a query node into branches; `fresh` allocates the variables of projected-away columns.
class UnionNormalFormBuilder
{
    UnionNormalForm m_result;  ///< Variables, frame size and input schemas so far.

    static std::vector<ConjunctiveBranch> product(const std::vector<ConjunctiveBranch>& lhs, const std::vector<ConjunctiveBranch>& rhs);

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
    UnionNormalForm build(QueryView<> query) &&
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
    std::vector<ConjunctiveBranch> flatten(QueryView<> query, std::span<const uint32_t> mapping);
};

}  // namespace detail

UnionNormalForm union_normal_form(QueryView<> query);

}  // namespace ygg::database::bound

#endif
