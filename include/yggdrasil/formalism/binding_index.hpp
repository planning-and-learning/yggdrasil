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

#ifndef YGG_FORMALISM_BINDING_INDEX_HPP_
#define YGG_FORMALISM_BINDING_INDEX_HPP_

#include <concepts>
#include <ranges>
#include <tuple>
#include <type_traits>
#include <yggdrasil/core/types.hpp>
#include <yggdrasil/formalism/declarations.hpp>
#include <yggdrasil/ids/index_mixins.hpp>

namespace ygg
{
template<>
struct Index<ygg::formalism::Row> : IndexMixin<Index<ygg::formalism::Row>>
{
    // Inherit constructors
    using Base = IndexMixin<Index<ygg::formalism::Row>>;
    using Base::Base;
};

template<typename Relation, typename ObjectTag>
struct Index<ygg::formalism::RelationBinding<Relation, ObjectTag>>
{
    Index<Relation> relation;
    Index<ygg::formalism::Row> row;

    auto identifying_members() const noexcept { return std::tie(relation, row); }
};
}  // namespace ygg

namespace ygg::formalism
{

template<typename Relation, typename ObjectTag, std::ranges::forward_range BindingRange>
    requires std::same_as<std::remove_cvref_t<std::ranges::range_reference_t<BindingRange>>, Index<ygg::formalism::Row>>
struct RelationBindingsForwardRange
{
    const Index<Relation>& relation;
    const BindingRange& rows;
};

template<typename Relation, typename ObjectTag, std::ranges::random_access_range BindingRange>
    requires std::same_as<std::remove_cvref_t<std::ranges::range_reference_t<BindingRange>>, Index<ygg::formalism::Row>>
struct RelationBindingsRandomAccessRange
{
    const Index<Relation>& relation;
    const BindingRange& rows;
};
}  // namespace ygg::formalism

#endif
