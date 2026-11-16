/*
 * Copyright (C) 2025-2026 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef YGG_FORMALISM_DECLARATIONS_HPP_
#define YGG_FORMALISM_DECLARATIONS_HPP_

#include <atomic>
#include <boost/mp11.hpp>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <urd/elements/concepts.hpp>
#include <urd/elements/flat.hpp>

namespace ygg
{
/// The relation model (records in relation_data.hpp): a relation of family F is its pool (an index<F>: the layer's pool
/// of F's rows that holds its rows), its Columns (its labels with the pool's types, interned by the layer) and its
/// rows (their sorted ids there). A Schema is a list of column types, e.g. (object, object), interned once in the
/// layer's relation repository. It has two roles: every pool is declared with one (its rows' column types: the pool of
/// predicate `at` with (object, object)), and for free relations, which no symbol names, a Schema is the pool's key itself
/// (all free relations of types (object, object) share the pool keyed by that Schema).
struct Schema
{
    static constexpr std::string_view name = "Schema";
};
/// Column labels, distinct from tuple values and positions.
struct Column
{
};
struct ColumnLayout;
struct Columns
{
    static constexpr std::string_view name = "Columns";
};
template<typename F>
struct Relation
{
    static constexpr std::string_view name = "Relation";
};
/// A row of family F (predicates, actions, ..., Schema): its pool's key (relation) and its values (row_data.hpp). Stored
/// once per pool, its id there is an index<Row<F>>.
template<typename F>
struct Row
{
    static constexpr std::string_view name = "Row";
};

/// The index of a stored row: its pool's key and its id there. Its view is the urd::index_view of it (RowView), which
/// reads the row through its context (context.values(key)).
template<typename F>
struct RowIndex
{
    static constexpr std::string_view name = "RowIndex";

    urd::index<F> relation;
    urd::index<Row<F>> row;

    friend constexpr auto operator<=>(const RowIndex&, const RowIndex&) = default;
    static constexpr auto fields() noexcept { return urd::Fields<urd::field<"relation", &RowIndex::relation>, urd::field<"row", &RowIndex::row>> {}; }
    static constexpr auto identifying_members() noexcept { return urd::members_of(fields()); }
};

/// The view of a stored row through its context C (a layer, or a relation builder): an indexed record under its
/// composite index.
template<typename F, typename C>
using RowView = urd::index_view<Row<F>, C, RowIndex<F>>;

/// A relation builder of family F read through a layer C (relation_view.hpp).
template<typename F, typename C>
class RelationBuilderView;
/// A row of a relation builder of family F, read through the builder by its position (row_view.hpp).
template<typename F>
class RowBuilderView;

/// What the ids of one pool's rows give through their context C: each id as its row's view.
template<typename F, typename C>
struct RowOfId
{
    urd::index<F> relation;
    const C* context;

    auto operator()(urd::index<Row<F>> row) const { return context->view(RowIndex<F> { relation, row }); }
};

/// The rows of one pool, given by their ids, read through their context C: each id transformed into its row's view
/// (urd::transformed_range: it traverses as the ids do, and is a borrowed range). Ids that are not a borrowed view and
/// the context must outlive it; its iterators may outlive it.
template<typename F, typename Ids, typename C>
using RowsView = urd::transformed_range<Ids, RowOfId<F, C>>;

/// The rows of `relation` whose ids are `ids`, read through `context`.
template<typename F, std::ranges::forward_range Ids, typename C>
    requires std::same_as<std::remove_cvref_t<std::ranges::range_reference_t<const Ids>>, urd::index<Row<F>>>
RowsView<F, Ids, C> rows_view(urd::index<F> relation, const Ids& ids, const C& context)
{ return { ids, { relation, &context } }; }

/// The next number of a context of views (a repository layer, a relation builder): drawn once per context, process-wide,
/// never reused.
[[nodiscard]] inline std::size_t next_layer() noexcept
{
    static constinit std::atomic<std::size_t> next { 0 };
    return next.fetch_add(1, std::memory_order_relaxed);
}
}  // namespace ygg

namespace ygg::formalism
{

template<typename SymbolRepo, typename RelationRepo>
class Repository;
template<typename... Ts>
class SymbolRepository;
template<typename... Ts>
class ConcurrentSymbolRepository;
template<typename ObjectTag, typename... Ts>
class RelationRepository;
template<typename ObjectTag, typename... Ts>
class ConcurrentRelationRepository;

/// What a Repository holds, named by what it holds rather than by the stores that hold it: its symbol types
/// (Symbols<Ts...>; ConcurrentSymbols<Ts...> for concurrent interning) and its relation families with their object tag
/// (Relations<ObjectTag, Ts...>; ConcurrentRelations<ObjectTag, Ts...>). Empty: a Repository named by them is completed
/// with nothing else, so that a call with a Repository argument, looked up by argument (ADL, which completes the class
/// templates among its argument's template arguments), never instantiates its stores. The stores are the symbol and
/// relation repositories these map to (repository_impl.hpp).
template<typename... Ts>
struct Symbols
{
};
template<typename... Ts>
struct ConcurrentSymbols
{
};
template<typename ObjectTag, typename... Ts>
struct Relations
{
};
template<typename ObjectTag, typename... Ts>
struct ConcurrentRelations
{
};

namespace detail
{
/// What a Repository knows of what it holds from its template arguments alone (Symbols<...>, Relations<...>, or the
/// symbol and relation repositories themselves), so that naming a Repository instantiates no store (repository.hpp
/// declares; repository_impl.hpp defines); Store is the repository that holds them.
template<typename Held>
struct symbol_repository_traits;
template<typename... Ts>
struct symbol_repository_traits<Symbols<Ts...>>
{
    using SymbolTypes = boost::mp11::mp_list<Ts...>;
    static constexpr bool thread_safe = false;
    using Store = SymbolRepository<Ts...>;
};
template<typename... Ts>
struct symbol_repository_traits<ConcurrentSymbols<Ts...>>
{
    using SymbolTypes = boost::mp11::mp_list<Ts...>;
    static constexpr bool thread_safe = true;
    using Store = ConcurrentSymbolRepository<Ts...>;
};
template<typename... Ts>
struct symbol_repository_traits<SymbolRepository<Ts...>> : symbol_repository_traits<Symbols<Ts...>>
{
};
template<typename... Ts>
struct symbol_repository_traits<ConcurrentSymbolRepository<Ts...>> : symbol_repository_traits<ConcurrentSymbols<Ts...>>
{
};
template<typename Held>
struct relation_repository_traits;
template<typename ObjectTag, typename... Ts>
struct relation_repository_traits<Relations<ObjectTag, Ts...>>
{
    using object_tag = ObjectTag;
    using RelationTypes = boost::mp11::mp_list<Ts...>;
    static constexpr bool thread_safe = false;
    using Store = RelationRepository<ObjectTag, Ts...>;
};
template<typename ObjectTag, typename... Ts>
struct relation_repository_traits<ConcurrentRelations<ObjectTag, Ts...>>
{
    using object_tag = ObjectTag;
    using RelationTypes = boost::mp11::mp_list<Ts...>;
    static constexpr bool thread_safe = true;
    using Store = ConcurrentRelationRepository<ObjectTag, Ts...>;
};
template<typename ObjectTag, typename... Ts>
struct relation_repository_traits<RelationRepository<ObjectTag, Ts...>> : relation_repository_traits<Relations<ObjectTag, Ts...>>
{
};
template<typename ObjectTag, typename... Ts>
struct relation_repository_traits<ConcurrentRelationRepository<ObjectTag, Ts...>> : relation_repository_traits<ConcurrentRelations<ObjectTag, Ts...>>
{
};
}  // namespace detail

/// Whether a repository's symbol inventory contains T.
template<typename Repository, typename T>
concept SupportsSymbol = boost::mp11::mp_contains<typename std::remove_cvref_t<Repository>::SymbolTypes, T>::value;

/// Whether a repository has pools of family F's rows.
template<typename Repository, typename F>
concept HasRows = boost::mp11::mp_contains<typename std::remove_cvref_t<Repository>::RelationTypes, F>::value;

/// Whether a repository has relations of family F (Relation<F> is among its symbols).
template<typename Repository, typename F>
concept HasRelations = SupportsSymbol<Repository, Relation<F>>;

template<typename Tag>
struct Object
{
};
}  // namespace ygg::formalism

#endif
