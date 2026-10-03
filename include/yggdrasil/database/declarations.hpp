/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_DECLARATIONS_HPP_
#define YGG_DATABASE_DECLARATIONS_HPP_

#include "yggdrasil/core/concepts.hpp"
#include "yggdrasil/core/types.hpp"

namespace ygg::database
{
/// Schema-local column identifiers, distinct from tuple values and positions.
struct Column
{
};
struct Columns
{
};
template<TriviallyCopyable T = uint_t>
struct Relation
{
};
template<TriviallyCopyable T = uint_t>
struct RelationRow
{
};
template<TriviallyCopyable T = uint_t>
struct RelationRowSet
{
};
template<TriviallyCopyable T = uint_t>
class RelationPool;
template<TriviallyCopyable T = uint_t>
class RelationPoolFactory;
template<TriviallyCopyable T = uint_t>
class RelationRepository;
template<TriviallyCopyable T = uint_t>
class RelationRepositoryFactory;
template<TriviallyCopyable T = uint_t>
using RelationView = ygg::View<ygg::Index<Relation<T>>, RelationRepository<T>>;
}  // namespace ygg::database

#endif
