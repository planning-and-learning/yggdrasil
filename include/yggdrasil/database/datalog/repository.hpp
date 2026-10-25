/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_DATALOG_REPOSITORY_HPP_
#define YGG_DATABASE_DATALOG_REPOSITORY_HPP_

#include "yggdrasil/database/datalog/view.hpp"
#include "yggdrasil/formalism/declarations.hpp"
#include "yggdrasil/formalism/symbol_repository.hpp"

#include <cassert>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

namespace ygg::database::datalog
{
/// Copies share one identity sequence. Repositories whose views are compared
/// or hashed together must come from the same factory.
class DatalogRepositoryFactory
{
    std::shared_ptr<size_t> m_next_index = std::make_shared<size_t>(0);

    size_t next_index()
    {
        if (*m_next_index == std::numeric_limits<size_t>::max())
            throw std::overflow_error("DatalogRepositoryFactory: index space exhausted.");
        return (*m_next_index)++;
    }

public:
    [[nodiscard]] DatalogRepository create();
    [[nodiscard]] std::shared_ptr<DatalogRepository> create_shared();
};

/// Interns Datalog records. Records are validated by prepare_for_insert (construction.hpp) before insertion; children
/// precede their parents.
class DatalogRepository
{
    friend class DatalogRepositoryFactory;

public:
    using SymbolTypes = DatalogSymbolTypes;

private:
    ApplyTypeListT<formalism::SymbolRepository, DatalogSymbolTypes> m_symbols;
    DatalogRepositoryFactory m_factory;
    size_t m_index;

    DatalogRepository(size_t index, DatalogRepositoryFactory factory);

public:
    ~DatalogRepository();
    DatalogRepository(const DatalogRepository&) = delete;
    DatalogRepository& operator=(const DatalogRepository&) = delete;
    DatalogRepository(DatalogRepository&&) = delete;
    DatalogRepository& operator=(DatalogRepository&&) = delete;

    [[nodiscard]] size_t get_index() const noexcept { return m_index; }
    [[nodiscard]] auto get_factory() const noexcept { return m_factory; }
    friend const DatalogRepository& get_repository(const DatalogRepository& repository YGG_LIFETIMEBOUND) noexcept { return repository; }
    template<typename T>
        requires formalism::SupportsSymbol<DatalogRepository, T>
    [[nodiscard]] size_t size() const noexcept
    { return m_symbols.template local_size<T>(); }

    template<typename T>
        requires formalism::SupportsSymbol<DatalogRepository, T>
    std::optional<View<Index<T>, DatalogRepository>> find(const Data<T>& data) const noexcept
    {
        if (const auto index = m_symbols.template find_local<T>(data))
            return View<Index<T>, DatalogRepository>(*index, *this);
        return std::nullopt;
    }

    template<typename T>
        requires formalism::SupportsSymbol<DatalogRepository, T>
    std::pair<View<Index<T>, DatalogRepository>, bool> insert(Data<T>& data)
    {
        const auto [index, created] = m_symbols.template insert_local<T>(data);
        data.index = index;
        return { View<Index<T>, DatalogRepository>(index, *this), created };
    }

    template<typename T>
        requires formalism::SupportsSymbol<DatalogRepository, T>
    const Data<T>& operator[](Index<T> index) const noexcept
    { return m_symbols.template local<T>(index); }
};

}  // namespace ygg::database::datalog

// The symbol storage of every Datalog record type is instantiated in the database library (docs/TEMPLATE_RULES.md, rule 9).
extern template class ygg::formalism::BasicSymbolRepository<ygg::database::datalog::Relation, false>;
extern template class ygg::formalism::BasicSymbolRepository<ygg::database::datalog::Atom, false>;
extern template class ygg::formalism::BasicSymbolRepository<ygg::database::datalog::Aggregate, false>;
extern template class ygg::formalism::BasicSymbolRepository<ygg::database::datalog::Rule, false>;
extern template class ygg::formalism::BasicSymbolRepository<ygg::database::datalog::Program, false>;

#endif
