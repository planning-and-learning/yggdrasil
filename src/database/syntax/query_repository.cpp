/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/syntax/query_repository.hpp"

#include "yggdrasil/database/detail/repository_instantiations.hpp"

namespace ygg::database
{
QueryRepository QueryRepositoryFactory::create() { return QueryRepository(m_indices.next(), *this); }
}

namespace ygg::database
{
std::shared_ptr<QueryRepository> QueryRepositoryFactory::create_shared() { return std::shared_ptr<QueryRepository>(new QueryRepository(m_indices.next(), *this)); }
}

namespace ygg::database
{
QueryRepository::QueryRepository(size_t index, QueryRepositoryFactory factory) : m_factory(std::move(factory)), m_index(index) {}

QueryRepository::~QueryRepository() = default;
}

YGG_DATABASE_INTERNS()
