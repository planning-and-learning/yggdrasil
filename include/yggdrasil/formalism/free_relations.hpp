/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_FORMALISM_FREE_RELATIONS_HPP_
#define YGG_FORMALISM_FREE_RELATIONS_HPP_

#include "yggdrasil/formalism/declarations.hpp"

#include <urd/elements/flat.hpp>

namespace ygg
{
/// Free relations: relations with columns only (no symbol names them), their rows in pools keyed by their Schema.
using FreeRelationRepository =
    formalism::Repository<formalism::SymbolRepository<Columns, Relation<Schema>>, formalism::RelationRepository<void, Schema>>;
/// A free relation interned in a repository layer.
using FreeRelationView = urd::index_view<Relation<Schema>, FreeRelationRepository>;
/// A free relation builder held elsewhere, read through a layer (urd::resolve(builder, layer) or layer.view(builder)).
using FreeBuilderRelationView = urd::borrowed_view<Relation<Schema>, FreeRelationRepository, urd::builder<Relation<Schema>>>;
}  // namespace ygg

#endif
