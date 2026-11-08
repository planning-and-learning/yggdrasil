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

#include <boost/mp11.hpp>
#include <concepts>
#include <ranges>
#include <tuple>
#include <type_traits>
#include <urd/elements/concepts.hpp>

namespace ygg
{
/// The relation model (records in relation_data.hpp): a relation of family F is its pool (an index<F>: the layer's pool
/// of F's rows that holds its rows), its Columns (labels and types) and its RowSet (the sorted ids of its rows there).
/// A Schema (the column types) keys the pools of free relations.
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
struct RowSet
{
};
template<typename F>
struct Relation
{
};
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

/// Whether a repository's relation inventory contains a relation tag.
template<typename Repository, typename T>
concept SupportsRelation = boost::mp11::mp_contains<typename std::remove_cvref_t<Repository>::RelationTypes, T>::value;

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

struct Row
{
};

template<typename RelationTag_, typename ObjectTag_>
struct RelationBinding
{
    using relation_tag = RelationTag_;
    using object_tag = ObjectTag_;
};

/// The key of a published relation binding: its relation and its row in that relation's table. Its view is the
/// urd::owning_view of the key (RelationBindingView); the context gives the row (context[key]).
template<typename RelationTag, typename ObjectTag>
struct RelationBindingIndex
{
    urd::index<RelationTag> relation;
    urd::index<Row> row;

    friend constexpr auto operator<=>(const RelationBindingIndex&, const RelationBindingIndex&) = default;
    static constexpr auto identifying_members() noexcept { return urd::Members<&RelationBindingIndex::relation, &RelationBindingIndex::row> {}; }
};

/// The binding a key identifies.
template<typename Key>
struct binding_of;

template<typename RelationTag, typename ObjectTag>
struct binding_of<RelationBindingIndex<RelationTag, ObjectTag>>
{
    using type = RelationBinding<RelationTag, ObjectTag>;
};

/// Read access to a supported binding's row representation.
template<typename Repository, typename RelationTag, typename ObjectTag>
concept RelationRepositoryFor = SupportsRelation<Repository, RelationTag>
                                && requires(const std::remove_reference_t<Repository>& repository, ygg::formalism::RelationBindingIndex<RelationTag, ObjectTag> index) {
                                       { repository[index].relation } -> std::convertible_to<urd::index<RelationTag>>;
                                       requires std::ranges::input_range<decltype(repository[index].objects)>
                                                && std::same_as<std::ranges::range_value_t<decltype(repository[index].objects)>, urd::index<Object<ObjectTag>>>;
                                   };

template<typename C, typename RelationTag, typename ObjectTag>
concept RelationContextFor = requires(const std::remove_reference_t<C>& context) {
    { get_repository(context) } -> RelationRepositoryFor<RelationTag, ObjectTag>;
};


/// A published binding as its context gives it (context[RelationBindingIndex]): its relation and its row of objects,
/// the shape of a binding's record, so that one mixin reads both (binding_view.hpp).
template<typename RelationTag, typename Objects>
struct RelationBindingRow
{
    urd::index<RelationTag> relation;
    Objects objects;
};

/// The view of a published binding through its context C: its key H (a RelationBindingIndex) held by value.
template<typename H, typename C>
using RelationBindingView = urd::owning_view<typename binding_of<H>::type, C, H>;

/// What the rows of one relation's bindings give through their context C: each row as the view of its binding.
template<typename RelationTag, typename ObjectTag, typename C>
struct RelationBindingOfRow
{
    urd::index<RelationTag> relation;
    const C* context;

    auto operator()(urd::index<Row> row) const { return context->view(RelationBindingIndex<RelationTag, ObjectTag> { relation, row }); }
};

/// The bindings of one relation, stored as its rows, read through their context C: each row transformed into its
/// binding's view (urd::transformed_range: it traverses as the rows do, and is a borrowed range). Rows that are not a
/// borrowed view and the context must outlive it; its iterators may outlive it.
template<typename RelationTag, typename ObjectTag, typename Rows, typename C>
using RelationBindingsView = urd::transformed_range<Rows, RelationBindingOfRow<RelationTag, ObjectTag, C>>;

/// The bindings of `relation` stored as `rows`, read through `context`.
template<typename ObjectTag, typename RelationTag, std::ranges::forward_range Rows, typename C>
    requires std::same_as<std::remove_cvref_t<std::ranges::range_reference_t<const Rows>>, urd::index<Row>>
RelationBindingsView<RelationTag, ObjectTag, Rows, C> bindings_view(urd::index<RelationTag> relation, const Rows& rows, const C& context)
{ return { rows, { relation, &context } }; }
}  // namespace ygg::formalism

#endif
