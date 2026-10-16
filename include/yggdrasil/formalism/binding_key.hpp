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

#ifndef YGG_FORMALISM_BINDING_KEY_HPP_
#define YGG_FORMALISM_BINDING_KEY_HPP_

#include "yggdrasil/semantics/equal_to.hpp"
#include "yggdrasil/semantics/hash.hpp"

namespace ygg::formalism
{

/// Hashes a binding by its logical key, the relation and the object row of get_key(), rather than by its row identity.
/// Transparent, so a map keyed by bindings of one repository can be queried with any type whose get_key() spells the
/// same binding, e.g. a prefix of a row in another repository, without publishing it first.
struct BindingKeyHash
{
    using is_transparent = void;

    template<typename Key>
    hash_t operator()(const Key& key) const
    {
        const auto [relation, objects] = key.get_key();
        return hash_combine(relation, hash_range(objects));
    }
};

/// Compares bindings by their logical key, see BindingKeyHash.
struct BindingKeyEqualTo
{
    using is_transparent = void;

    template<typename Lhs, typename Rhs>
    bool operator()(const Lhs& lhs, const Rhs& rhs) const
    {
        const auto [lhs_relation, lhs_objects] = lhs.get_key();
        const auto [rhs_relation, rhs_objects] = rhs.get_key();
        return EqualTo<> {}(lhs_relation, rhs_relation) && equal_range(lhs_objects, rhs_objects);
    }
};

}

#endif
