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
#include <urd/elements/copy.hpp>
#include <urd/elements/view.hpp>

/**
 * urd::copy of the records urd does not read field by field (urd::copying): a relation (its columns written from their
 * labels and types). A row is copied by urd's own copying: its values' object columns as children (urd::element_children,
 * row_view.hpp). Included only
 * by the sources that copy.
 */

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
        record->pool = urd::visit_view(relation.pool(), copied);
        auto columns = storage.template scratch<ygg::Columns>();  // its labels and types, in whatever form it holds them
        const auto layouts = relation.columns();
        columns->values.set(layouts.begin(), layouts.end());
        record->layout = to.insert(*columns).first.index();
        for (const auto row : relation.rows())
            record->rows.push_back(urd::visit_view(row, copied));
        return to.insert(*record).first.index();
    }
};

#endif
