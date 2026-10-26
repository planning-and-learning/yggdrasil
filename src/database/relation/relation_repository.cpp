/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/relation/relation_repository.hpp"

#include "yggdrasil/formalism/detail/basic_symbol_repository_impl.hpp"

namespace ygg::database
{
RelationRepository RelationRepositoryFactory::create() { return RelationRepository(next_index(), *this); }
}

namespace ygg::database
{
std::shared_ptr<RelationRepository> RelationRepositoryFactory::create_shared()
{ return std::shared_ptr<RelationRepository>(new RelationRepository(next_index(), *this)); }
}

namespace ygg::database
{
const RelationRepository& get_repository(const RelationRepository& repository YGG_LIFETIMEBOUND) noexcept { return repository; }
}

namespace ygg::database
{
void prepare_for_insert(RelationRepository&, Data<Columns>&) noexcept {}
}

namespace ygg::database
{
void prepare_for_insert(RelationRepository&, Data<Relation>&) noexcept {}
}

namespace ygg::database
{
RelationRepository::RelationRepository(size_t index, RelationRepositoryFactory factory) : m_factory(std::move(factory)), m_index(index) {}

RelationRepository::~RelationRepository() = default;
}

template class ygg::formalism::BasicSymbolRepository<ygg::database::Columns, false>;
template class ygg::formalism::BasicSymbolRepository<ygg::database::Relation, false>;

namespace ygg::database
{
std::pair<RelationView, bool> RelationRepository::insert(Data<Relation>& data)
{
    if (!m_symbols.template is_local<Columns>(data.columns_index) || data.row_set_index.get_value() >= m_row_sets.size())
        throw std::invalid_argument("RelationRepository: invalid schema or row-set index.");
    const auto columns = get_columns(data.columns_index);
    const auto rows = m_row_sets[data.row_set_index.get_value()];
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (rows[i].get_value() >= m_rows.size() || m_rows[rows[i].get_value()].size() != columns.row_size())
            throw std::invalid_argument("RelationRepository: row does not match the schema.");
        if (i != 0 && rows[i - 1] >= rows[i])
            throw std::invalid_argument("RelationRepository: row indices must be sorted and unique.");
    }
    ensure_storage_index(data.row_set_index);
    const auto [index, created] = m_symbols.template insert_local<Relation>(data);
    return { RelationView(index, *this), created };
}
}
