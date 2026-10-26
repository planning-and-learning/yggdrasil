/*
 * Copyright (C) 2026 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef YGG_DATABASE_OPERATORS_RELATIONAL_SET_HPP_
#define YGG_DATABASE_OPERATORS_RELATIONAL_SET_HPP_

#include "yggdrasil/database/operators/relational/common.hpp"

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <limits>
#include <ranges>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace ygg::database
{
/// Union and difference require identical ordered schemas. Project one input
/// first to align a differently ordered schema. The trailing underscore avoids
/// the C++ keyword union.
template<RelationViewConcept L, RelationViewConcept R>
void union_(const L& lhs, const R& rhs, Builder<Relation>& out);

template<RelationViewConcept L, RelationViewConcept R>
Builder<Relation> union_(const L& lhs, const R& rhs);

template<RelationViewConcept L, RelationViewConcept R>
void difference(const L& lhs, const R& rhs, Builder<Relation>& out);

template<RelationViewConcept L, RelationViewConcept R>
Builder<Relation> difference(const L& lhs, const R& rhs);

template<RelationViewConcept L, RelationViewConcept R>
void union_(const L& lhs, const R& rhs, Builder<Relation>& out)
{
    detail::require_same_columns(lhs, rhs);
    detail::prepare_output(out, lhs.columns().span(), { lhs.get_storage_address(), rhs.get_storage_address() });
    for (size_t i = 0; i < lhs.size(); ++i)
        out.insert(Row(lhs.row(i), lhs.columns().span()));
    for (size_t i = 0; i < rhs.size(); ++i)
        out.insert(Row(rhs.row(i), rhs.columns().span()));
}

template<RelationViewConcept L, RelationViewConcept R>
Builder<Relation> union_(const L& lhs, const R& rhs)
{
    auto out = Builder<Relation>(lhs.columns().span());
    union_(lhs, rhs, out);
    return out;
}

template<RelationViewConcept L, RelationViewConcept R>
void difference(const L& lhs, const R& rhs, Builder<Relation>& out)
{
    detail::require_same_columns(lhs, rhs);
    detail::prepare_output(out, lhs.columns().span(), { lhs.get_storage_address(), rhs.get_storage_address() });
    for (size_t i = 0; i < lhs.size(); ++i)
        if (!rhs.contains(Row(lhs.row(i), lhs.columns().span())))
            out.insert(Row(lhs.row(i), lhs.columns().span()));
}

template<RelationViewConcept L, RelationViewConcept R>
Builder<Relation> difference(const L& lhs, const R& rhs)
{
    auto out = Builder<Relation>(lhs.columns().span());
    difference(lhs, rhs, out);
    return out;
}
}  // namespace ygg::database

#endif
