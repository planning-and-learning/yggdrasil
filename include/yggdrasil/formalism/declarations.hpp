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

#include <urd/elements/flat.hpp>

#include <atomic>
#include <boost/mp11.hpp>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <tuple>
#include <type_traits>
#include <urd/elements/concepts.hpp>

namespace ygg
{
/// The relation model (records in relation_data.hpp): a relation of family F is its pool (an index<F>: the layer's pool
/// of F's rows that holds its rows), its Columns (its labels with the pool's types, interned by the layer) and its
/// rows (their sorted ids there). A Schema is a list of column types, interned once in the layer's
/// relation repository: every pool has one (its column types), and a Schema keys the pools of free relations.
struct Schema
{
};
/// Column labels, distinct from tuple values and positions.
struct Column
{
};
struct ColumnLayout;
struct Columns
{
};
template<typename F>
struct Relation
{
};
/// A row of family F (predicates, actions, ..., Schema): its pool's key (relation) and its values (row_data.hpp). Stored
/// once per pool, its id there is an index<Row<F>>.
template<typename F>
struct Row
{
};

/// The key of a stored row: its pool's key and its id there. Its view is the urd::owning_view of the key (RowView),
/// which reads the row through its context (context[key]).
template<typename F>
struct RowIndex
{
    urd::index<F> relation;
    urd::index<Row<F>> row;

    friend constexpr auto operator<=>(const RowIndex&, const RowIndex&) = default;
    static constexpr auto identifying_members() noexcept { return urd::Members<&RowIndex::relation, &RowIndex::row> {}; }
};

/// The view of a stored row through its context C (a layer, or a relation builder): its key held by value.
template<typename F, typename C>
using RowView = urd::owning_view<Row<F>, C, RowIndex<F>>;

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

/// What tells relations apart while their rows stay unchanged: the number of their context (a layer's or a builder's,
/// one process-wide counter, next_layer) and, for an interned relation, its index (none for a builder).
struct RelationIdentity
{
    std::size_t context;
    std::uint32_t relation = urd::index<void>::none_value;

    friend bool operator==(const RelationIdentity&, const RelationIdentity&) = default;
    static constexpr auto identifying_members() noexcept { return urd::Members<&RelationIdentity::context, &RelationIdentity::relation> {}; }
};

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
template<typename ObjectTag, typename... Ts>
class RelationRepository;

/// Whether a repository's symbol inventory contains T.
template<typename Repository, typename T>
concept SupportsSymbol = boost::mp11::mp_contains<typename std::remove_cvref_t<Repository>::SymbolTypes, T>::value;

/// Whether a repository has pools of family F's rows.
template<typename Repository, typename F>
concept HasRows = boost::mp11::mp_contains<typename std::remove_cvref_t<Repository>::RelationTypes, F>::value;

/// Whether a repository has relations of family F (Relation<F> is among its symbols).
template<typename Repository, typename F>
concept HasRelations = SupportsSymbol<Repository, Relation<F>>;

/// Read access to one supported symbol representation.
template<typename Repository, typename T>
concept SymbolRepositoryFor = SupportsSymbol<Repository, T> && requires(const std::remove_reference_t<Repository>& repository, urd::index<T> index) {
    { repository[index] } -> std::same_as<const urd::data<T>&>;
};

template<typename C, typename T>
concept SymbolContextFor = requires(const C& context) {
    { get_repository(context) } -> SymbolRepositoryFor<T>;
    get_repository(context).layer();
};

template<typename Tag>
struct Object
{
};
}  // namespace ygg::formalism

#endif
