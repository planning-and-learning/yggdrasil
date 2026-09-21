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

#include "yggdrasil/containers/unordered_multi_map.hpp"
#include "yggdrasil/database/plans.hpp"
#include "yggdrasil/database/relation.hpp"
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
    std::vector<Column> columns;
    std::vector<size_t> lhs_keys;
    std::vector<size_t> rhs_keys;
    std::vector<size_t> rhs_payload;
    std::vector<T> row;
    UnorderedMultiMap<hash_t, size_t> join_index;
};

/// Relabels columns positionally, without copying any tuples. New labels must
/// be unique. To identify two columns, select their equality and project.
template<TriviallyCopyable T>
RelationView<T> rename(const RelationView<T>& input, Columns columns);

template<TriviallyCopyable T>
RelationView<T> rename(const RelationView<T>& input, ColumnsView columns);

template<TriviallyCopyable T>
RelationView<T> rename(const RelationView<T>& input, std::vector<Column> columns);

/// Borrows labels as well as rows. Pass a span explicitly; its underlying
/// labels must outlive the returned view.
template<TriviallyCopyable T, typename C, size_t Extent>
    requires std::same_as<std::remove_const_t<C>, Column>
RelationView<T> rename(const RelationView<T>& input, std::span<C, Extent> columns);

template<TriviallyCopyable T>
RelationView<T> rename(const RelationView<T>& input, std::initializer_list<Column> columns);

/// Keeps columns in the requested order and eliminates duplicate result rows.
/// Output overloads replace rows, retain capacity, and require a matching
/// schema and storage distinct from every input (including renamed views).
template<TriviallyCopyable T>
void project(const RelationView<T>& input, const ProjectionPlan& plan, Relation<T>& out, Workspace<T>& workspace);

template<TriviallyCopyable T>
void project(const RelationView<T>& input, ColumnsView columns, Relation<T>& out, Workspace<T>& workspace);

template<TriviallyCopyable T>
void project(const RelationView<T>& input, std::span<const Column> columns, Relation<T>& out, Workspace<T>& workspace);

template<TriviallyCopyable T>
void project(const RelationView<T>& input, std::initializer_list<Column> columns, Relation<T>& out, Workspace<T>& workspace);

template<TriviallyCopyable T>
void project(const RelationView<T>& input, ColumnsView columns, Relation<T>& out);

template<TriviallyCopyable T>
void project(const RelationView<T>& input, std::span<const Column> columns, Relation<T>& out);

template<TriviallyCopyable T>
void project(const RelationView<T>& input, std::initializer_list<Column> columns, Relation<T>& out);

template<TriviallyCopyable T>
Relation<T> project(const RelationView<T>& input, Columns columns);

template<TriviallyCopyable T>
Relation<T> project(const RelationView<T>& input, ColumnsView columns);

template<TriviallyCopyable T>
Relation<T> project(const RelationView<T>& input, std::vector<Column> columns);

template<TriviallyCopyable T>
Relation<T> project(const RelationView<T>& input, std::initializer_list<Column> columns);

/// The predicate receives a row span in input column order. It must not mutate
/// inputs or output. A throwing predicate can leave a partial output result.
template<TriviallyCopyable T, typename Predicate>
    requires std::predicate<Predicate&, std::span<const T>>
void select(const RelationView<T>& input, Predicate predicate, Relation<T>& out);

template<TriviallyCopyable T, typename Predicate>
    requires std::predicate<Predicate&, std::span<const T>>
Relation<T> select(const RelationView<T>& input, Predicate predicate);

template<TriviallyCopyable T>
void select_equal_columns(const RelationView<T>& input, Column lhs, Column rhs, Relation<T>& out);

template<TriviallyCopyable T>
Relation<T> select_equal_columns(const RelationView<T>& input, Column lhs, Column rhs);

template<TriviallyCopyable T>
void select_equal_value(const RelationView<T>& input, Column column, const std::type_identity_t<T>& value, Relation<T>& out);

template<TriviallyCopyable T>
Relation<T> select_equal_value(const RelationView<T>& input, Column column, const std::type_identity_t<T>& value);

/// Natural join: match common labels and return lhs columns followed by
/// rhs-only columns. Disjoint schemas produce the Cartesian product.
/// Hashes the smaller input, retaining only hashes and row indices in the
/// transient index. Collisions are resolved by comparing the actual values.
template<TriviallyCopyable T>
void join(const RelationView<T>& lhs, const RelationView<T>& rhs, const JoinPlan& plan, Relation<T>& out, Workspace<T>& workspace);

template<TriviallyCopyable T>
void join(const RelationView<T>& lhs, const RelationView<T>& rhs, Relation<T>& out, Workspace<T>& workspace);

template<TriviallyCopyable T>
void join(const RelationView<T>& lhs, const RelationView<T>& rhs, Relation<T>& out);

template<TriviallyCopyable T>
Relation<T> join(const RelationView<T>& lhs, const RelationView<T>& rhs);

/// Union and difference require identical ordered schemas. Project one input
/// first to align a differently ordered schema. The trailing underscore avoids
/// the C++ keyword union.
template<TriviallyCopyable T>
void union_(const RelationView<T>& lhs, const RelationView<T>& rhs, Relation<T>& out);

template<TriviallyCopyable T>
Relation<T> union_(const RelationView<T>& lhs, const RelationView<T>& rhs);

template<TriviallyCopyable T>
void difference(const RelationView<T>& lhs, const RelationView<T>& rhs, Relation<T>& out);

template<TriviallyCopyable T>
Relation<T> difference(const RelationView<T>& lhs, const RelationView<T>& rhs);

}  // namespace ygg::database

#include "yggdrasil/database/details/operations.hpp"

#endif
