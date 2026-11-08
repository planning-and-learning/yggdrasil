/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/syntax/query_repository.hpp"

#include "yggdrasil/database/detail/repository_instantiations.hpp"

namespace ygg::database
{
QueryRepository::QueryRepository() : m_index(s_next_index.fetch_add(1, std::memory_order_relaxed)) {}

QueryRepository::~QueryRepository() = default;
}

YGG_DATABASE_INTERNS()
