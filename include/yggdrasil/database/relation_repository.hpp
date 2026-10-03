/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_RELATION_REPOSITORY_HPP_
#define YGG_DATABASE_RELATION_REPOSITORY_HPP_

#include "yggdrasil/containers/raw_vector_set.hpp"
#include "yggdrasil/database/relation_pool.hpp"
#include "yggdrasil/database/relation_view.hpp"
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
template<TriviallyCopyable T>
class RelationRepositoryFactory
{
    std::shared_ptr<size_t> m_next_index = std::make_shared<size_t>(0);
    RelationPoolFactory<T> m_rows;

    size_t next_index()
    {
        if (*m_next_index == std::numeric_limits<size_t>::max())
            throw std::overflow_error("RelationRepositoryFactory: index space exhausted.");
        return (*m_next_index)++;
    }

public:
    explicit RelationRepositoryFactory(RelationPoolFactory<T> rows = {}) : m_rows(std::move(rows)) {}
    [[nodiscard]] RelationRepository<T> create();
    [[nodiscard]] std::shared_ptr<RelationRepository<T>> create_shared();
};

/// Interns column schemas and compact relation records alongside rows and sorted row-ID sets.
/// Builders are supplied by callers and never retained. All borrowed views are
/// valid until clear/destruction; clear retains allocation for subsequent states.
template<TriviallyCopyable T>
class RelationRepository
{
    friend class RelationRepositoryFactory<T>;

public:
    using SymbolRepository = formalism::SymbolRepository<Columns, Relation<T>>;
    using RowRepository = RawVectorSet<uint_t, T>;
    using RowSetRepository = RawVectorSet<uint_t, Index<RelationRow<T>>>;

private:
    SymbolRepository m_symbols;
    Data<Columns> m_columns_data;
    RowRepository m_rows;
    RowSetRepository m_row_sets;
    std::vector<Index<RelationRow<T>>> m_row_indices;
    std::vector<size_t> m_storage_indices;
    RelationPoolFactory<T> m_row_factory;
    RelationRepositoryFactory<T> m_factory;
    size_t m_index;

    RelationRepository(size_t index, RelationRepositoryFactory<T> factory, RelationPoolFactory<T> rows) :
        m_row_factory(std::move(rows)),
        m_factory(std::move(factory)),
        m_index(index)
    {
    }

public:
    RelationRepository(const RelationRepository&) = delete;
    RelationRepository& operator=(const RelationRepository&) = delete;
    RelationRepository(RelationRepository&&) = delete;
    RelationRepository& operator=(RelationRepository&&) = delete;

    size_t get_index() const noexcept { return m_index; }
    auto get_factory() const noexcept { return m_factory; }
    auto& get_row_repository() noexcept { return m_rows; }
    const auto& get_row_repository() const noexcept { return m_rows; }
    auto& get_row_set_repository() noexcept { return m_row_sets; }
    const auto& get_row_set_repository() const noexcept { return m_row_sets; }

    Index<RelationRowSet<T>> intern_rows(const Builder<Relation<T>>& builder)
    {
        m_row_indices.clear();
        m_row_indices.reserve(builder.size());
        for (size_t i = 0; i < builder.size(); ++i)
            m_row_indices.emplace_back(m_rows.insert(builder[i]));
        std::ranges::sort(m_row_indices);
        const auto index = Index<RelationRowSet<T>>(m_row_sets.insert(m_row_indices));
        ensure_storage_index(index);
        return index;
    }

    template<typename Entity>
    std::optional<View<Index<Entity>, RelationRepository>> find(const Data<Entity>& data) const noexcept
    {
        if (const auto index = m_symbols.template find_local<Entity>(data))
            return View<Index<Entity>, RelationRepository>(*index, *this);
        return std::nullopt;
    }

    std::pair<View<Index<Columns>, RelationRepository>, bool> get_or_create(Data<Columns>& data)
    {
        validate_columns(std::span<const Index<Column>>(data.values.data(), data.values.size()));
        const auto [index, created] = m_symbols.template get_or_create_local<Columns>(data);
        return { View<Index<Columns>, RelationRepository>(index, *this), created };
    }

    View<Index<Columns>, RelationRepository> intern_columns(std::span<const Index<Column>> columns)
    {
        m_columns_data.clear();
        m_columns_data.values.set(columns.begin(), columns.end());
        return get_or_create(m_columns_data).first;
    }

    std::pair<RelationView<T>, bool> get_or_create(Data<Relation<T>>& data)
    {
        if (!m_symbols.template is_local<Columns>(data.columns_index) || data.row_set_index.get_value() >= m_row_sets.size())
            throw std::invalid_argument("RelationRepository: invalid schema or row-set index.");
        const auto columns = get_columns(data.columns_index);
        const auto rows = m_row_sets[data.row_set_index.get_value()];
        for (size_t i = 0; i < rows.size(); ++i)
        {
            if (rows[i].get_value() >= m_rows.size() || m_rows[rows[i].get_value()].size() != columns.size())
                throw std::invalid_argument("RelationRepository: row does not match the schema.");
            if (i != 0 && rows[i - 1] >= rows[i])
                throw std::invalid_argument("RelationRepository: row indices must be sorted and unique.");
        }
        ensure_storage_index(data.row_set_index);
        const auto [index, created] = m_symbols.template get_or_create_local<Relation<T>>(data);
        return { RelationView<T>(index, *this), created };
    }

    template<typename Entity>
    const Data<Entity>& operator[](Index<Entity> index) const noexcept
    {
        assert(m_symbols.template is_local<Entity>(index));
        return m_symbols.template at_local<Entity>(index);
    }

    template<typename Entity>
    const RelationRepository& get_canonical_context(Index<Entity>) const noexcept
    {
        return *this;
    }
    size_t size() const noexcept { return m_symbols.template local_size<Relation<T>>(); }
    bool empty() const noexcept { return size() == 0; }

    View<Index<Columns>, RelationRepository> get_columns(Index<Columns> index) const noexcept { return { index, *this }; }

    size_t get_storage_index(Index<RelationRowSet<T>> row_set_index) const noexcept
    {
        assert(row_set_index.get_value() < m_storage_indices.size());
        return m_storage_indices[row_set_index.get_value()];
    }

    RelationView<T> rename(RelationView<T> source, std::span<const Index<Column>> columns, size_t schema_namespace)
    {
        if (&source.get_context() != this)
            throw std::invalid_argument("RelationRepository: rename requires a source in this repository.");
        if (columns.size() != source.arity())
            throw std::invalid_argument("RelationRepository: rename requires matching arity.");
        auto data = source.get_data();
        ygg::clear(data.index);
        data.columns_index = intern_columns(columns).get_index();
        data.schema_namespace = schema_namespace;
        return get_or_create(data).first;
    }

    RelationView<T> rename(RelationView<T> source, std::span<const Index<Column>> columns)
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
    void ensure_storage_index(Index<RelationRowSet<T>> index)
    {
        while (m_storage_indices.size() <= index.get_value())
            m_storage_indices.push_back(m_row_factory.next_index());
    }
};

template<TriviallyCopyable T>
RelationRepository<T> RelationRepositoryFactory<T>::create()
{
    return RelationRepository<T>(next_index(), *this, m_rows);
}

template<TriviallyCopyable T>
std::shared_ptr<RelationRepository<T>> RelationRepositoryFactory<T>::create_shared()
{
    return std::shared_ptr<RelationRepository<T>>(new RelationRepository<T>(next_index(), *this, m_rows));
}

template<TriviallyCopyable T>
const RelationRepository<T>& get_relation_repository(const RelationRepository<T>& repository) noexcept
{
    return repository;
}

template<TriviallyCopyable T>
const RelationRepository<T>& get_columns_repository(const RelationRepository<T>& repository) noexcept
{
    return repository;
}

template<TriviallyCopyable T>
auto get_or_create(RelationRepository<T>& repository, Data<Columns>& data)
{
    return repository.get_or_create(data);
}

template<TriviallyCopyable T>
auto intern_columns(Builder<Columns>& builder, RelationRepository<T>& repository)
{
    auto result = get_or_create(repository, builder.get_data());
    builder.set_index(result.first.get_index());
    return result;
}

template<TriviallyCopyable T>
Data<Relation<T>>& make_data(const Builder<Relation<T>>& builder, Data<Relation<T>>& data, RelationRepository<T>& repository, size_t schema_namespace = 0)
{
    data.clear();
    data.columns_index = repository.intern_columns(builder.columns()).get_index();
    data.row_set_index = repository.intern_rows(builder);
    data.schema_namespace = schema_namespace;
    return data;
}

template<TriviallyCopyable T>
auto get_or_create(RelationRepository<T>& repository, Data<Relation<T>>& data)
{
    return repository.get_or_create(data);
}

template<TriviallyCopyable T>
auto intern_relation(Builder<Relation<T>>& builder, RelationRepository<T>& repository, size_t schema_namespace = 0)
{
    auto data = Data<Relation<T>>();
    make_data(builder, data, repository, schema_namespace);
    auto result = get_or_create(repository, data);
    builder.set_index(result.first.get_index());
    return result;
}

}  // namespace ygg::database

#endif
