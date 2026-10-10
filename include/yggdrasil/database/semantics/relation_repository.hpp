/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SEMANTICS_RELATION_REPOSITORY_HPP_
#define YGG_DATABASE_SEMANTICS_RELATION_REPOSITORY_HPP_

#include "yggdrasil/containers/raw_vector_set.hpp"
#include "yggdrasil/database/semantics/relation_pool.hpp"
#include "yggdrasil/database/semantics/relation_view.hpp"
#include "yggdrasil/formalism/interning.hpp"
#include "yggdrasil/formalism/symbol_repository.hpp"

#include <algorithm>
#include <cassert>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace ygg::database
{

/// Copies share repository and row-storage identity sequences. Repositories
/// used together in identity-keyed caches must come from the same factory.
template<ColumnTypes Values>
class RelationRepositoryFactory
{
    std::shared_ptr<size_t> m_next_index = std::make_shared<size_t>(0);
    RelationPoolFactory<Values> m_rows;

    friend class RelationRepository<Values>;

    size_t next_index()
    {
        if (*m_next_index == std::numeric_limits<size_t>::max())
            throw std::overflow_error("RelationRepositoryFactory: index space exhausted.");
        return (*m_next_index)++;
    }

public:
    explicit RelationRepositoryFactory(RelationPoolFactory<Values> rows = {}) : m_rows(std::move(rows)) {}
    [[nodiscard]] RelationRepository<Values> create();
    [[nodiscard]] std::shared_ptr<RelationRepository<Values>> create_shared();
};

/// Interns column schemas and compact relation records alongside rows and
/// sorted row-ID sets. Builders are supplied by callers and never retained. All
/// borrowed views are valid until clear/destruction; clear retains allocation
/// for subsequent states.
template<ColumnTypes Values>
class RelationRepository
{
    friend class RelationRepositoryFactory<Values>;

public:
    using SymbolRepository = formalism::SymbolRepository<Columns<Values>, Relation<Values>>;

    using SymbolTypes = typename SymbolRepository::SymbolTypes;
    using RowRepository = RawVectorSet<uint_t, std::byte>;
    using RowSetRepository = RawVectorSet<uint_t, Index<RelationRow<Values>>>;

private:
    SymbolRepository m_symbols;
    Data<Columns<Values>> m_columns_data;
    RowRepository m_rows;
    RowSetRepository m_row_sets;
    std::vector<Index<RelationRow<Values>>> m_row_indices;
    std::vector<size_t> m_storage_indices;
    RelationRepositoryFactory<Values> m_factory;
    size_t m_index;

    RelationRepository(size_t index, RelationRepositoryFactory<Values> factory) : m_factory(std::move(factory)), m_index(index) {}

public:
    RelationRepository(const RelationRepository&) = delete;
    RelationRepository& operator=(const RelationRepository&) = delete;
    RelationRepository(RelationRepository&&) = delete;
    RelationRepository& operator=(RelationRepository&&) = delete;

    size_t get_index() const noexcept { return m_index; }
    auto get_factory() const noexcept { return m_factory; }
    const auto& get_row_repository() const noexcept { return m_rows; }
    const auto& get_row_set_repository() const noexcept { return m_row_sets; }

    /// Interns the view's rows as a sorted row set.
    template<RelationViewConcept<Values> V>
    Index<RelationRowSet<Values>> insert_rows(const V& builder)
    {
        m_row_indices.clear();
        m_row_indices.reserve(builder.size());
        for (size_t i = 0; i < builder.size(); ++i)
            m_row_indices.emplace_back(m_rows.insert(builder.row(i)));
        std::ranges::sort(m_row_indices);
        const auto index = Index<RelationRowSet<Values>>(m_row_sets.insert(m_row_indices));
        ensure_storage_index(index);
        return index;
    }

    template<typename Entity>
        requires formalism::SupportsSymbol<RelationRepository, Entity>
    std::optional<View<Index<Entity>, RelationRepository>> find(const Data<Entity>& data) const noexcept
    {
        if (const auto index = m_symbols.template find_local<Entity>(data))
            return View<Index<Entity>, RelationRepository>(*index, *this);
        return std::nullopt;
    }

    std::pair<View<Index<Columns<Values>>, RelationRepository>, bool> insert(Data<Columns<Values>>& data)
    {
        validate_columns<Values>(std::span<const ColumnLayout>(data.values.data(), data.values.size()));
        const auto [index, created] = m_symbols.template insert_local<Columns<Values>>(data);
        return { View<Index<Columns<Values>>, RelationRepository>(index, *this), created };
    }

    std::pair<View<Index<Columns<Values>>, RelationRepository>, bool> insert(std::span<const ColumnLayout> columns)
    {
        m_columns_data.clear();
        m_columns_data.values.set(columns.begin(), columns.end());
        return insert(m_columns_data);
    }

    std::pair<View<Index<Columns<Values>>, RelationRepository>, bool> insert(std::span<const Index<Column>> columns)
    {
        const auto schema = Builder<Columns<Values>>(columns);
        return insert(schema.span());
    }

    std::pair<RelationView<Values>, bool> insert(Data<Relation<Values>>& data)
    {
        if (!m_symbols.template is_local<Columns<Values>>(data.columns_index) || data.row_set_index.get_value() >= m_row_sets.size())
            throw std::invalid_argument("RelationRepository: invalid schema or row-set index.");
        const auto columns = get_columns(data.columns_index);
        const auto rows = m_row_sets[data.row_set_index.get_value()];
        for (size_t i = 0; i < rows.size(); ++i)
        {
            if (rows[i].get_value() >= m_rows.size() || m_rows[rows[i].get_value()].size() != columns.row_size())
                throw std::invalid_argument("RelationRepository: row does not match the schema.");
            validate_row<Values>(m_rows[rows[i].get_value()], columns.span());
            if (i != 0 && rows[i - 1] >= rows[i])
                throw std::invalid_argument("RelationRepository: row indices must be sorted and unique.");
        }
        ensure_storage_index(data.row_set_index);
        const auto [index, created] = m_symbols.template insert_local<Relation<Values>>(data);
        return { RelationView<Values>(index, *this), created };
    }

    template<typename Entity>
        requires formalism::SupportsSymbol<RelationRepository, Entity>
    const Data<Entity>& operator[](Index<Entity> index) const noexcept
    {
        assert(m_symbols.template is_local<Entity>(index));
        return m_symbols.template at_local<Entity>(index);
    }

    template<typename Entity>
        requires formalism::SupportsSymbol<RelationRepository, Entity>
    const RelationRepository& get_canonical_context(Index<Entity>) const noexcept
    {
        return *this;
    }
    size_t size() const noexcept { return m_symbols.template local_size<Relation<Values>>(); }
    bool empty() const noexcept { return size() == 0; }

    View<Index<Columns<Values>>, RelationRepository> get_columns(Index<Columns<Values>> index) const noexcept { return { index, *this }; }

    size_t get_storage_index(Index<RelationRowSet<Values>> row_set_index) const noexcept
    {
        assert(row_set_index.get_value() < m_storage_indices.size());
        return m_storage_indices[row_set_index.get_value()];
    }

    template<typename C>
    View<Index<Relation<Values>>, C> rename(View<Index<Relation<Values>>, C> source, std::span<const ColumnLayout> columns, size_t schema_namespace)
    {
        if (&get_relation_repository(source.get_context()) != this)
            throw std::invalid_argument("RelationRepository: rename requires a source in this repository.");
        detail::validate_relabel<Values>(source.columns().span(), columns);
        auto data = source.get_data();
        ygg::clear(data.index);
        data.columns_index = insert(columns).first.get_index();
        data.schema_namespace = schema_namespace;
        return make_view(insert(data).first.get_index(), source.get_context());
    }

    template<typename C>
    View<Index<Relation<Values>>, C> rename(View<Index<Relation<Values>>, C> source, std::span<const ColumnLayout> columns)
    {
        return rename(source, columns, source.get_data().schema_namespace);
    }

    template<typename C>
    View<Index<Relation<Values>>, C> rename(View<Index<Relation<Values>>, C> source, std::span<const Index<Column>> columns, size_t schema_namespace)
    {
        auto schema = Builder<Columns<Values>>(source.columns().span());
        schema.rename(columns);
        return rename(source, schema.span(), schema_namespace);
    }

    template<typename C>
    View<Index<Relation<Values>>, C> rename(View<Index<Relation<Values>>, C> source, std::span<const Index<Column>> columns)
    {
        return rename(source, columns, source.get_data().schema_namespace);
    }

    void clear() noexcept
    {
        m_symbols.clear();
        m_columns_data.clear();
        m_row_sets.clear();
        m_rows.clear();
        m_row_indices.clear();
    }

private:
    void ensure_storage_index(Index<RelationRowSet<Values>> index)
    {
        while (m_storage_indices.size() <= index.get_value())
            m_storage_indices.push_back(m_factory.m_rows.next_index());
    }
};

template<ColumnTypes Values>
RelationRepository<Values> RelationRepositoryFactory<Values>::create()
{
    return RelationRepository<Values>(next_index(), *this);
}

template<ColumnTypes Values>
std::shared_ptr<RelationRepository<Values>> RelationRepositoryFactory<Values>::create_shared()
{
    return std::shared_ptr<RelationRepository<Values>>(new RelationRepository<Values>(next_index(), *this));
}

template<ColumnTypes Values>
const RelationRepository<Values>& get_repository(const RelationRepository<Values>& repository) noexcept
{
    return repository;
}

template<ColumnTypes Values>
const RelationRepository<Values>& get_relation_repository(const RelationRepository<Values>& repository) noexcept
{
    return repository;
}

template<ColumnTypes Values>
const RelationRepository<Values>& get_columns_repository(const RelationRepository<Values>& repository) noexcept
{
    return repository;
}

// Validation stays in the raw repository members for both direct and prepared
// calls.
template<ColumnTypes Values>
void prepare_for_insert(RelationRepository<Values>&, Data<Columns<Values>>&) noexcept
{
}

template<ColumnTypes Values>
void prepare_for_insert(RelationRepository<Values>&, Data<Relation<Values>>&) noexcept
{
}

using formalism::insert;

/// Insert mutable schemas and synchronize their canonical identity.
template<ColumnTypes Values>
auto insert(RelationRepository<Values>& repository, Builder<Columns<Values>>& builder)
{
    return formalism::insert(repository, builder.get_data());
}

/// Intern a compatible mutable relation without copying its tuple storage.
template<ColumnTypes Values>
auto insert(RelationRepository<Values>& repository, Builder<Relation<Values>>& builder, size_t schema_namespace = 0)
{
    auto data = Data<Relation<Values>>();
    data.columns_index = repository.insert(builder.columns().span()).first.get_index();
    data.row_set_index = repository.insert_rows(builder);
    data.schema_namespace = schema_namespace;
    auto result = formalism::insert(repository, data);
    builder.set_index(result.first.get_index());
    return result;
}

/// Remap repository-local schema and row identities into the destination.
template<ColumnTypes Values, typename C>
auto copy(View<Data<Relation<Values>>, C> source, RelationRepository<Values>& repository)
{
    auto data = Data<Relation<Values>>();
    data.columns_index = repository.insert(source.columns().span()).first.get_index();
    data.row_set_index = repository.insert_rows(source);
    data.schema_namespace = source.get_data().schema_namespace;
    return formalism::insert(repository, data);
}

template<ColumnTypes Values, typename C>
auto copy(View<Index<Relation<Values>>, C> source, RelationRepository<Values>& repository)
{
    if (&get_relation_repository(source.get_context()) == &repository)
        return std::pair { make_view(source.get_index(), repository), false };
    return copy(make_view(source.get_data(), source.get_context()), repository);
}

template<ColumnTypes Values, ColumnsViewConcept<Values> V>
auto copy(const V& source, RelationRepository<Values>& repository)
{
    return repository.insert(source.span());
}

}  // namespace ygg::database

#endif
