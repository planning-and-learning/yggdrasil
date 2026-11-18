/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_FORMALISM_SCRATCH_RELATION_HPP_
#define YGG_FORMALISM_SCRATCH_RELATION_HPP_

#include "yggdrasil/formalism/column_layout.hpp"
#include "yggdrasil/formalism/declarations.hpp"
#include "yggdrasil/formalism/free_relations.hpp"
#include "yggdrasil/formalism/relation_builder.hpp"

#include <concepts>
#include <span>
#include <urd/elements/handle.hpp>
#include <urd/repr/slots/slots.hpp>
#include <urd/store.hpp>

namespace ygg
{
/// A relation being built together with the storage of its rows: its own repository C (no parent: its rows, columns and
/// pool are interned there, by its builder's members) and its builder, read through it (view()). It does not move: its
/// builder's children are views into its repository.
template<typename F, typename C>
struct ScratchRelation
{
    C rows;
    urd::builder<Relation<F>, C> relation;

    /// An empty one (a repository's default constructor is explicit: so is not an aggregate's).
    ScratchRelation() : rows(), relation() {}

    /// Its builder, read through its repository.
    [[nodiscard]] RelationBuilderView<F, C> view() const { return rows.view(relation); }
};

/// The scratch relations a computation draws (draw()): each held by its handle and given back with it, its
/// repository's buffers kept for the next.
template<typename F, typename C>
class ScratchRelations
{
public:
    /// A scratch relation of these columns (validated) in the pool `pool`: its repository cleared, its builder
    /// initialized.
    [[nodiscard]] urd::handle<ScratchRelation<F, C>> draw(urd::index<F> pool, std::span<const ColumnLayout> columns)
    {
        auto drawn = m_slots.make_handle();
        drawn->rows.clear();
        drawn->relation.initialize(drawn->rows, pool, columns);
        return drawn;
    }

    /// A free relation of these columns (validated), its pool the Schema of their types.
    [[nodiscard]] urd::handle<ScratchRelation<F, C>> draw(std::span<const ColumnLayout> columns)
        requires std::same_as<F, Schema>
    {
        auto drawn = m_slots.make_handle();
        drawn->rows.clear();
        drawn->relation.initialize(drawn->rows, columns);
        return drawn;
    }

private:
    urd::Store<urd::slots<ScratchRelation<F, C>>> m_slots;
};

/// A free relation being built with its rows' storage, and the scratch relations to draw them from.
using FreeScratchRelation = ScratchRelation<Schema, FreeRelationRepository>;
using FreeScratchRelations = ScratchRelations<Schema, FreeRelationRepository>;
}  // namespace ygg

#endif
