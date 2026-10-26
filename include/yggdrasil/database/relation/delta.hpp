/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_RELATION_DELTA_HPP_
#define YGG_DATABASE_RELATION_DELTA_HPP_

#include "yggdrasil/database/relation/relation_builder.hpp"
#include "yggdrasil/database/relation/relation_view.hpp"

#include <concepts>
#include <initializer_list>
#include <ranges>
#include <span>
#include <tuple>
#include <utility>

namespace ygg::database
{
/// Actual set changes with one ordered schema. Added and removed rows are
/// disjoint. The caller guarantees additions were absent and removals present.
/// Exchanging the two inputs to update() reverses the change.
struct Delta
{
    Builder<Relation> added;
    Builder<Relation> removed;

    explicit Delta(std::span<const ColumnLayout> columns) : added(columns), removed(columns) {}
    explicit Delta(std::span<const Index<Column>> columns) : added(columns), removed(columns) {}
    Delta(std::initializer_list<Index<Column>> columns) : Delta(std::span<const Index<Column>>(columns.begin(), columns.size())) {}

    void clear() noexcept
    {
        added.clear();
        removed.clear();
    }
    /// The (added, removed) change.
    [[nodiscard]] std::pair<const Builder<Relation>&, const Builder<Relation>&> change() const noexcept { return { added, removed }; }
    [[nodiscard]] size_t memory_usage() const noexcept { return added.memory_usage() + removed.memory_usage(); }
};

/// An actual set change of one input: disjoint (added, removed) relations, read with std::get.
template<typename C>
concept RelationChange = requires(const C& change) {
    { std::get<0>(change) } -> RelationViewConcept;
    { std::get<1>(change) } -> RelationViewConcept;
};

/// The exact changes of a query's inputs by slot: f(slot).added and f(slot).removed are relation views, the rows added to
/// and removed from input slot `slot`.
template<typename F>
concept SlotChanges = std::invocable<const F&, size_t> && requires(const F& change, size_t slot) {
    { change(slot).added } -> RelationViewConcept;
    { change(slot).removed } -> RelationViewConcept;
};

/// Sized random-access changes, one per input.
template<typename R>
concept RelationDeltaRange =
    std::ranges::random_access_range<const R> && std::ranges::sized_range<const R> && RelationChange<std::ranges::range_reference_t<const R>>;
}  // namespace ygg::database

#endif
