/*
 * Copyright (C) 2025-2026 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef YGG_FORMALISM_COPY_HPP_
#define YGG_FORMALISM_COPY_HPP_

#include "yggdrasil/formalism/declarations.hpp"
#include "yggdrasil/formalism/interning.hpp"
#include "yggdrasil/formalism/relation_data.hpp"
#include "yggdrasil/formalism/relation_view.hpp"
#include "yggdrasil/formalism/row_view.hpp"
#include "yggdrasil/formalism/schema_view.hpp"

#include <span>
#include <urd/elements/copy.hpp>
#include <urd/elements/view.hpp>

/**
 * urd::copy of the records urd does not read field by field (urd::copying): a Schema, a row and a relation. Included only
 * by the sources that copy.
 */

/// A Schema: the pool of free relations of its column types, interned in the target.
template<>
struct urd::copying<ygg::Schema>
{
    using key_type = urd::index<ygg::Schema>;

    template<typename V, typename To, typename Storage>
    static key_type into(const V& schema, To& to, Storage& /*storage*/)
    {
        const auto& types = schema.get_column_types();
        return to.schema(std::span<const ygg::TypeId>(types.data(), types.size()));
    }
};

/// A row: its relation copied (its key as it is where the target sees it), its values as they are (only the relation knows
/// what they mean: an object column names an object the target must see, as the layers of one task share theirs), stored
/// in the target's pool of that relation (one insert_row where the relation is seen).
template<typename F>
struct urd::copying<ygg::Row<F>>
{
    using key_type = ygg::RowIndex<F>;

    template<typename V, typename To, typename Storage>
    static key_type into(const V& row, To& to, Storage& storage)
    {
        const auto relation = urd::visit_view(row.get_relation(), [&to, &storage](const auto& view) { return urd::copy(view, to, storage); });
        return key_type { relation, to.insert_row(relation, row.get_values()).first };
    }
};

/// A relation, interned or a builder's: its pool and its rows copied, its columns interned from their labels and types,
/// then the relation record interned (made canonical) in the target, from scratch records of `storage`.
template<typename F>
struct urd::copying<ygg::Relation<F>>
{
    using key_type = urd::index<ygg::Relation<F>>;

    template<typename V, typename To, typename Storage>
    static key_type into(const V& relation, To& to, Storage& storage)
    {
        const auto copied = [&to, &storage](const auto& view) { return urd::copy(view, to, storage); };
        auto record = storage.template scratch<ygg::Relation<F>>();
        record->pool = urd::visit_view(relation.get_pool(), copied);
        auto columns = storage.template scratch<ygg::Columns>();  // its labels and types, in whatever form it holds them
        const auto layouts = relation.columns();
        columns->values.set(layouts.begin(), layouts.end());
        record->columns = ygg::formalism::insert(to, *columns).first.index();
        for (const auto row : relation.get_rows())
            record->rows.push_back(urd::visit_view(row, copied));
        return ygg::formalism::insert(to, *record).first.index();
    }
};

#endif
