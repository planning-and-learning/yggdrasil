/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_DETAIL_REPOSITORY_INSTANTIATIONS_HPP_
#define YGG_DATABASE_DETAIL_REPOSITORY_INSTANTIATIONS_HPP_

/// Extern declarations of intern for every symbol type of the database's repositories (queries, relations),
/// instantiated in the library (syntax/query_repository.cpp). Include only from sources that intern many of these
/// types; see docs/TEMPLATE_RULES.md, rule 9.

#include "yggdrasil/database/semantics/relation_view.hpp"
#include "yggdrasil/database/syntax/query_repository.hpp"
#include "yggdrasil/formalism/intern_instantiations.hpp"

/// The instantiations (PREFIX extern: declarations).
#define YGG_DATABASE_INTERNS(PREFIX) \
    YGG_FORMALISM_INTERNS(PREFIX, 18, false, boost::mp11::mp_append<ygg::database::QuerySymbolTypes, ygg::database::Repository::SymbolTypes>)

YGG_DATABASE_INTERNS(extern)

#endif
