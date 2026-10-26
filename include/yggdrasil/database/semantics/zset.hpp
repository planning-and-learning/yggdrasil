/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_SEMANTICS_ZSET_HPP_
#define YGG_DATABASE_SEMANTICS_ZSET_HPP_

#include "yggdrasil/database/semantics/relation_builder.hpp"
#include "yggdrasil/database/semantics/relation_view.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace ygg::database
{
/// The weight of a row in a Z-set.
using Weight = int64_t;

/// A Z-set [Budiu23, §4.1]: rows of one schema with nonzero integer weights. Sets, multisets and changes of either are
/// Z-sets; adding a weight consolidates, and a row whose weight becomes zero is erased (the last row moves into its
/// position, as in Builder<Relation>).
///
/// [Budiu23] M. Budiu, F. McSherry, L. Ryzhyk, V. Tannen, DBSP: Automatic Incremental View Maintenance for Rich Query
///   Languages, PVLDB 16(7), 2023.
class ZSet
{
    Builder<Relation> m_rows;
    std::vector<Weight> m_weights;

public:
    explicit ZSet(std::span<const ColumnLayout> columns);
    explicit ZSet(std::span<const Index<Column>> columns = {});

    /// The rows of nonzero weight.
    [[nodiscard]] const Builder<Relation>& rows() const& noexcept { return m_rows; }
    [[nodiscard]] const Builder<Relation>& rows() const&& = delete;
    [[nodiscard]] std::span<const ColumnLayout> columns() const noexcept { return m_rows.columns().span(); }
    [[nodiscard]] size_t size() const noexcept { return m_rows.size(); }
    [[nodiscard]] bool empty() const noexcept { return m_rows.empty(); }
    [[nodiscard]] std::span<const std::byte> row(size_t index) const noexcept { return m_rows.row(index); }
    [[nodiscard]] Weight weight(size_t index) const noexcept { return m_weights[index]; }
    /// The weight of a row, 0 if absent.
    [[nodiscard]] Weight weight(std::span<const std::byte> row) const;

    /// Adds `weight` to the row's weight and returns the new weight.
    Weight add(std::span<const std::byte> row, Weight weight);
    /// Adds every row of a relation with the same weight.
    template<RelationViewConcept V>
    void add(const V& relation, Weight weight)
    {
        for (size_t i = 0; i < relation.size(); ++i)
            add(relation.row(i), weight);
    }
    /// Adds every row of another Z-set, its weights scaled by `factor`.
    void add(const ZSet& other, Weight factor = 1);
    void clear() noexcept;
    /// Clears rows and replaces the schema.
    void initialize(std::span<const ColumnLayout> columns);

    [[nodiscard]] size_t memory_usage() const noexcept { return m_rows.memory_usage() + m_weights.capacity() * sizeof(Weight); }
};
}  // namespace ygg::database

#endif
