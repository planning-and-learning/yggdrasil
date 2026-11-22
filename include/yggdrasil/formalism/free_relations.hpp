/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_FORMALISM_FREE_RELATIONS_HPP_
#define YGG_FORMALISM_FREE_RELATIONS_HPP_

#include "yggdrasil/formalism/builder.hpp"
#include "yggdrasil/formalism/declarations.hpp"

#include <urd/elements/flat.hpp>
#include <urd/tags.hpp>

namespace ygg
{
/// Free relations: relations with columns only (no symbol names them), their rows in pools keyed by their Schema.
using FreeRelationRepository = formalism::Repository<formalism::Symbols<Columns, Relation<Schema>>, formalism::Relations<Schema>>;
/// A free relation interned in a repository layer.
using FreeRelationView = urd::index_view<Relation<Schema>, FreeRelationRepository>;
/// A free relation being built, read through a layer of free relations.
using FreeRelationBuilder = urd::builder<Relation<Schema>, FreeRelationRepository>;
/// A free relation builder read through a layer (urd::resolve(builder, layer), layer.view(builder)).
using FreeRelationBuilderView = RelationBuilderView<Schema, FreeRelationRepository>;
/// The builders and scratch records free relations are interned through (layer.insert(builder, storage)): their
/// Schemas, columns, rows and relations. A library instantiates it in one source (builder_instantiations.hpp).
using FreeRelationStorage = formalism::BuilderStorage<FreeRelationRepository, Schema, Columns, Row<Schema>, Relation<Schema>>;
}  // namespace ygg

#endif
