/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_DATALOG_VIEW_HPP_
#define YGG_DATABASE_DATALOG_VIEW_HPP_

#include "yggdrasil/containers/vector.hpp"
#include "yggdrasil/database/datalog/data.hpp"

#include <span>

namespace ygg
{
template<typename C>
class View<Index<database::datalog::Relation>, C> : public IndexViewBase<database::datalog::Relation, C>
{
public:
    using IndexViewBase<database::datalog::Relation, C>::IndexViewBase;

    [[nodiscard]] size_t get_id() const { return this->get_data().id; }
    [[nodiscard]] size_t get_arity() const { return this->get_data().arity; }
    [[nodiscard]] bool is_input() const { return this->get_data().input_slot != Data<database::datalog::Relation>::derived; }
    [[nodiscard]] size_t get_input_slot() const { return this->get_data().input_slot; }
    [[nodiscard]] const auto& get_name() const noexcept { return this->get_data().name; }
};

template<typename C>
class View<Index<database::datalog::Atom>, C> : public IndexViewBase<database::datalog::Atom, C>
{
public:
    using IndexViewBase<database::datalog::Atom, C>::IndexViewBase;

    [[nodiscard]] auto get_relation() const { return make_view(this->get_data().relation, this->get_context()); }
    [[nodiscard]] std::span<const database::datalog::Term> get_terms() const { return { this->get_data().terms.data(), this->get_data().terms.size() }; }
};

template<typename C>
class View<Index<database::datalog::Aggregate>, C> : public IndexViewBase<database::datalog::Aggregate, C>
{
public:
    using IndexViewBase<database::datalog::Aggregate, C>::IndexViewBase;

    [[nodiscard]] auto get_function() const { return this->get_data().function; }
    [[nodiscard]] uint_t get_result() const { return this->get_data().result; }
    [[nodiscard]] uint_t get_value() const { return this->get_data().value; }
    [[nodiscard]] auto get_atoms() const { return make_view(this->get_data().atoms, this->get_context()); }
    [[nodiscard]] auto get_negated() const { return make_view(this->get_data().negated, this->get_context()); }
    [[nodiscard]] std::span<const database::datalog::Comparison> get_comparisons() const
    { return { this->get_data().comparisons.data(), this->get_data().comparisons.size() }; }
    [[nodiscard]] std::span<const database::datalog::Arithmetic> get_arithmetic() const
    { return { this->get_data().arithmetic.data(), this->get_data().arithmetic.size() }; }
};

template<typename C>
class View<Index<database::datalog::Rule>, C> : public IndexViewBase<database::datalog::Rule, C>
{
public:
    using IndexViewBase<database::datalog::Rule, C>::IndexViewBase;

    [[nodiscard]] auto get_head() const { return make_view(this->get_data().head, this->get_context()); }
    [[nodiscard]] auto get_atoms() const { return make_view(this->get_data().atoms, this->get_context()); }
    [[nodiscard]] auto get_negated() const { return make_view(this->get_data().negated, this->get_context()); }
    [[nodiscard]] std::span<const database::datalog::Comparison> get_comparisons() const
    { return { this->get_data().comparisons.data(), this->get_data().comparisons.size() }; }
    [[nodiscard]] std::span<const database::datalog::Arithmetic> get_arithmetic() const
    { return { this->get_data().arithmetic.data(), this->get_data().arithmetic.size() }; }
    [[nodiscard]] auto get_aggregates() const { return make_view(this->get_data().aggregates, this->get_context()); }
};

template<typename C>
class View<Index<database::datalog::Program>, C> : public IndexViewBase<database::datalog::Program, C>
{
public:
    using IndexViewBase<database::datalog::Program, C>::IndexViewBase;

    [[nodiscard]] auto get_rules() const { return make_view(this->get_data().rules, this->get_context()); }
};
}  // namespace ygg

namespace ygg::database::datalog
{
using RelationView = View<Index<Relation>, DatalogRepository>;
using AtomView = View<Index<Atom>, DatalogRepository>;
using AggregateView = View<Index<Aggregate>, DatalogRepository>;
using RuleView = View<Index<Rule>, DatalogRepository>;
using ProgramView = View<Index<Program>, DatalogRepository>;
}  // namespace ygg::database::datalog

#endif
