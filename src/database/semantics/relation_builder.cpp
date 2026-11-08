/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/formalism/relation_builder.hpp"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <variant>

namespace ygg
{
void validate_columns(std::span<const ColumnLayout> columns)
{
    for (auto it = columns.begin(); it != columns.end(); ++it)
    {
        if (static_cast<std::size_t>(it->type) >= std::variant_size_v<ColumnValue>)
            throw std::invalid_argument("Columns: invalid column type.");
        if (std::ranges::find(columns.begin(), it, it->label, &ColumnLayout::label) != it)
            throw std::invalid_argument("Columns: duplicate column label.");
    }
}
}  // namespace ygg
