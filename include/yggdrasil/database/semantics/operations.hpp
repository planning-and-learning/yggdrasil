/*
 * Copyright (C) 2026 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef YGG_DATABASE_SEMANTICS_OPERATIONS_HPP_
#define YGG_DATABASE_SEMANTICS_OPERATIONS_HPP_

#include "yggdrasil/database/semantics/join_index.hpp"
#include "yggdrasil/database/semantics/plans.hpp"
#include "yggdrasil/database/semantics/relation_view.hpp"
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
template<ColumnTypes Values = DefaultColumnTypes>
struct Workspace
{
    std::vector<ColumnLayout> columns;
    std::vector<ColumnSlice> lhs_keys;
    std::vector<ColumnSlice> rhs_keys;
    std::vector<ColumnSlice> rhs_payload;
    std::vector<std::byte> row;
    UnorderedMultiMap<hash_t, size_t> join_index;

    /// Retained scratch storage.
    size_t memory_usage() const noexcept
    {
        return columns.capacity() * sizeof(ColumnLayout) + (lhs_keys.capacity() + rhs_keys.capacity() + rhs_payload.capacity()) * sizeof(ColumnSlice)
               + row.capacity() + join_index.memory_usage();
    }
};

/// Marks inputs whose row storage remains immutable for the cache lifetime.
struct JoinReuse
{
    bool lhs = false;
    bool rhs = false;
};

/// The column type list is deduced from output/workspace arguments.
/// Return-by-value overloads require an explicit type list.
/// Keeps columns in the requested order and eliminates duplicate result rows.
/// Output overloads replace rows, retain capacity, and require a matching
/// schema and storage distinct from every input (including renamed views).
template<ColumnTypes Values, RelationViewConcept<Values> V>
void project(const V& input, const ProjectionPlan<Values>& plan, Builder<Relation<Values>>& out, Workspace<Values>& workspace);

template<ColumnTypes Values, RelationViewConcept<Values> V>
void project(const V& input, std::span<const Index<Column>> columns, Builder<Relation<Values>>& out, Workspace<Values>& workspace);

template<ColumnTypes Values, RelationViewConcept<Values> V>
void project(const V& input, std::initializer_list<Index<Column>> columns, Builder<Relation<Values>>& out, Workspace<Values>& workspace);

template<ColumnTypes Values, RelationViewConcept<Values> V>
void project(const V& input, std::span<const Index<Column>> columns, Builder<Relation<Values>>& out);

template<ColumnTypes Values, RelationViewConcept<Values> V>
void project(const V& input, std::initializer_list<Index<Column>> columns, Builder<Relation<Values>>& out);

template<ColumnTypes Values, RelationViewConcept<Values> V>
Builder<Relation<Values>> project(const V& input, Builder<Columns<Values>> columns);

template<ColumnTypes Values, RelationViewConcept<Values> V>
Builder<Relation<Values>> project(const V& input, std::span<const Index<Column>> columns);

template<ColumnTypes Values, RelationViewConcept<Values> V>
Builder<Relation<Values>> project(const V& input, std::initializer_list<Index<Column>> columns);

/// Replace ordered columns and rows, invalidating the destination's canonical
/// index. Self-assignment is supported. Storage identity is preserved, and
/// matching row size retains reusable capacity; changing row size replaces storage.
/// A source sharing destination storage must expose the same complete rows;
/// it may relabel columns.
/// Borrowed destination rows and schema spans are invalidated. An insertion
/// failure may leave partial output. Schema namespaces belong to publication.
template<ColumnTypes Values, RelationViewConcept<Values> V>
Builder<Relation<Values>>& assign(Builder<Relation<Values>>& destination, const V& source);

/// The predicate receives a typed row borrowing the input and its schema. It must not mutate
/// inputs or output. A throwing predicate can leave a partial output result.
template<ColumnTypes Values, RelationViewConcept<Values> V, typename Predicate>
    requires std::predicate<Predicate&, Row<Values>>
void select(const V& input, Predicate predicate, Builder<Relation<Values>>& out);

template<ColumnTypes Values, RelationViewConcept<Values> V, typename Predicate>
    requires std::predicate<Predicate&, Row<Values>>
Builder<Relation<Values>> select(const V& input, Predicate predicate);

template<ColumnTypes Values, RelationViewConcept<Values> V>
void select_equal_columns(const V& input, Index<Column> lhs, Index<Column> rhs, Builder<Relation<Values>>& out);

template<ColumnTypes Values, RelationViewConcept<Values> V>
Builder<Relation<Values>> select_equal_columns(const V& input, Index<Column> lhs, Index<Column> rhs);

template<ColumnTypes Values, RelationViewConcept<Values> V, ColumnValueFor<Values> T>
void select_equal_value(const V& input, Index<Column> column, const T& value, Builder<Relation<Values>>& out);

template<ColumnTypes Values, RelationViewConcept<Values> V, ColumnValueFor<Values> T>
Builder<Relation<Values>> select_equal_value(const V& input, Index<Column> column, const T& value);

/// Natural join: match common labels and return lhs columns followed by
/// rhs-only columns. Disjoint schemas produce the Cartesian product.
/// Hashes the smaller input, retaining only hashes and row indices in the
/// transient index. Collisions are resolved by comparing the actual values.
template<ColumnTypes Values, RelationViewConcept<Values> L, RelationViewConcept<Values> R>
void join(const L& lhs, const R& rhs, const JoinPlan<Values>& plan, Builder<Relation<Values>>& out, Workspace<Values>& workspace);

/// Reuses an index on either input instead of rebuilding the smaller side.
/// The index must match that input's storage and the plan's ordered field slices.
/// Result row order is unspecified, as for the other relational operations.
template<ColumnTypes Values, RelationViewConcept<Values> L, RelationViewConcept<Values> R>
void join(const L& lhs,
          const R& rhs,
          const JoinPlan<Values>& plan,
          const JoinIndex<Values>& index,
          Builder<Relation<Values>>& out,
          Workspace<Values>& workspace);

/// Reuses the one marked input, or the smaller input when both are marked.
/// With neither marked, uses an ordinary transient index. Empty inputs and
/// Cartesian products do not create cached indexes.
template<ColumnTypes Values, RelationViewConcept<Values> L, RelationViewConcept<Values> R>
void join(const L& lhs,
          const R& rhs,
          const JoinPlan<Values>& plan,
          JoinIndexCache<Values>& cache,
          JoinReuse reuse,
          Builder<Relation<Values>>& out,
          Workspace<Values>& workspace);

template<ColumnTypes Values, RelationViewConcept<Values> L, RelationViewConcept<Values> R>
void join(const L& lhs, const R& rhs, Builder<Relation<Values>>& out, Workspace<Values>& workspace);

template<ColumnTypes Values, RelationViewConcept<Values> L, RelationViewConcept<Values> R>
void join(const L& lhs, const R& rhs, Builder<Relation<Values>>& out);

template<ColumnTypes Values, RelationViewConcept<Values> L, RelationViewConcept<Values> R>
Builder<Relation<Values>> join(const L& lhs, const R& rhs);

/// Union and difference require identical ordered schemas. Project one input
/// first to align a differently ordered schema. The trailing underscore avoids
/// the C++ keyword union.
template<ColumnTypes Values, RelationViewConcept<Values> L, RelationViewConcept<Values> R>
void union_(const L& lhs, const R& rhs, Builder<Relation<Values>>& out);

template<ColumnTypes Values, RelationViewConcept<Values> L, RelationViewConcept<Values> R>
Builder<Relation<Values>> union_(const L& lhs, const R& rhs);

template<ColumnTypes Values, RelationViewConcept<Values> L, RelationViewConcept<Values> R>
void difference(const L& lhs, const R& rhs, Builder<Relation<Values>>& out);

template<ColumnTypes Values, RelationViewConcept<Values> L, RelationViewConcept<Values> R>
Builder<Relation<Values>> difference(const L& lhs, const R& rhs);

}  // namespace ygg::database

#include "yggdrasil/database/semantics/details/operations.hpp"

#endif
