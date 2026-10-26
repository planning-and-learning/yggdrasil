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
#include <limits>
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

/// Values are ordered as uint_t; the largest value stands for infinity (see Arithmetic).
struct Comparison
{
    enum class Kind : uint8_t
    {
        EQUAL,
        NOT_EQUAL,
        LESS,
        LESS_EQUAL,
    };
    Kind kind = Kind::EQUAL;
    Term lhs;
    Term rhs;

    [[nodiscard]] constexpr bool holds(uint_t lhs_value, uint_t rhs_value) const noexcept
    {
        switch (kind)
        {
            case Kind::EQUAL:
                return lhs_value == rhs_value;
            case Kind::NOT_EQUAL:
                return lhs_value != rhs_value;
            case Kind::LESS:
                return lhs_value < rhs_value;
            case Kind::LESS_EQUAL:
                return lhs_value <= rhs_value;
        }
        return false;
    }

    friend constexpr bool operator==(const Comparison&, const Comparison&) = default;
    static constexpr auto cista_members() noexcept { return Members<&Comparison::kind, &Comparison::lhs, &Comparison::rhs> {}; }
    static constexpr auto identifying_members() noexcept { return Members<&Comparison::kind, &Comparison::lhs, &Comparison::rhs> {}; }
};

/// `result = lhs operation rhs`, binding the variable `result`. Values are {0, ..., M - 1} and infinity, M = the largest
/// uint_t: addition and multiplication saturate to infinity, subtraction at 0 (infinity minus anything is infinity), and
/// division by 0 is infinity; these are runir's numerical semantics (docs/description_logic_syntax_semantics.tex).
struct Arithmetic
{
    enum class Operation : uint8_t
    {
        ADD,
        SUBTRACT,
        MULTIPLY,
        DIVIDE,
        MIN,
        MAX,
    };
    static constexpr uint_t infinity = std::numeric_limits<uint_t>::max();

    Operation operation = Operation::ADD;
    Term lhs;
    Term rhs;
    uint_t result = 0;

    [[nodiscard]] constexpr uint_t apply(uint_t a, uint_t b) const noexcept
    {
        switch (operation)
        {
            case Operation::ADD:
                return a == infinity || b == infinity || a >= infinity - b ? infinity : a + b;
            case Operation::SUBTRACT:
                return a == infinity ? infinity : (b >= a ? 0 : a - b);
            case Operation::MULTIPLY:
                if (a == 0 || b == 0)
                    return 0;
                return a == infinity || b == infinity || a >= infinity / b ? infinity : a * b;
            case Operation::DIVIDE:
                if (b == 0 || a == infinity)
                    return infinity;
                return b == infinity ? 0 : a / b;
            case Operation::MIN:
                return a < b ? a : b;
            case Operation::MAX:
                return a < b ? b : a;
        }
        return infinity;
    }

    friend constexpr bool operator==(const Arithmetic&, const Arithmetic&) = default;
    static constexpr auto cista_members() noexcept { return Members<&Arithmetic::operation, &Arithmetic::lhs, &Arithmetic::rhs, &Arithmetic::result> {}; }
    static constexpr auto identifying_members() noexcept { return Members<&Arithmetic::operation, &Arithmetic::lhs, &Arithmetic::rhs, &Arithmetic::result> {}; }
};
}  // namespace ygg::database::datalog

#endif
