/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_DATALOG_DECLARATIONS_HPP_
#define YGG_DATABASE_DATALOG_DECLARATIONS_HPP_

#include "yggdrasil/core/type_list.hpp"
#include "yggdrasil/core/types.hpp"
#include "yggdrasil/database/declarations.hpp"

#include <compare>
#include <cstdint>
#include <tuple>

/// Stratified Datalog over relations of object columns, with aggregates, evaluated by evaluator.hpp.
namespace ygg::database::datalog
{
/// An input relation (an input slot of the evaluator) or a derived relation (the heads of rules).
struct Relation;
/// A relation applied to terms.
struct Atom;
/// `result = function value : body`, grouped by the body's variables that the enclosing rule's atoms bind.
struct Aggregate;
/// head ← atoms, not negated, comparisons, aggregates.
struct Rule;
struct Program;

using DatalogSymbolTypes = TypeList<Relation, Atom, Aggregate, Rule, Program>;

class DatalogRepository;

enum class AggregateFunction : uint8_t
{
    /// The number of rows; 0 for a group without rows.
    COUNT,
    /// The sum of a column; 0 for a group without rows.
    SUM,
    /// The least value of a column; a group without rows has none.
    MIN,
    /// The greatest value of a column; a group without rows has none.
    MAX,
};

/// A variable, numbered within its rule, or an object constant.
struct Term
{
    enum class Kind : uint8_t
    {
        VARIABLE,
        CONSTANT,
    };
    Kind kind = Kind::VARIABLE;
    uint_t value = 0;

    static constexpr Term variable(uint_t variable) noexcept { return { Kind::VARIABLE, variable }; }
    static constexpr Term constant(uint_t object) noexcept { return { Kind::CONSTANT, object }; }
    [[nodiscard]] constexpr bool is_variable() const noexcept { return kind == Kind::VARIABLE; }

    friend constexpr bool operator==(const Term&, const Term&) = default;
    friend constexpr auto operator<=>(const Term& lhs, const Term& rhs) { return std::tie(lhs.kind, lhs.value) <=> std::tie(rhs.kind, rhs.value); }
    static constexpr auto cista_members() noexcept { return Members<&Term::kind, &Term::value> {}; }
    static constexpr auto identifying_members() noexcept { return Members<&Term::kind, &Term::value> {}; }
};

struct Comparison
{
    enum class Kind : uint8_t
    {
        EQUAL,
        NOT_EQUAL,
    };
    Kind kind = Kind::EQUAL;
    Term lhs;
    Term rhs;

    friend constexpr bool operator==(const Comparison&, const Comparison&) = default;
    static constexpr auto cista_members() noexcept { return Members<&Comparison::kind, &Comparison::lhs, &Comparison::rhs> {}; }
    static constexpr auto identifying_members() noexcept { return Members<&Comparison::kind, &Comparison::lhs, &Comparison::rhs> {}; }
};
}  // namespace ygg::database::datalog

#endif
