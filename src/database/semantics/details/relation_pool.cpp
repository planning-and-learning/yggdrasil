/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/semantics/relation_pool.hpp"

namespace ygg::database
{
urd::unique_slot_ptr<urd::builder<Relation>> RelationPool::get_or_allocate(std::span<const ColumnLayout> columns)
{
    validate_columns(columns);
    return allocate(row_size(columns), columns);
}
}
