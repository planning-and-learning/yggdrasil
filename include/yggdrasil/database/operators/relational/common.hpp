/*
 * Copyright (C) 2026 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef YGG_DATABASE_OPERATORS_RELATIONAL_COMMON_HPP_
#define YGG_DATABASE_OPERATORS_RELATIONAL_COMMON_HPP_

#include "yggdrasil/containers/unordered_multi_map.hpp"
#include "yggdrasil/database/operators/relational/plan.hpp"
#include "yggdrasil/database/relation/relation_view.hpp"
#include "yggdrasil/semantics/equal_to.hpp"
#include "yggdrasil/semantics/hash.hpp"

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <limits>
#include <ranges>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

/// What the relational operations share: scratch storage and the preparation and copying of output rows.
namespace ygg::database
{
/// Scratch storage for sequential relational operations. Keep one workspace
/// per evaluator (or borrow one from UniqueObjectPool); never share it between
/// concurrent or reentrant calls. All buffers retain their capacity. Inputs
/// and their borrowed schemas must not refer to these scratch buffers.
struct Workspace
{
    std::vector<ColumnLayout> columns;
    std::vector<ColumnSlice> lhs_keys;
    std::vector<ColumnSlice> rhs_keys;
    std::vector<ColumnSlice> rhs_payload;
    std::vector<std::byte> row;
    UnorderedMultiMap<hash_t, size_t> join_index;

    /// Retained scratch storage.
    [[nodiscard]] size_t memory_usage() const noexcept
    {
        return columns.capacity() * sizeof(ColumnLayout) + (lhs_keys.capacity() + rhs_keys.capacity() + rhs_payload.capacity()) * sizeof(ColumnSlice)
               + row.capacity() + join_index.memory_usage();
    }
};

namespace detail
{
/// Copy canonical fields verbatim; plans resolve their byte slices before evaluation.
void append_fields(std::vector<std::byte>& output, std::span<const std::byte> input, std::span<const ColumnSlice> fields);

void prepare_output(Builder<Relation>& out, std::span<const ColumnLayout> columns, std::initializer_list<const void*> inputs);

template<RelationViewConcept L, RelationViewConcept R>
void require_same_columns(const L& lhs, const R& rhs)
{
    if (!std::ranges::equal(lhs.columns().span(), rhs.columns().span()))
        throw std::invalid_argument("Relational operation: input schemas must have the same column order.");
}
}  // namespace detail
}  // namespace ygg::database

#endif
