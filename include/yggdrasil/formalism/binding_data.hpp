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

#ifndef YGG_FORMALISM_BINDING_DATA_HPP_
#define YGG_FORMALISM_BINDING_DATA_HPP_

#include <urd/elements/flat.hpp>
#include <urd/repr/sequences/sequence.hpp>
#include <urd/store.hpp>
#include "yggdrasil/formalism/declarations.hpp"

#include <tuple>
#include <utility>
#include <vector>

namespace urd
{

template<typename RelationTag, typename ObjectTag>
struct data<ygg::formalism::RelationBinding<RelationTag, ObjectTag>>
{
    urd::index<RelationTag> relation;
    urd::flat_vector<urd::index<ygg::formalism::Object<ObjectTag>>> objects;

    data() = default;
    data(urd::index<RelationTag> relation_, urd::flat_vector<urd::index<ygg::formalism::Object<ObjectTag>>> objects_) : relation(relation_), objects(std::move(objects_)) {}
    data(const data& other) = delete;
    data& operator=(const data& other) = delete;
    data(data&& other) = default;
    data& operator=(data&& other) = default;

    static constexpr auto cista_members() noexcept { return urd::Members<&data::relation, &data::objects> {}; }
    static constexpr auto identifying_members() noexcept { return urd::Members<&data::relation, &data::objects> {}; }
};

}  // namespace urd

#endif
