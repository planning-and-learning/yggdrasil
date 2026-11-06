/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SEMANTICS_RELATION_REPOSITORY_HPP_
#define YGG_DATABASE_SEMANTICS_RELATION_REPOSITORY_HPP_

#include <urd/repr/sequences/sequence.hpp>
#include <urd/repr/pools/pools.hpp>
#include <urd/store.hpp>
#include <urd/config.hpp>
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
    using SymbolTypes = boost::mp11::mp_list<Columns, Relation>;
    /// Rows of any width (one per schema), and sorted row-ID sets, interned in urd pools (stored flat): a handle's
    /// value is a row's ID, or a row set's.
    using RowPool = urd::Store<urd::pool<urd::Store<urd::sequence<std::byte>, urd::frozen>>>;
    using RowSetPool = urd::Store<urd::pool<urd::Store<urd::sequence<urd::index<RelationRow>>, urd::frozen>>>;

private:
    formalism::SymbolRepository<Columns, Relation> m_symbols;
    urd::data<Columns> m_columns_data;
    RowPool m_rows;
    RowSetPool m_row_sets;
    urd::Store<urd::sequence<urd::index<RelationRow>>> m_row_indices;
    urd::Store<urd::sequence<size_t>> m_storage_indices;
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
    /// The repository's number, as urd::view asks of a context.
    [[nodiscard]] std::size_t layer() const noexcept { return m_index; }
    [[nodiscard]] auto get_factory() const noexcept { return m_factory; }
    [[nodiscard]] const auto& get_row_repository() const noexcept { return m_rows; }
    [[nodiscard]] const auto& get_row_set_repository() const noexcept { return m_row_sets; }

    /// Interns the view's rows as a sorted row set.
    template<RelationViewConcept V>
    urd::index<RelationRowSet> insert_rows(const V& builder)
    {
        m_row_indices.clear();
        m_row_indices.reserve(builder.size());
        for (size_t i = 0; i < builder.size(); ++i)
            m_row_indices.emplace_back(m_rows.insert(builder.row(i)).first.value);
        std::ranges::sort(m_row_indices);
        const auto index = urd::index<RelationRowSet>(m_row_sets.insert(m_row_indices).first.value);
        ensure_storage_index(index);
        return index;
    }

    template<typename Entity>
        requires formalism::SupportsSymbol<RelationRepository, Entity>
    [[nodiscard]] std::optional<urd::view<Entity, RelationRepository>> find(const urd::data<Entity>& data) const noexcept
    {
        if (const auto index = m_symbols.template find_local<Entity>(data))
            return urd::view<Entity, RelationRepository>(*index, *this);
        return std::nullopt;
    }

    std::pair<urd::view<Columns, RelationRepository>, bool> insert(urd::data<Columns>& data)
    {
        validate_columns(std::span<const ColumnLayout>(data.values.data(), data.values.size()));
        const auto [index, created] = m_symbols.template insert_local<Columns>(data);
        return { urd::view<Columns, RelationRepository>(index, *this), created };
    }

    std::pair<urd::view<Columns, RelationRepository>, bool> insert(std::span<const ColumnLayout> columns)
    {
        urd::clear_members(m_columns_data);
        m_columns_data.values.set(columns.begin(), columns.end());
        return insert(m_columns_data);
    }

    std::pair<urd::view<Columns, RelationRepository>, bool> insert(std::span<const urd::index<Column>> columns)
    {
        const auto schema = urd::builder<Columns>(columns);
        return insert(schema.span());
    }

    std::pair<RelationView, bool> insert(urd::data<Relation>& data);

    template<typename Entity>
        requires formalism::SupportsSymbol<RelationRepository, Entity>
    const urd::data<Entity>& operator[](urd::index<Entity> index) const noexcept
    { return m_symbols.template local<Entity>(index); }

    /// An index as its view here (urd's resolves contract): what compositions of indices are read through (urd::resolve).
    template<typename T>
    [[nodiscard]] urd::view<T, RelationRepository> view(urd::index<T> index) const noexcept
    { return { index, *this }; }

    [[nodiscard]] size_t size() const noexcept { return m_symbols.template local_size<Relation>(); }
    [[nodiscard]] bool empty() const noexcept { return size() == 0; }

    [[nodiscard]] urd::view<Columns, RelationRepository> get_columns(urd::index<Columns> index) const noexcept { return { index, *this }; }

    [[nodiscard]] size_t get_storage_index(urd::index<RelationRowSet> row_set_index) const noexcept
    {
        assert(row_set_index.value < m_storage_indices.size());
        return m_storage_indices[row_set_index.value];
    }

    template<typename C>
    urd::view<Relation, C> rename(urd::view<Relation, C> source, std::span<const ColumnLayout> columns, size_t schema_namespace)
    {
        if (&get_repository(source.context()) != this)
            throw std::invalid_argument("RelationRepository: rename requires a source in this repository.");
        detail::validate_relabel(source.columns().span(), columns);
        auto data = source.data();
        data.index = {};
        data.columns_index = insert(columns).first.index();
        data.schema_namespace = schema_namespace;
        return source.context().view(insert(data).first.index());
    }

    template<typename C>
    urd::view<Relation, C> rename(urd::view<Relation, C> source, std::span<const ColumnLayout> columns)
    { return rename(source, columns, source.data().schema_namespace); }

    template<typename C>
    urd::view<Relation, C> rename(urd::view<Relation, C> source, std::span<const urd::index<Column>> columns, size_t schema_namespace)
    {
        auto schema = urd::builder<Columns>(source.columns().span());
        schema.rename(columns);
        return rename(source, schema.span(), schema_namespace);
    }

    template<typename C>
    urd::view<Relation, C> rename(urd::view<Relation, C> source, std::span<const urd::index<Column>> columns)
    { return rename(source, columns, source.data().schema_namespace); }

    void clear() noexcept
    {
        m_symbols.clear();
        urd::clear_members(m_columns_data);
        m_row_sets.clear();
        m_rows.clear();
        m_row_indices.clear();
    }

private:
    void ensure_storage_index(urd::index<RelationRowSet> index)
    {
        while (m_storage_indices.size() <= index.value)
            m_storage_indices.push_back(m_factory.m_rows.next_index());
    }
};

const RelationRepository& get_repository(const RelationRepository& repository URD_LIFETIMEBOUND) noexcept;

// Validation stays in the raw repository members for both direct and prepared
// calls.
void prepare_for_insert(RelationRepository&, urd::data<Columns>&) noexcept;

void prepare_for_insert(RelationRepository&, urd::data<Relation>&) noexcept;

using formalism::insert;

/// Insert mutable schemas and synchronize their canonical identity.
inline auto insert(RelationRepository& repository, urd::builder<Columns>& builder) { return formalism::insert(repository, builder.get_data()); }

/// Intern a compatible mutable relation without copying its tuple storage.
inline auto insert(RelationRepository& repository, urd::builder<Relation>& builder, size_t schema_namespace = 0)
{
    auto data = urd::data<Relation>();
    data.columns_index = repository.insert(builder.columns().span()).first.index();
    data.row_set_index = repository.insert_rows(builder);
    data.schema_namespace = schema_namespace;
    auto result = formalism::insert(repository, data);
    builder.set_index(result.first.index());
    return result;
}

/// Remap repository-local schema and row identities into the destination.
template<typename C>
auto copy(urd::borrowed_view<Relation, C> source, RelationRepository& repository)
{
    auto data = urd::data<Relation>();
    data.columns_index = repository.insert(source.columns().span()).first.index();
    data.row_set_index = repository.insert_rows(source);
    data.schema_namespace = source.data().schema_namespace;
    return formalism::insert(repository, data);
}

template<typename C>
auto copy(urd::view<Relation, C> source, RelationRepository& repository)
{
    if (&get_repository(source.context()) == &repository)
        return std::pair { repository.view(source.index()), false };
    return copy(urd::resolve(source.data(), source.context()), repository);
}

template<ColumnsViewConcept V>
auto copy(const V& source, RelationRepository& repository)
{ return repository.insert(source.span()); }

}  // namespace ygg::database

// The symbol storage of schemas and relations is instantiated in the database library (docs/TEMPLATE_RULES.md, rule 9).
extern template class ygg::formalism::BasicSymbolRepository<ygg::database::Columns, false>;
extern template class ygg::formalism::BasicSymbolRepository<ygg::database::Relation, false>;

#endif
