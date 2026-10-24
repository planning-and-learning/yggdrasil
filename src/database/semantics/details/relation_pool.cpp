/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/semantics/details/relation_pool.hpp"

namespace ygg::database
{
RelationPool RelationPoolFactory::create_pool() { return RelationPool(*this); }
}

namespace ygg::database
{
UniqueObjectPoolPtr<ygg::Builder<Relation>> RelationPool::get_or_allocate(std::span<const ColumnLayout> columns)
{
    validate_columns(columns);
    return allocate(row_size(columns), columns);
}
}
