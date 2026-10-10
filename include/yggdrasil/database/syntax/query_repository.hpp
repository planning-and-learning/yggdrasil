/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_SYNTAX_QUERY_REPOSITORY_HPP_
#define YGG_DATABASE_SYNTAX_QUERY_REPOSITORY_HPP_

#include "yggdrasil/database/syntax/query_view.hpp"
#include "yggdrasil/formalism/declarations.hpp"
#include "yggdrasil/formalism/symbol_repository.hpp"

#include <cassert>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

namespace ygg::database
{
/// Copies share one identity sequence. Repositories whose views are compared
/// or hashed together must come from the same factory.
template<ColumnTypes Values>
class QueryRepositoryFactory
{
    std::shared_ptr<size_t> m_next_index = std::make_shared<size_t>(0);

    size_t next_index()
    {
        if (*m_next_index == std::numeric_limits<size_t>::max())
            throw std::overflow_error("QueryRepositoryFactory: index space exhausted.");
        return (*m_next_index)++;
    }

public:
    [[nodiscard]] QueryRepository<Values> create();
    [[nodiscard]] std::shared_ptr<QueryRepository<Values>> create_shared();
};

/// Interns typed query records. Records are prepared by prepare_for_insert
/// (query_construction.hpp) before insertion; children precede their parents.
template<ColumnTypes Values>
class QueryRepository
{
    friend class QueryRepositoryFactory<Values>;
    template<typename Tag>
    using ConcreteQuery = Query<Values, Tag>;

public:
    using SymbolTypes = ConcatTypeListsT<TypeList<Query<Values>>, MapTypeListT<ConcreteQuery, QueryConstructorTags>>;
    using SymbolRepository = ApplyTypeListT<formalism::SymbolRepository, SymbolTypes>;

private:
    SymbolRepository m_symbols;
    QueryRepositoryFactory<Values> m_factory;
    size_t m_index;

    QueryRepository(size_t index, QueryRepositoryFactory<Values> factory) : m_factory(std::move(factory)), m_index(index) {}

public:
    QueryRepository(const QueryRepository&) = delete;
    QueryRepository& operator=(const QueryRepository&) = delete;
    QueryRepository(QueryRepository&&) = delete;
    QueryRepository& operator=(QueryRepository&&) = delete;

    size_t get_index() const noexcept { return m_index; }
    auto get_factory() const noexcept { return m_factory; }
    /// Number of root queries. Root indices are topological.
    size_t size() const noexcept { return m_symbols.template local_size<Query<Values>>(); }

    template<typename T>
        requires formalism::SupportsSymbol<QueryRepository, T>
    size_t size() const noexcept
    {
        return m_symbols.template local_size<T>();
    }

    template<typename T>
        requires formalism::SupportsSymbol<QueryRepository, T>
    std::optional<View<Index<T>, QueryRepository>> find(const Data<T>& data) const noexcept
    {
        if (const auto index = m_symbols.template find_local<T>(data))
            return View<Index<T>, QueryRepository>(*index, *this);
        return std::nullopt;
    }

    template<typename T>
        requires formalism::SupportsSymbol<QueryRepository, T>
    std::pair<View<Index<T>, QueryRepository>, bool> insert(Data<T>& data)
    {
        const auto [index, created] = m_symbols.template insert_local<T>(data);
        data.index = index;
        return { View<Index<T>, QueryRepository>(index, *this), created };
    }

    template<typename T>
        requires formalism::SupportsSymbol<QueryRepository, T>
    const Data<T>& operator[](Index<T> index) const noexcept
    {
        assert(index.get_value() < size<T>());
        return m_symbols.template at_local<T>(index);
    }
};

template<ColumnTypes Values>
QueryRepository<Values> QueryRepositoryFactory<Values>::create()
{
    return QueryRepository<Values>(next_index(), *this);
}

template<ColumnTypes Values>
std::shared_ptr<QueryRepository<Values>> QueryRepositoryFactory<Values>::create_shared()
{
    return std::shared_ptr<QueryRepository<Values>>(new QueryRepository<Values>(next_index(), *this));
}
}  // namespace ygg::database

#endif
