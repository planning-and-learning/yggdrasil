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
#include <urd/elements/builder.hpp>
#include <urd/elements/concepts.hpp>
#include <urd/elements/data.hpp>
#include <urd/elements/flat.hpp>
#include <urd/elements/index.hpp>
#include <urd/tags.hpp>

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
    static constexpr std::string_view name = "Column";
};
struct ColumnLayout;

struct Columns
{
    static constexpr std::string_view name = "Columns";
};

template<typename F>
struct Relation
{
    static constexpr std::string_view name = urd::joined_name<"Relation", F::name>;
};

/// A row of family F (predicates, actions, ..., Schema): its values, stored once per pool, its relation's (its key's
/// first part), its id there an index<Row<F>>. Held three ways (row_data.hpp): packed in its pool's slot (PackedRow<F>,
/// what a lookup hands out), owned outside it (data<Row<F>>: its values), and built (builder<Row<F>, C>: its relation
/// and its values).
template<typename F>
struct Row
{
    static constexpr std::string_view name = urd::joined_name<"Row", F::name>;
};

/// A row as its pool's slot holds it, read on demand (row_data.hpp).
template<typename F>
class PackedRow;
}  // namespace ygg

/// What a lookup of a row hands out (urd::record_at, a repository's operator[] and at()): a reader of the row packed in
/// its slot, by value.
template<typename F>
struct urd::stored_ref_of<ygg::Row<F>>
{
    using type = ygg::PackedRow<F>;
};

/// A row is numbered within its relation's pool: its key is its relation's index, then its own there.
template<typename F>
struct urd::key_of<ygg::Row<F>>
{
    using type = urd::key<F, ygg::Row<F>>;
};

namespace ygg
{

/// The view of a stored row through its context C (a layer, or a relation builder): an indexed record under its key,
/// urd::key<F, Row<F>> (its relation's index, then its row's there).
template<typename F, typename C>
using RowView = urd::index_view<Row<F>, C, urd::key<F, Row<F>>>;

namespace formalism
{
/// The key a repository interns a record of T under (urd::key_of): its index, urd::index<T>; a row's, urd::key<F, Row<F>>.
template<typename T>
using key_t = urd::key_t<T>;
}  // namespace formalism

/// A relation builder of family F read through a context C: its borrowed view, with the relation mixin
/// (relation_view.hpp).
template<typename F, typename C>
using RelationBuilderView = urd::borrowed_view<Relation<F>, C, urd::builder<Relation<F>, C>>;
/// A row builder of family F read through a context C: its borrowed view, with the row mixin (row_view.hpp).
template<typename F, typename C>
using RowBuilderView = urd::borrowed_view<Row<F>, C, urd::builder<Row<F>, C>>;

/// What the ids of one pool's rows give through their context C: each id as its row's view.
template<typename F, typename C>
struct RowOfId
{
    urd::index<F> relation;
    const C* context;

    auto operator()(urd::index<Row<F>> row) const { return context->view(urd::key<F, Row<F>> { relation, row }); }
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

template<bool ThreadSafe, typename... Ts>
class SymbolStores;
template<typename... Ts>
class RelationRepository;
template<typename... Ts>
class ConcurrentRelationRepository;

/// What a Repository holds, named by what it holds rather than by the stores that hold it: its symbol types
/// (Symbols<Ts...>; ConcurrentSymbols<Ts...> for concurrent interning) and its relation families (Relations<Ts...>;
/// ConcurrentRelations<Ts...>). Empty: a Repository named by them is completed
/// with nothing else, so that a call with a Repository argument, looked up by argument (ADL, which completes the class
/// templates among its argument's template arguments), never instantiates its stores. The stores are the symbol and
/// relation repositories these map to (repository_impl.hpp).
template<typename... Ts>
struct Symbols
{
    using List = Symbols;
};

template<typename... Ts>
struct ConcurrentSymbols
{
    using List = ConcurrentSymbols;
};

template<typename... Ts>
struct Relations
{
    using List = Relations;
};

template<typename... Ts>
struct ConcurrentRelations
{
    using List = ConcurrentRelations;
};

/// Whether Held names a list it is not: a library's class deriving from Symbols<...>, ConcurrentSymbols<...>,
/// Relations<...> or ConcurrentRelations<...>, so that a Repository named by it, and everything that names that Repository,
/// spells its short name instead of every type of the list (the list's types, spelled out, are most of the names of its
/// symbols and of their debug info). Its traits are its list's.
template<typename Held>
concept NamesList = requires { typename Held::List; } && !std::same_as<Held, typename Held::List>;

/// A repository of the symbols SymbolRepo and the relations RelationRepo: each a class of its own deriving from
/// Symbols<...> or ConcurrentSymbols<...>, Relations<...> or ConcurrentRelations<...> (NamesList), so that the names of
/// everything over it spell their names, not every one of its types (brokkr's template rule T20).
template<NamesList SymbolRepo, NamesList RelationRepo>
class Repository;

namespace detail
{
/// The slot of the rows of relation symbol F in one layer (relation_repository.hpp): what a Repository's relations are
/// written through.
template<typename F, typename Rows>
class RowSlot;

/// What a Repository knows of what it holds from its template arguments alone (Symbols<...>, Relations<...>, or the
/// symbol and relation repositories themselves), so that naming a Repository instantiates no store (repository.hpp
/// declares; repository_impl.hpp defines); Store is the repository that holds them.
template<typename Held>
struct symbol_repository_traits;

template<typename... Ts>
struct symbol_repository_traits<Symbols<Ts...>>
{
    using SymbolTypes = boost::mp11::mp_list<Ts...>;
    /// The keys it resolves (repository[key]): each symbol's index.
    using KeyTypes = boost::mp11::mp_list<urd::index<Ts>...>;
    static constexpr bool thread_safe = false;
    /// The store that holds them (symbol_repository.hpp): one layered pool per kind.
    using Store = SymbolStores<false, Ts...>;
};

template<typename... Ts>
struct symbol_repository_traits<ConcurrentSymbols<Ts...>>
{
    using SymbolTypes = boost::mp11::mp_list<Ts...>;
    using KeyTypes = boost::mp11::mp_list<urd::index<Ts>...>;
    static constexpr bool thread_safe = true;
    using Store = SymbolStores<true, Ts...>;
};

template<NamesList Held>
struct symbol_repository_traits<Held> : symbol_repository_traits<typename Held::List>
{
};

template<typename Held>
struct relation_repository_traits;

template<NamesList Held>
struct relation_repository_traits<Held> : relation_repository_traits<typename Held::List>
{
};

template<typename... Ts>
struct relation_repository_traits<Relations<Ts...>>
{
    using RelationTypes = boost::mp11::mp_list<Ts...>;
    /// The keys it resolves (repository[key]): a Schema's index, and each family's rows' keys.
    using KeyTypes = boost::mp11::mp_list<urd::index<Schema>, urd::key<Ts, Row<Ts>>...>;
    static constexpr bool thread_safe = false;
    using Store = RelationRepository<Ts...>;
};

template<typename... Ts>
struct relation_repository_traits<ConcurrentRelations<Ts...>>
{
    using RelationTypes = boost::mp11::mp_list<Ts...>;
    using KeyTypes = boost::mp11::mp_list<urd::index<Schema>, urd::key<Ts, Row<Ts>>...>;
    static constexpr bool thread_safe = true;
    using Store = ConcurrentRelationRepository<Ts...>;
};

}  // namespace detail

/// Whether a repository's symbol inventory contains T.
template<typename Repository, typename T>
concept SupportsSymbol = boost::mp11::mp_contains<typename std::remove_cvref_t<Repository>::SymbolTypes, T>::value;

/// Whether a repository has pools of family F's rows.
template<typename Repository, typename F>
concept HasRows = boost::mp11::mp_contains<typename std::remove_cvref_t<Repository>::RelationTypes, F>::value;

/// Whether a repository names the keys it resolves (KeyTypes: repository[key] is the record of the key's last tag).
template<typename R>
concept DeclaresKeys = requires { typename std::remove_cvref_t<R>::KeyTypes; };

/// Whether a repository resolves keys of type K itself (repository[key]: one of its KeyTypes).
template<typename R, typename K>
concept HoldsKey = DeclaresKeys<R> && boost::mp11::mp_contains<typename std::remove_cvref_t<R>::KeyTypes, K>::value;

/// Whether a repository holds records of T itself: it resolves their key (HoldsKey of key_t<T>), whatever their kind (a
/// symbol, a Schema, a row).
template<typename Repository, typename T>
concept HoldsRecords = HoldsKey<Repository, key_t<T>>;

/// The kind a key names: its last tag (urd::key<..., T>::element_type).
template<typename K>
using kind_of_key = typename K::element_type;

/// A record's canonical form: as built, unless its domain says more by an overload of its own for its kind (found by ADL
/// through T's namespace; more specialized, so it takes precedence).
template<typename T>
void canonicalize(urd::data<T>& /*record*/) noexcept
{
}

/// Whether a record is in its canonical form: always, unless its domain says more (as canonicalize).
template<typename T>
bool is_canonical(const urd::data<T>& /*record*/) noexcept
{ return true; }

/// What a repository's one insert and find run on a record first (prepare_for_insert, found by ADL): its canonical form.
/// A domain whose preparation does more says so by an overload of its own (more specialized, so it takes precedence).
template<typename Repository, typename T>
void prepare_for_insert(const Repository& /*repository*/, urd::data<T>& record)
{ canonicalize(record); }

/// Whether a repository nests others (Nests, nested.hpp): it names its NestedRepositories.
template<typename R>
concept NestsRepositories = requires { typename R::NestedRepositories; };

/// An object: what the object columns of rows hold (an urd::index<Object>), the same type in every library that stores
/// objects (tyr's records of objects are urd::data<ygg::formalism::Object>).
struct Object
{
    static constexpr std::string_view name = "Object";
};

}  // namespace ygg::formalism

#endif
