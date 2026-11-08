/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/semantics/incremental/delta.hpp"

#include <urd/config.hpp>

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
void apply_delta(const Delta& delta, urd::builder<Relation<Schema>>& result)
{
    const auto columns = result.columns();
    const auto [added, removed] = delta.change();
    for (size_t i = 0; i < removed.size(); ++i)
    {
        const auto position = result.find(removed.row(i));
        URD_ASSUME(position.has_value());  // a removed row is in the result
        result.erase(*position);
    }
    for (size_t i = 0; i < added.size(); ++i)
        result.insert(added.row(i));
}
}
