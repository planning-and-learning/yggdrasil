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
#include "yggdrasil/formalism/row_view_concepts.hpp"
#include "yggdrasil/formalism/schema_view.hpp"
#include "yggdrasil/formalism/values.hpp"

#include <cstddef>
#include <ranges>
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

/// A row: its relation copied (its key as it is where the target sees it), then its values, each as its column's type
/// reads it: an object column's object copied as a child (urd::copy of the object it names, resolved through the row's
/// context: its key as it is where the target sees the layer that holds it), any other value as it is. Stored in the
/// target's pool of that relation (one insert_row where the relation is seen), through a scratch row of `storage`. A row
/// whose context resolves no objects (a free relation's) has its values kept as they are.
template<typename F>
struct urd::copying<ygg::Row<F>>
{
    using key_type = ygg::RowIndex<F>;

    template<typename V, typename To, typename Storage>
    static key_type into(const V& row, To& to, Storage& storage)
    {
        const auto relation = urd::visit_view(row.get_relation(), [&to, &storage](const auto& view) { return urd::copy(view, to, storage); });
        if constexpr (ygg::ResolvesObjects<urd::view_context_t<V>>)
        {
            auto copied = storage.template scratch<ygg::Row<F>>();
            copied->relation = relation;
            for (std::size_t column = 0; column < std::ranges::size(row.get_values()); ++column)
            {
                if (row.type(column) == ygg::type_id<urd::index<ygg::formalism::Object>>)
                    copied->values.push_back(ygg::encode_value(urd::copy(row.template resolve<ygg::formalism::Object>(column), to, storage)));
                else
                    copied->values.push_back(row.value(column));
            }
            return key_type { relation, to.insert_row(*copied).first };
        }
        // A context that resolves no objects (a free relation's) names none: its values mean the same anywhere.
        else
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
