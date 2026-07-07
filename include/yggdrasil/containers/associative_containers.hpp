/*
 * Copyright (C) 2026 Dominik Drexler
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

#ifndef YGG_CONTAINERS_ASSOCIATIVE_CONTAINERS_HPP_
#define YGG_CONTAINERS_ASSOCIATIVE_CONTAINERS_HPP_

#include <gtl/btree.hpp>
#include <gtl/phmap.hpp>

namespace ygg
{

template<typename T>
struct Hash;

template<typename T>
struct EqualTo;

template<typename T>
struct Less;

template<typename T>
using UnorderedSet = gtl::flat_hash_set<T, Hash<T>, EqualTo<T>>;

template<typename T, typename V>
using UnorderedMap = gtl::flat_hash_map<T, V, Hash<T>, EqualTo<T>>;

template<typename T>
using Set = gtl::btree_set<T, Less<T>>;

template<typename T, typename V>
using Map = gtl::btree_map<T, V, Less<T>>;

}  // namespace ygg

#endif
