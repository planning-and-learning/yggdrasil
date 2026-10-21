/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/semantics/search/backward_frontier.hpp"

namespace ygg::database
{
void BackwardFrontier::start(std::span<const uint_t> targets)
{
    m_level.clear();
    m_frontier.clear();
    for (const auto target : targets)
    {
        level_slot(target) = 0;
        m_frontier.push_back(target);
    }
    m_depth = 0;
    m_exhausted = false;
}
}
