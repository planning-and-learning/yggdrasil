/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SEMANTICS_RELATION_INDEX_HPP_
#define YGG_DATABASE_SEMANTICS_RELATION_INDEX_HPP_

#include "yggdrasil/database/syntax/columns_index.hpp"
#include "yggdrasil/database/declarations.hpp"
#include "yggdrasil/ids/index_mixins.hpp"

namespace ygg
{
template<database::ColumnTypes Values>
struct Index<database::Relation<Values>> : IndexMixin<Index<database::Relation<Values>>>
{
    using Base = IndexMixin<Index<database::Relation<Values>>>;
    using Base::Base;
};

template<database::ColumnTypes Values>
struct Index<database::RelationRow<Values>> : IndexMixin<Index<database::RelationRow<Values>>>
{
    using Base = IndexMixin<Index<database::RelationRow<Values>>>;
    using Base::Base;
};

template<database::ColumnTypes Values>
struct Index<database::RelationRowSet<Values>> : IndexMixin<Index<database::RelationRowSet<Values>>>
{
    using Base = IndexMixin<Index<database::RelationRowSet<Values>>>;
    using Base::Base;
};
}  // namespace ygg

#endif
