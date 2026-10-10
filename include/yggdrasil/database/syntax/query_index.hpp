/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_SYNTAX_QUERY_INDEX_HPP_
#define YGG_DATABASE_SYNTAX_QUERY_INDEX_HPP_

#include "yggdrasil/database/declarations.hpp"
#include "yggdrasil/ids/index_mixins.hpp"

namespace ygg
{
template<database::ColumnTypes Values, typename Tag>
struct Index<database::Query<Values, Tag>> : IndexMixin<Index<database::Query<Values, Tag>>>
{
    using IndexMixin<Index<database::Query<Values, Tag>>>::IndexMixin;
};

}  // namespace ygg

#endif
