/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_SYNTAX_QUERY_REPOSITORY_HPP_
#define YGG_DATABASE_SYNTAX_QUERY_REPOSITORY_HPP_

#include "yggdrasil/core/index_sequence.hpp"
#include "yggdrasil/database/syntax/query_view.hpp"
#include "yggdrasil/formalism/declarations.hpp"
#include "yggdrasil/formalism/symbol_repository.hpp"

#include <cassert>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <urd/config.hpp>

namespace ygg::database
{
/// Copies share one identity sequence. Repositories whose views are compared
/// or hashed together must come from the same factory.
class QueryRepositoryFactory
{
    IndexSequence m_indices;

public:
    [[nodiscard]] QueryRepository create();
    [[nodiscard]] std::shared_ptr<QueryRepository> create_shared();
};

/// Interns typed query records. Records are prepared by prepare_for_insert
/// The erased query record followed by one concrete record per constructor tag.
using QuerySymbolTypes = boost::mp11::mp_append<boost::mp11::mp_list<Query<>>, boost::mp11::mp_transform<Query, QueryConstructorTags>>;

/// (query_construction.hpp) before insertion; children precede their parents.
class QueryRepository
{
    friend class QueryRepositoryFactory;

public:
    using SymbolTypes = QuerySymbolTypes;

private:
    boost::mp11::mp_apply<formalism::SymbolRepository, QuerySymbolTypes> m_symbols;
    QueryRepositoryFactory m_factory;
    size_t m_index;

    QueryRepository(size_t index, QueryRepositoryFactory factory);

public:
    ~QueryRepository();

    /// An index as its view here (urd's resolves contract): what compositions of indices are read through (urd::resolve).
    template<typename T>
    [[nodiscard]] urd::index_view<T, QueryRepository> view(urd::index<T> index) const noexcept
    { return { index, this }; }
    QueryRepository(const QueryRepository&) = delete;
    QueryRepository& operator=(const QueryRepository&) = delete;
    QueryRepository(QueryRepository&&) = delete;
    QueryRepository& operator=(QueryRepository&&) = delete;

    /// The repository's number, as urd's views ask of a context.
    [[nodiscard]] std::size_t layer() const noexcept { return m_index; }
    [[nodiscard]] auto get_factory() const noexcept { return m_factory; }
    friend const QueryRepository& get_repository(const QueryRepository& repository URD_LIFETIMEBOUND) noexcept { return repository; }
    /// Number of root queries. Root indices are topological.
    [[nodiscard]] size_t size() const noexcept { return m_symbols.template size<Query<>>(); }

    template<typename T>
        requires formalism::SupportsSymbol<QueryRepository, T>
    [[nodiscard]] size_t size() const noexcept
    { return m_symbols.template size<T>(); }

    template<typename T>
        requires formalism::SupportsSymbol<QueryRepository, T>
    std::optional<urd::index_view<T, QueryRepository>> find(const urd::data<T>& data) const noexcept
    {
        if (const auto index = m_symbols.template get<T>().find(data); !index.is_none())
            return urd::index_view<T, QueryRepository>(index, this);
        return std::nullopt;
    }

    template<typename T>
        requires formalism::SupportsSymbol<QueryRepository, T>
    std::pair<urd::index_view<T, QueryRepository>, bool> insert(urd::data<T>& data)
    {
        const auto [index, created] = formalism::intern(m_symbols.template get<T>(), data);
        return { urd::index_view<T, QueryRepository>(index, this), created };
    }

    template<typename T>
        requires formalism::SupportsSymbol<QueryRepository, T>
    const urd::data<T>& operator[](urd::index<T> index) const noexcept
    { return m_symbols[index]; }
};

}  // namespace ygg::database

#endif
