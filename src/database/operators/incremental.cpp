/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/operators/incremental.hpp"

#include "yggdrasil/core/config.hpp"

namespace ygg::database::incremental::detail
{
void require_initialized(bool initialized)
{
    if (!initialized)
        throw std::logic_error("Incremental evaluation: initialize before use.");
}
}

namespace ygg::database::incremental::detail
{
void apply_delta(const Delta& delta, Builder<Relation>& result)
{
    const auto columns = result.columns().span();
    for (size_t i = 0; i < delta.removed.size(); ++i)
    {
        const auto position = result.find(Row(delta.removed.row(i), columns));
        YGG_ASSUME(position.has_value());  // a removed row is in the result
        result.erase(*position);
    }
    for (size_t i = 0; i < delta.added.size(); ++i)
        result.insert(Row(delta.added.row(i), columns));
}
}
