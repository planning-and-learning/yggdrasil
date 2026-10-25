/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/datalog/repository.hpp"

#include "yggdrasil/formalism/detail/basic_symbol_repository_impl.hpp"

namespace ygg::database::datalog
{
DatalogRepository DatalogRepositoryFactory::create() { return DatalogRepository(next_index(), *this); }
}

namespace ygg::database::datalog
{
std::shared_ptr<DatalogRepository> DatalogRepositoryFactory::create_shared()
{ return std::shared_ptr<DatalogRepository>(new DatalogRepository(next_index(), *this)); }
}

namespace ygg::database::datalog
{
DatalogRepository::DatalogRepository(size_t index, DatalogRepositoryFactory factory) : m_factory(std::move(factory)), m_index(index) {}

DatalogRepository::~DatalogRepository() = default;
}

template class ygg::formalism::BasicSymbolRepository<ygg::database::datalog::Relation, false>;
template class ygg::formalism::BasicSymbolRepository<ygg::database::datalog::Atom, false>;
template class ygg::formalism::BasicSymbolRepository<ygg::database::datalog::Aggregate, false>;
template class ygg::formalism::BasicSymbolRepository<ygg::database::datalog::Rule, false>;
template class ygg::formalism::BasicSymbolRepository<ygg::database::datalog::Program, false>;
