/*
 * Copyright (C) 2025-2026 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef YGG_FORMALISM_REPOSITORY_HPP_
#define YGG_FORMALISM_REPOSITORY_HPP_

#include "yggdrasil/formalism/builder.hpp"
#include "yggdrasil/formalism/declarations.hpp"
#include "yggdrasil/formalism/interning.hpp"
#include "yggdrasil/formalism/keyed.hpp"
#include "yggdrasil/formalism/relation_data.hpp"
#include "yggdrasil/formalism/relation_view.hpp"
#include "yggdrasil/formalism/row_view.hpp"

#include <array>
#include <boost/mp11.hpp>
#include <cstddef>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <tuple>
#include <urd/config.hpp>
#include <urd/elements/view.hpp>
#include <utility>
#include <vector>

namespace ygg::formalism
{

/// A repository layer, the context of its views: symbols and rows, over the layers of a parent, whose records it finds
/// too. A view's context is the layer that owns its record (its canonical context, found through urd's owner), so that
/// views of one record are equal wherever they were made. Each layer has its own number.
///
/// Every relation family F (HasRows: predicates, actions, ..., Schema) has rows Row<F>, keyed {pool, row}. A family F
/// with relations (Relation<F> among the layer's symbols, with Columns) also has relations: a
/// relation's rows go into the layer's pool of its key (an index<F>), and the relation is {pool, columns, row ids}.
/// Rename keeps the pool and the rows. Free relations are the Schema family: their key is the Schema of their
/// column types, inserted as any record.
///
/// Concurrent repository aliases support overlapping find, insert and lookups. Builders remain thread-confined; clear,
/// memory accounting, and destruction require quiescence. Parent layers must remain frozen and outlive their
/// descendants.
///
/// This header declares the repository; its stores (the symbol and relation repositories, the builders) are behind a
/// pointer to its Impl, which repository_impl.hpp defines with every member. A library includes repository_impl.hpp in
/// one source and instantiates its repositories' members there (repository_instantiations.hpp); its other sources
/// include this header only, and instantiate none of the stores.
template<typename SymbolRepo, typename RelationRepo>
class Repository : public KeyedRepository<Repository<SymbolRepo, RelationRepo>>
{
public:
    using SymbolTypes = typename detail::symbol_repository_traits<SymbolRepo>::SymbolTypes;
    using RelationTypes = typename detail::relation_repository_traits<RelationRepo>::RelationTypes;
    /// The keys it resolves (operator[]): its symbols' and its relation repository's.
    using KeyTypes = boost::mp11::mp_append<typename detail::symbol_repository_traits<SymbolRepo>::KeyTypes,
                                            typename detail::relation_repository_traits<RelationRepo>::KeyTypes>;
    /// Its own parts, behind its Impl, as read (Writable false) or written into (key_parts): its symbol repository, then
    /// its relation repository (own_parts()). It nests no repository.
    template<bool Writable>
    using OwnParts = std::tuple<HeldPart<detail::access_t<Writable, Repository>,
                                         detail::access_t<Writable, typename detail::symbol_repository_traits<SymbolRepo>::Store>,
                                         typename detail::symbol_repository_traits<SymbolRepo>::KeyTypes>,
                                HeldPart<detail::access_t<Writable, Repository>,
                                         detail::access_t<Writable, typename detail::relation_repository_traits<RelationRepo>::Store>,
                                         typename detail::relation_repository_traits<RelationRepo>::KeyTypes>>;
    /// Whether finds, inserts and lookups may overlap (its symbols and its relations concurrent).
    static constexpr bool thread_safe =
        detail::symbol_repository_traits<SymbolRepo>::thread_safe && detail::relation_repository_traits<RelationRepo>::thread_safe;

    /// The stores: defined in repository_impl.hpp.
    struct Impl;

    friend const Repository& get_repository(const Repository& repository URD_LIFETIMEBOUND) noexcept { return repository; }

    /// A layer over `parent` (none: a root). Parent layers must remain frozen and outlive this one.
    explicit Repository(const Repository* parent = nullptr);
    Repository(const Repository& other) = delete;
    Repository& operator=(const Repository& other) = delete;
    Repository(Repository&& other) = delete;
    Repository& operator=(Repository&& other) = delete;
    ~Repository();

    [[nodiscard]] const Repository* parent() const noexcept { return m_parent; }

    /// The layer's number, as urd's views ask of a context.
    [[nodiscard]] std::size_t layer() const noexcept { return m_layer; }

    /// Forgets this layer's own records and rows; the parent's stay. Views of the forgotten records
    /// dangle. Its pool keys (e.g. predicates) stay, with their pools' declarations (Schemas), and so their pools, emptied: a refill in any order finds each
    /// pool built alike and allocates nothing.
    void clear();
    /// Forgets every record and row of this layer, its pool keys and their pools too; the parent's stay.
    void clear_all();

    // Every key (a symbol's index, a Schema's, a row's key) is checked, owned and viewed by the one contains(key),
    // owner(key) and view(key) of KeyedRepository: a view's context is the layer that owns its record.

    /**
     * Symbols: found in this layer and its parents.
     */

    /// Every record (a symbol, a Schema, a row under its relation) is stored and found by the one insert and find of
    /// KeyedRepository (of a record, a builder, or a view), through the part that holds its kind; its record's key
    /// written into it.
private:
    friend class KeyedRepository<Repository>;

    /// Its own parts as read: what KeyedRepository resolves a key through.
    [[nodiscard]] OwnParts<false> own_parts() const;

    /// Its own parts as written into: what KeyedRepository stores a record through.
    [[nodiscard]] OwnParts<true> own_parts();

    // What a library instantiates: the constrained members above, unconstrained, which they forward to. Clang names an
    // instantiation's requires-clause in its symbol and g++ does not, so only unconstrained symbols link between code
    // built by either.

    const Repository* m_parent;
    std::size_t m_layer;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace ygg::formalism

#endif
