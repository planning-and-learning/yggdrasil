/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/syntax/query_repository.hpp"
#include "yggdrasil/formalism/detail/basic_symbol_repository_impl.hpp"

namespace ygg::database
{
QueryRepository QueryRepositoryFactory::create()
{
    return QueryRepository(next_index(), *this);
}
}

namespace ygg::database
{
std::shared_ptr<QueryRepository> QueryRepositoryFactory::create_shared()
{
    return std::shared_ptr<QueryRepository>(new QueryRepository(next_index(), *this));
}
}

namespace ygg::database
{
QueryRepository::QueryRepository(size_t index, QueryRepositoryFactory factory) : m_factory(std::move(factory)), m_index(index) {}

QueryRepository::~QueryRepository() = default;
}

template class ygg::formalism::BasicSymbolRepository<ygg::database::Query<>, false>;
template class ygg::formalism::BasicSymbolRepository<ygg::database::Query<ygg::database::QueryInputTag>, false>;
template class ygg::formalism::BasicSymbolRepository<ygg::database::Query<ygg::database::QueryEmptyTag>, false>;
template class ygg::formalism::BasicSymbolRepository<ygg::database::Query<ygg::database::QueryJoinTag>, false>;
template class ygg::formalism::BasicSymbolRepository<ygg::database::Query<ygg::database::QueryProjectTag>, false>;
template class ygg::formalism::BasicSymbolRepository<ygg::database::Query<ygg::database::QueryRenameTag>, false>;
template class ygg::formalism::BasicSymbolRepository<ygg::database::Query<ygg::database::QuerySelectEqualTag>, false>;
template class ygg::formalism::BasicSymbolRepository<ygg::database::Query<ygg::database::QuerySelectValueTag>, false>;
template class ygg::formalism::BasicSymbolRepository<ygg::database::Query<ygg::database::QueryUnionTag>, false>;
template class ygg::formalism::BasicSymbolRepository<ygg::database::Query<ygg::database::QueryDifferenceTag>, false>;
template class ygg::formalism::BasicSymbolRepository<ygg::database::Query<ygg::database::QueryDistanceTag>, false>;
template class ygg::formalism::BasicSymbolRepository<ygg::database::Query<ygg::database::QueryGenericJoinTag>, false>;
template class ygg::formalism::BasicSymbolRepository<ygg::database::Query<ygg::database::QuerySemijoinTag>, false>;
template class ygg::formalism::BasicSymbolRepository<ygg::database::Query<ygg::database::QueryAntijoinTag>, false>;
