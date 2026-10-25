/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_DATALOG_DATA_HPP_
#define YGG_DATABASE_DATALOG_DATA_HPP_

#include "yggdrasil/core/types_utils.hpp"
#include "yggdrasil/database/datalog/declarations.hpp"

#include <cista/containers/vector.h>
#include <cstddef>
#include <limits>

namespace ygg
{
template<>
struct Data<database::datalog::Relation>
{
    static constexpr size_t derived = std::numeric_limits<size_t>::max();

    Index<database::datalog::Relation> index;
    /// Names the relation; a derived relation's id is its relation in the compiled program.
    size_t id = 0;
    size_t arity = 0;
    /// The input slot of an input relation, or `derived`.
    size_t input_slot = derived;

    Data() = default;
    Data(size_t id_, size_t arity_, size_t input_slot_ = derived) : id(id_), arity(arity_), input_slot(input_slot_) {}

    static constexpr auto identifying_members() noexcept { return Members<&Data::id, &Data::arity, &Data::input_slot> {}; }
    static constexpr auto cista_members() noexcept { return Members<&Data::index, &Data::id, &Data::arity, &Data::input_slot> {}; }
    void clear() noexcept
    {
        clear_members(*this);
        input_slot = derived;
    }
};

template<>
struct Data<database::datalog::Atom>
{
    Index<database::datalog::Atom> index;
    Index<database::datalog::Relation> relation;
    cista::offset::vector<database::datalog::Term> terms;

    static constexpr auto identifying_members() noexcept { return Members<&Data::relation, &Data::terms> {}; }
    static constexpr auto cista_members() noexcept { return Members<&Data::index, &Data::relation, &Data::terms> {}; }
    void clear() noexcept { clear_members(*this); }
};

template<>
struct Data<database::datalog::Aggregate>
{
    Index<database::datalog::Aggregate> index;
    database::datalog::AggregateFunction function = database::datalog::AggregateFunction::COUNT;
    /// The variable that receives the aggregate, fresh in the enclosing rule.
    uint_t result = 0;
    /// The aggregated variable of SUM, MIN and MAX; COUNT counts the body's distinct variable assignments.
    uint_t value = 0;
    IndexList<database::datalog::Atom> atoms;
    IndexList<database::datalog::Atom> negated;
    cista::offset::vector<database::datalog::Comparison> comparisons;

    static constexpr auto identifying_members() noexcept
    { return Members<&Data::function, &Data::result, &Data::value, &Data::atoms, &Data::negated, &Data::comparisons> {}; }
    static constexpr auto cista_members() noexcept
    { return Members<&Data::index, &Data::function, &Data::result, &Data::value, &Data::atoms, &Data::negated, &Data::comparisons> {}; }
    void clear() noexcept { clear_members(*this); }
};

template<>
struct Data<database::datalog::Rule>
{
    Index<database::datalog::Rule> index;
    Index<database::datalog::Atom> head;
    IndexList<database::datalog::Atom> atoms;
    IndexList<database::datalog::Atom> negated;
    cista::offset::vector<database::datalog::Comparison> comparisons;
    IndexList<database::datalog::Aggregate> aggregates;

    static constexpr auto identifying_members() noexcept
    { return Members<&Data::head, &Data::atoms, &Data::negated, &Data::comparisons, &Data::aggregates> {}; }
    static constexpr auto cista_members() noexcept
    { return Members<&Data::index, &Data::head, &Data::atoms, &Data::negated, &Data::comparisons, &Data::aggregates> {}; }
    void clear() noexcept { clear_members(*this); }
};

template<>
struct Data<database::datalog::Program>
{
    Index<database::datalog::Program> index;
    IndexList<database::datalog::Rule> rules;

    static constexpr auto identifying_members() noexcept { return Members<&Data::rules> {}; }
    static constexpr auto cista_members() noexcept { return Members<&Data::index, &Data::rules> {}; }
    void clear() noexcept { clear_members(*this); }
};
}  // namespace ygg

#endif
