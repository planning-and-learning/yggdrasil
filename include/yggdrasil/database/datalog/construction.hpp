/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_DATALOG_CONSTRUCTION_HPP_
#define YGG_DATABASE_DATALOG_CONSTRUCTION_HPP_

#include "yggdrasil/database/datalog/repository.hpp"
#include "yggdrasil/formalism/builder.hpp"
#include "yggdrasil/formalism/interning.hpp"

namespace ygg::database::datalog
{
using DatalogBuilder = ApplyTypeListT<formalism::BuilderStorage, DatalogSymbolTypes>;

using formalism::checkout;
using formalism::insert;

void prepare_for_insert(DatalogRepository& repository, Data<Relation>& data);

/// Requires as many terms as the relation's arity.
void prepare_for_insert(DatalogRepository& repository, Data<Atom>& data);

/// Requires every variable of the negated atoms and comparisons, and the value of SUM, MIN and MAX, in a positive atom of
/// the aggregate, and a result that its atoms do not use.
void prepare_for_insert(DatalogRepository& repository, Data<Aggregate>& data);

/// Requires a derived head with distinct variables, and every variable of the head, the negated atoms and the
/// comparisons in a positive atom or as an aggregate's result; the results are distinct and not in a positive atom.
void prepare_for_insert(DatalogRepository& repository, Data<Rule>& data);

inline void prepare_for_insert(DatalogRepository&, Data<Program>&) {}
}  // namespace ygg::database::datalog

#endif
