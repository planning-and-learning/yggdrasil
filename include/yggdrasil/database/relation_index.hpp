/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_RELATION_INDEX_HPP_
#define YGG_DATABASE_RELATION_INDEX_HPP_

#include "yggdrasil/database/columns_index.hpp"
#include "yggdrasil/database/declarations.hpp"
#include "yggdrasil/ids/index_mixins.hpp"

namespace ygg
{
template<TriviallyCopyable T>
struct Index<database::Relation<T>> : IndexMixin<Index<database::Relation<T>>>
{
    using Base = IndexMixin<Index<database::Relation<T>>>;
    using Base::Base;
};

template<TriviallyCopyable T>
struct Index<database::RelationRow<T>> : IndexMixin<Index<database::RelationRow<T>>>
{
    using Base = IndexMixin<Index<database::RelationRow<T>>>;
    using Base::Base;
};

template<TriviallyCopyable T>
struct Index<database::RelationRowSet<T>> : IndexMixin<Index<database::RelationRowSet<T>>>
{
    using Base = IndexMixin<Index<database::RelationRowSet<T>>>;
    using Base::Base;
};
}  // namespace ygg

#endif
