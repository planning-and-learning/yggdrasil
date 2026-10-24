/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/semantics/incremental/query.hpp"

namespace ygg::database::incremental::detail
{
void QueryValue::set(std::span<const std::byte> bytes, bool present)
{
    const Row row(bytes, result.columns().span());
    const auto position = result.find(row);
    if (present && !position)
    {
        result.insert(row);
        delta.added.insert(row);
    }
    else if (!present && position)
    {
        delta.removed.insert(row);
        result.erase(*position);
    }
}
}
