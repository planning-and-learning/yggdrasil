/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_RELATION_RELATION_REPOSITORY_HPP_
#define YGG_DATABASE_RELATION_RELATION_REPOSITORY_HPP_

#include "yggdrasil/containers/raw_vector_set.hpp"
#include "yggdrasil/core/config.hpp"
#include "yggdrasil/database/relation/relation_pool.hpp"
#include "yggdrasil/database/relation/relation_view.hpp"
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
class RelationRepositoryFactory
{
    std::shared_ptr<size_t> m_next_index = std::make_shared<size_t>(0);
    RelationPoolFactory m_rows;

    friend class RelationRepository;

    size_t next_index()
    {
        if (*m_next_index == std::numeric_limits<size_t>::max())
            throw std::overflow_error("RelationRepositoryFactory: index space exhausted.");
        return (*m_next_index)++;
    }

public:
    explicit RelationRepositoryFactory(RelationPoolFactory rows = {}) : m_rows(std::move(rows)) {}
    [[nodiscard]] RelationRepository create();
    [[nodiscard]] std::shared_ptr<RelationRepository> create_shared();
};

/// Interns column schemas and compact relation records alongside rows and
/// sorted row-ID sets. Builders are supplied by callers and never retained. All
/// borrowed views are valid until clear/destruction; clear retains allocation
/// for subsequent states.
class RelationRepository
{
    friend class RelationRepositoryFactory;

public:
    using SymbolTypes = TypeList<Columns, Relation>;

private:
    formalism::SymbolRepository<Columns, Relation> m_symbols;
    Data<Columns> m_columns_data;
    RawVectorSet<uint_t, std::byte> m_rows;
    RawVectorSet<uint_t, Index<RelationRow>> m_row_sets;
    std::vector<Index<RelationRow>> m_row_indices;
    std::vector<size_t> m_storage_indices;
    RelationRepositoryFactory m_factory;
    size_t m_index;

    RelationRepository(size_t index, RelationRepositoryFactory factory);

public:
    ~RelationRepository();
    RelationRepository(const RelationRepository&) = delete;
    RelationRepository& operator=(const RelationRepository&) = delete;
    RelationRepository(RelationRepository&&) = delete;
    RelationRepository& operator=(RelationRepository&&) = delete;

    [[nodiscard]] size_t get_index() const noexcept { return m_index; }
    [[nodiscard]] auto get_factory() const noexcept { return m_factory; }
    [[nodiscard]] const auto& get_row_repository() const noexcept { return m_rows; }
    [[nodiscard]] const auto& get_row_set_repository() const noexcept { return m_row_sets; }

    /// Interns the view's rows as a sorted row set.
    template<RelationViewConcept V>
    Index<RelationRowSet> insert_rows(const V& builder)
    {
        m_row_indices.clear();
        m_row_indices.reserve(builder.size());
        for (size_t i = 0; i < builder.size(); ++i)
            m_row_indices.emplace_back(m_rows.insert(builder.row(i)));
        std::ranges::sort(m_row_indices);
        const auto index = Index<RelationRowSet>(m_row_sets.insert(m_row_indices));
        ensure_storage_index(index);
        return index;
    }

    template<typename Entity>
        requires formalism::SupportsSymbol<RelationRepository, Entity>
    [[nodiscard]] std::optional<View<Index<Entity>, RelationRepository>> find(const Data<Entity>& data) const noexcept
    {
        if (const auto index = m_symbols.template find_local<Entity>(data))
            return View<Index<Entity>, RelationRepository>(*index, *this);
        return std::nullopt;
    }

    std::pair<View<Index<Columns>, RelationRepository>, bool> insert(Data<Columns>& data)
    {
        validate_columns(std::span<const ColumnLayout>(data.values.data(), data.values.size()));
        const auto [index, created] = m_symbols.template insert_local<Columns>(data);
        return { View<Index<Columns>, RelationRepository>(index, *this), created };
    }

    std::pair<View<Index<Columns>, RelationRepository>, bool> insert(std::span<const ColumnLayout> columns)
    {
        m_columns_data.clear();
        m_columns_data.values.set(columns.begin(), columns.end());
        return insert(m_columns_data);
    }

    std::pair<View<Index<Columns>, RelationRepository>, bool> insert(std::span<const Index<Column>> columns)
    {
        const auto schema = Builder<Columns>(columns);
        return insert(schema.span());
    }

    std::pair<RelationView, bool> insert(Data<Relation>& data);

    template<typename Entity>
        requires formalism::SupportsSymbol<RelationRepository, Entity>
    const Data<Entity>& operator[](Index<Entity> index) const noexcept
    { return m_symbols.template local<Entity>(index); }

    template<typename Entity>
        requires formalism::SupportsSymbol<RelationRepository, Entity>
    [[nodiscard]] const RelationRepository& get_canonical_context(Index<Entity>) const noexcept
    { return *this; }
    [[nodiscard]] size_t size() const noexcept { return m_symbols.template local_size<Relation>(); }
    [[nodiscard]] bool empty() const noexcept { return size() == 0; }

    [[nodiscard]] View<Index<Columns>, RelationRepository> get_columns(Index<Columns> index) const noexcept { return { index, *this }; }

    [[nodiscard]] size_t get_storage_index(Index<RelationRowSet> row_set_index) const noexcept
    {
        assert(row_set_index.get_value() < m_storage_indices.size());
        return m_storage_indices[row_set_index.get_value()];
    }

    template<typename C>
    View<Index<Relation>, C> rename(View<Index<Relation>, C> source, std::span<const ColumnLayout> columns, size_t schema_namespace)
    {
        if (&get_repository(source.get_context()) != this)
            throw std::invalid_argument("RelationRepository: rename requires a source in this repository.");
        detail::validate_relabel(source.columns().span(), columns);
        auto data = source.get_data();
        ygg::clear(data.index);
        data.columns_index = insert(columns).first.get_index();
        data.schema_namespace = schema_namespace;
        return make_view(insert(data).first.get_index(), source.get_context());
    }

    template<typename C>
    View<Index<Relation>, C> rename(View<Index<Relation>, C> source, std::span<const ColumnLayout> columns)
    { return rename(source, columns, source.get_data().schema_namespace); }

    template<typename C>
    View<Index<Relation>, C> rename(View<Index<Relation>, C> source, std::span<const Index<Column>> columns, size_t schema_namespace)
    {
        auto schema = Builder<Columns>(source.columns().span());
        schema.rename(columns);
        return rename(source, schema.span(), schema_namespace);
    }

    template<typename C>
    View<Index<Relation>, C> rename(View<Index<Relation>, C> source, std::span<const Index<Column>> columns)
    { return rename(source, columns, source.get_data().schema_namespace); }

    void clear() noexcept
    {
        m_symbols.clear();
        m_columns_data.clear();
        m_row_sets.clear();
        m_rows.clear();
        m_row_indices.clear();
    }

private:
    void ensure_storage_index(Index<RelationRowSet> index)
    {
        while (m_storage_indices.size() <= index.get_value())
            m_storage_indices.push_back(m_factory.m_rows.next_index());
    }
};

const RelationRepository& get_repository(const RelationRepository& repository YGG_LIFETIMEBOUND) noexcept;

// Validation stays in the raw repository members for both direct and prepared
// calls.
void prepare_for_insert(RelationRepository&, Data<Columns>&) noexcept;

void prepare_for_insert(RelationRepository&, Data<Relation>&) noexcept;

using formalism::insert;

/// Insert mutable schemas and synchronize their canonical identity.
inline auto insert(RelationRepository& repository, Builder<Columns>& builder) { return formalism::insert(repository, builder.get_data()); }

/// Intern a compatible mutable relation without copying its tuple storage.
inline auto insert(RelationRepository& repository, Builder<Relation>& builder, size_t schema_namespace = 0)
{
    auto data = Data<Relation>();
    data.columns_index = repository.insert(builder.columns().span()).first.get_index();
    data.row_set_index = repository.insert_rows(builder);
    data.schema_namespace = schema_namespace;
    auto result = formalism::insert(repository, data);
    builder.set_index(result.first.get_index());
    return result;
}

/// Remap repository-local schema and row identities into the destination.
template<typename C>
auto copy(View<Data<Relation>, C> source, RelationRepository& repository)
{
    auto data = Data<Relation>();
    data.columns_index = repository.insert(source.columns().span()).first.get_index();
    data.row_set_index = repository.insert_rows(source);
    data.schema_namespace = source.get_data().schema_namespace;
    return formalism::insert(repository, data);
}

template<typename C>
auto copy(View<Index<Relation>, C> source, RelationRepository& repository)
{
    if (&get_repository(source.get_context()) == &repository)
        return std::pair { make_view(source.get_index(), repository), false };
    return copy(make_view(source.get_data(), source.get_context()), repository);
}

template<ColumnsViewConcept V>
auto copy(const V& source, RelationRepository& repository)
{ return repository.insert(source.span()); }

}  // namespace ygg::database

// The symbol storage of schemas and relations is instantiated in the database library (docs/TEMPLATE_RULES.md, rule 9).
extern template class ygg::formalism::BasicSymbolRepository<ygg::database::Columns, false>;
extern template class ygg::formalism::BasicSymbolRepository<ygg::database::Relation, false>;

#endif
