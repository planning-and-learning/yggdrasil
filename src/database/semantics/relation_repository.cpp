/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/semantics/relation_repository.hpp"

#include <urd/config.hpp>

namespace ygg::database
{
RelationRepository RelationRepositoryFactory::create() { return RelationRepository(m_indices.next(), *this); }
}

namespace ygg::database
{
std::shared_ptr<RelationRepository> RelationRepositoryFactory::create_shared()
{ return std::shared_ptr<RelationRepository>(new RelationRepository(m_indices.next(), *this)); }
}

namespace ygg::database
{
const RelationRepository& get_repository(const RelationRepository& repository URD_LIFETIMEBOUND) noexcept { return repository; }
}

namespace ygg::database
{
void prepare_for_insert(RelationRepository&, urd::data<Columns>&) noexcept {}
}

namespace ygg::database
{
void prepare_for_insert(RelationRepository&, urd::data<Relation>&) noexcept {}
}

namespace ygg::database
{
RelationRepository::RelationRepository(size_t index, RelationRepositoryFactory factory) : m_factory(std::move(factory)), m_index(index) {}

RelationRepository::~RelationRepository() = default;
}

namespace ygg::database
{
std::pair<RelationView, bool> RelationRepository::insert(urd::data<Relation>& data)
{
    if (!m_symbols.contains(data.columns_index) || data.row_set_index.value >= m_row_sets.size())
        throw std::invalid_argument("RelationRepository: invalid schema or row-set index.");
    const auto columns = view(data.columns_index);
    const auto rows = m_row_sets.view(RowSetPool::handle_type(data.row_set_index.value));
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (rows[i].value >= m_rows.size() || m_rows.view(RowPool::handle_type(rows[i].value)).size() != columns.row_size())
            throw std::invalid_argument("RelationRepository: row does not match the schema.");
        if (i != 0 && rows[i - 1] >= rows[i])
            throw std::invalid_argument("RelationRepository: row indices must be sorted and unique.");
    }
    ensure_storage_index(data.row_set_index);
    const auto [index, created] = formalism::intern(m_symbols.template get<Relation>(), data);
    return { RelationView(index, this), created };
}
}
