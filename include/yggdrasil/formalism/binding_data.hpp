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

#ifndef YGGDRASIL_FORMALISM_BINDING_DATA_HPP_
#define YGGDRASIL_FORMALISM_BINDING_DATA_HPP_

#include <stdexcept>
#include <utility>
#include <vector>
#include <yggdrasil/core/types.hpp>
#include <yggdrasil/core/types_utils.hpp>
#include <yggdrasil/formalism/binding_index.hpp>
#include <yggdrasil/formalism/declarations.hpp>
#include <yggdrasil/formalism/object_index.hpp>

namespace ygg
{

template<typename Relation, typename ObjectTag>
struct Data<ygg::formalism::RelationBinding<Relation, ObjectTag>>
{
    Index<Relation> relation;
    IndexList<ygg::formalism::Object<ObjectTag>> objects;

    Data() = default;
    Data(Index<Relation> relation_, size_t arity, IndexList<ygg::formalism::Object<ObjectTag>> objects_) : relation(relation_), objects(std::move(objects_))
    {
        if (objects.size() != arity)
            throw std::invalid_argument("RelationBinding object count does not match relation arity.");
    }
    // Python constructor
    template<typename C>
    Data(::ygg::View<Index<Relation>, C> relation_, const std::vector<::ygg::View<Index<ygg::formalism::Object<ObjectTag>>, C>>& objects_) :
        relation(),
        objects()
    {
        set(relation_, relation);
        set(objects_, objects);
    }
    Data(const Data& other) = delete;
    Data& operator=(const Data& other) = delete;
    Data(Data&& other) = default;
    Data& operator=(Data&& other) = default;

    void clear() noexcept
    {
        ygg::clear(relation);
        ygg::clear(objects);
    }

    auto cista_members() const noexcept { return std::tie(relation, objects); }
    auto identifying_members() const noexcept { return std::tie(relation, objects); }
};

}  // namespace ygg

#endif
