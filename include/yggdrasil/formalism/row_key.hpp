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

#ifndef YGG_FORMALISM_ROW_KEY_HPP_
#define YGG_FORMALISM_ROW_KEY_HPP_

#include "yggdrasil/core/config.hpp"
#include "yggdrasil/formalism/view_reads.hpp"

#include <urd/elements/equal_to.hpp>
#include <urd/elements/hash.hpp>

namespace ygg
{

/// Hashes a row by its logical key (key_of: its relation_key() and its get_values()), rather than by its identity.
/// Transparent, so a map keyed by rows of one repository can be queried with any type whose relation_key() and
/// get_values() spell the same row, e.g. a prefix of a row in another repository, without storing it first.
struct RowKeyHash
{
    using is_transparent = void;

    template<typename Key>
    hash_t operator()(const Key& key) const
    {
        const auto [relation, values] = ygg::key_of(key);
        return urd::hash_combine(relation, urd::hash_range(values));
    }
};

/// Compares rows by their logical key, see RowKeyHash.
struct RowKeyEqualTo
{
    using is_transparent = void;

    template<typename Lhs, typename Rhs>
    bool operator()(const Lhs& lhs, const Rhs& rhs) const
    {
        const auto [lhs_relation, lhs_values] = ygg::key_of(lhs);
        const auto [rhs_relation, rhs_values] = ygg::key_of(rhs);
        return urd::EqualTo<> {}(lhs_relation, rhs_relation) && urd::equal_elements(lhs_values, rhs_values);
    }
};

}

#endif
