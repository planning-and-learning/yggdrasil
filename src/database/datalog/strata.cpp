/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/datalog/detail/strata.hpp"

#include "yggdrasil/database/datalog/detail/program.hpp"

namespace ygg::database::datalog::detail
{
std::vector<Version>& Stratum::reads(const CompiledRule& rule, Version all)
{
    m_reads.assign(rule.literals.size(), all);
    return m_reads;
}
}  // namespace ygg::database::datalog::detail
