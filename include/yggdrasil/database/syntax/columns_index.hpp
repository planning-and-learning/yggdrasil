/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SYNTAX_COLUMNS_INDEX_HPP_
#define YGG_DATABASE_SYNTAX_COLUMNS_INDEX_HPP_

#include "yggdrasil/database/declarations.hpp"
#include "yggdrasil/ids/index_mixins.hpp"

namespace ygg
{
template<>
struct Index<database::Column> : IndexMixin<Index<database::Column>>
{
    using Base = IndexMixin<Index<database::Column>>;
    using Base::Base;
};

template<database::ColumnTypes Values>
struct Index<database::Columns<Values>> : IndexMixin<Index<database::Columns<Values>>>
{
    using Base = IndexMixin<Index<database::Columns<Values>>>;
    using Base::Base;
};
}  // namespace ygg

#endif
