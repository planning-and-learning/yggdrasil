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
    [[nodiscard]] QueryRepository create();
    [[nodiscard]] std::shared_ptr<QueryRepository> create_shared();
};

/// Interns typed query records. Records are prepared by prepare_for_insert
/// The erased query record followed by one concrete record per constructor tag.
using QuerySymbolTypes = ConcatTypeListsT<TypeList<Query<>>, MapTypeListT<Query, QueryConstructorTags>>;

/// (query_construction.hpp) before insertion; children precede their parents.
class QueryRepository
{
    friend class QueryRepositoryFactory;

public:
    using SymbolTypes = QuerySymbolTypes;

private:
    ApplyTypeListT<formalism::SymbolRepository, QuerySymbolTypes> m_symbols;
    QueryRepositoryFactory m_factory;
    size_t m_index;

    QueryRepository(size_t index, QueryRepositoryFactory factory);

public:
    ~QueryRepository();
    QueryRepository(const QueryRepository&) = delete;
    QueryRepository& operator=(const QueryRepository&) = delete;
    QueryRepository(QueryRepository&&) = delete;
    QueryRepository& operator=(QueryRepository&&) = delete;

    [[nodiscard]] size_t get_index() const noexcept { return m_index; }
    [[nodiscard]] auto get_factory() const noexcept { return m_factory; }
    friend const QueryRepository& get_repository(const QueryRepository& repository YGG_LIFETIMEBOUND) noexcept { return repository; }
    /// Number of root queries. Root indices are topological.
    [[nodiscard]] size_t size() const noexcept { return m_symbols.template local_size<Query<>>(); }

    template<typename T>
        requires formalism::SupportsSymbol<QueryRepository, T>
    [[nodiscard]] size_t size() const noexcept
    { return m_symbols.template local_size<T>(); }

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
    { return m_symbols.template local<T>(index); }
};

}  // namespace ygg::database

// The symbol storage of every query record type is instantiated in the database library (docs/TEMPLATE_RULES.md, rule 9).
extern template class ygg::formalism::BasicSymbolRepository<ygg::database::Query<>, false>;
extern template class ygg::formalism::BasicSymbolRepository<ygg::database::Query<ygg::database::QueryInputTag>, false>;
extern template class ygg::formalism::BasicSymbolRepository<ygg::database::Query<ygg::database::QueryEmptyTag>, false>;
extern template class ygg::formalism::BasicSymbolRepository<ygg::database::Query<ygg::database::QueryJoinTag>, false>;
extern template class ygg::formalism::BasicSymbolRepository<ygg::database::Query<ygg::database::QueryProjectTag>, false>;
extern template class ygg::formalism::BasicSymbolRepository<ygg::database::Query<ygg::database::QueryRenameTag>, false>;
extern template class ygg::formalism::BasicSymbolRepository<ygg::database::Query<ygg::database::QuerySelectEqualTag>, false>;
extern template class ygg::formalism::BasicSymbolRepository<ygg::database::Query<ygg::database::QuerySelectValueTag>, false>;
extern template class ygg::formalism::BasicSymbolRepository<ygg::database::Query<ygg::database::QueryUnionTag>, false>;
extern template class ygg::formalism::BasicSymbolRepository<ygg::database::Query<ygg::database::QueryDifferenceTag>, false>;
extern template class ygg::formalism::BasicSymbolRepository<ygg::database::Query<ygg::database::QueryDistanceTag>, false>;
extern template class ygg::formalism::BasicSymbolRepository<ygg::database::Query<ygg::database::QueryGenericJoinTag>, false>;
extern template class ygg::formalism::BasicSymbolRepository<ygg::database::Query<ygg::database::QuerySemijoinTag>, false>;
extern template class ygg::formalism::BasicSymbolRepository<ygg::database::Query<ygg::database::QueryAntijoinTag>, false>;

#endif
