/*
 * Copyright (C) 2026 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef YGG_DATABASE_OPERATIONS_HPP_
#define YGG_DATABASE_OPERATIONS_HPP_

#include "yggdrasil/database/join_index.hpp"
#include "yggdrasil/database/plans.hpp"
#include "yggdrasil/database/relation_view.hpp"
#include "yggdrasil/semantics/equal_to.hpp"
#include "yggdrasil/semantics/hash.hpp"

#include <concepts>
#include <cstddef>
#include <initializer_list>
#include <span>
#include <type_traits>
#include <vector>

namespace ygg::database
{

/// Scratch storage for sequential relational operations. Keep one workspace
/// per evaluator (or borrow one from UniqueObjectPool); never share it between
/// concurrent or reentrant calls. All buffers retain their capacity. Inputs
/// and their borrowed schemas must not refer to these scratch buffers.
template<TriviallyCopyable T = uint_t>
struct Workspace
{
    std::vector<Index<Column>> columns;
    std::vector<size_t> lhs_keys;
    std::vector<size_t> rhs_keys;
    std::vector<size_t> rhs_payload;
    std::vector<T> row;
    UnorderedMultiMap<hash_t, size_t> join_index;
};

/// Marks inputs whose row storage remains immutable for the cache lifetime.
struct JoinReuse
{
    bool lhs = false;
    bool rhs = false;
};

/// Element type T is deduced from output/workspace arguments. Return-by-value
/// overloads require an explicit T, for example project<uint_t>(input, columns).
/// Keeps columns in the requested order and eliminates duplicate result rows.
/// Output overloads replace rows, retain capacity, and require a matching
/// schema and storage distinct from every input (including renamed views).
template<TriviallyCopyable T, RelationViewConcept<T> V>
void project(const V& input, const ProjectionPlan& plan, Builder<Relation<T>>& out, Workspace<T>& workspace);

template<TriviallyCopyable T, RelationViewConcept<T> V>
void project(const V& input, std::span<const Index<Column>> columns, Builder<Relation<T>>& out, Workspace<T>& workspace);

template<TriviallyCopyable T, RelationViewConcept<T> V>
void project(const V& input, std::initializer_list<Index<Column>> columns, Builder<Relation<T>>& out, Workspace<T>& workspace);

template<TriviallyCopyable T, RelationViewConcept<T> V>
void project(const V& input, std::span<const Index<Column>> columns, Builder<Relation<T>>& out);

template<TriviallyCopyable T, RelationViewConcept<T> V>
void project(const V& input, std::initializer_list<Index<Column>> columns, Builder<Relation<T>>& out);

template<TriviallyCopyable T, RelationViewConcept<T> V>
Builder<Relation<T>> project(const V& input, Builder<Columns> columns);

template<TriviallyCopyable T, RelationViewConcept<T> V>
Builder<Relation<T>> project(const V& input, std::span<const Index<Column>> columns);

template<TriviallyCopyable T, RelationViewConcept<T> V>
Builder<Relation<T>> project(const V& input, std::initializer_list<Index<Column>> columns);

/// Extract rows into existing storage. The output must have the same ordered
/// schema and distinct storage; the operation retains its reusable capacity.
template<TriviallyCopyable T, RelationViewConcept<T> V>
Builder<Relation<T>>& assign(Builder<Relation<T>>& destination, const V& source);

/// The predicate receives a row span in input column order. It must not mutate
/// inputs or output. A throwing predicate can leave a partial output result.
template<TriviallyCopyable T, RelationViewConcept<T> V, typename Predicate>
    requires std::predicate<Predicate&, std::span<const T>>
void select(const V& input, Predicate predicate, Builder<Relation<T>>& out);

template<TriviallyCopyable T, RelationViewConcept<T> V, typename Predicate>
    requires std::predicate<Predicate&, std::span<const T>>
Builder<Relation<T>> select(const V& input, Predicate predicate);

template<TriviallyCopyable T, RelationViewConcept<T> V>
void select_equal_columns(const V& input, Index<Column> lhs, Index<Column> rhs, Builder<Relation<T>>& out);

template<TriviallyCopyable T, RelationViewConcept<T> V>
Builder<Relation<T>> select_equal_columns(const V& input, Index<Column> lhs, Index<Column> rhs);

template<TriviallyCopyable T, RelationViewConcept<T> V>
void select_equal_value(const V& input, Index<Column> column, const std::type_identity_t<T>& value, Builder<Relation<T>>& out);

template<TriviallyCopyable T, RelationViewConcept<T> V>
Builder<Relation<T>> select_equal_value(const V& input, Index<Column> column, const std::type_identity_t<T>& value);

/// Natural join: match common labels and return lhs columns followed by
/// rhs-only columns. Disjoint schemas produce the Cartesian product.
/// Hashes the smaller input, retaining only hashes and row indices in the
/// transient index. Collisions are resolved by comparing the actual values.
template<TriviallyCopyable T, RelationViewConcept<T> L, RelationViewConcept<T> R>
void join(const L& lhs, const R& rhs, const JoinPlan& plan, Builder<Relation<T>>& out, Workspace<T>& workspace);

/// Reuses an index on either input instead of rebuilding the smaller side.
/// The index must match that input's storage and the plan's ordered key positions.
/// Result row order is unspecified, as for the other relational operations.
template<TriviallyCopyable T, RelationViewConcept<T> L, RelationViewConcept<T> R>
void join(const L& lhs, const R& rhs, const JoinPlan& plan, const JoinIndex<T>& index, Builder<Relation<T>>& out, Workspace<T>& workspace);

/// Reuses the one marked input, or the smaller input when both are marked.
/// With neither marked, uses an ordinary transient index. Empty inputs and
/// Cartesian products do not create cached indexes.
template<TriviallyCopyable T, RelationViewConcept<T> L, RelationViewConcept<T> R>
void join(const L& lhs, const R& rhs, const JoinPlan& plan, JoinIndexCache<T>& cache, JoinReuse reuse, Builder<Relation<T>>& out, Workspace<T>& workspace);

template<TriviallyCopyable T, RelationViewConcept<T> L, RelationViewConcept<T> R>
void join(const L& lhs, const R& rhs, Builder<Relation<T>>& out, Workspace<T>& workspace);

template<TriviallyCopyable T, RelationViewConcept<T> L, RelationViewConcept<T> R>
void join(const L& lhs, const R& rhs, Builder<Relation<T>>& out);

template<TriviallyCopyable T, RelationViewConcept<T> L, RelationViewConcept<T> R>
Builder<Relation<T>> join(const L& lhs, const R& rhs);

/// Union and difference require identical ordered schemas. Project one input
/// first to align a differently ordered schema. The trailing underscore avoids
/// the C++ keyword union.
template<TriviallyCopyable T, RelationViewConcept<T> L, RelationViewConcept<T> R>
void union_(const L& lhs, const R& rhs, Builder<Relation<T>>& out);

template<TriviallyCopyable T, RelationViewConcept<T> L, RelationViewConcept<T> R>
Builder<Relation<T>> union_(const L& lhs, const R& rhs);

template<TriviallyCopyable T, RelationViewConcept<T> L, RelationViewConcept<T> R>
void difference(const L& lhs, const R& rhs, Builder<Relation<T>>& out);

template<TriviallyCopyable T, RelationViewConcept<T> L, RelationViewConcept<T> R>
Builder<Relation<T>> difference(const L& lhs, const R& rhs);

}  // namespace ygg::database

#include "yggdrasil/database/details/operations.hpp"

#endif
