/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_OPTIMIZATION_STATISTICS_HPP_
#define YGG_DATABASE_OPTIMIZATION_STATISTICS_HPP_

#include "yggdrasil/containers/associative_containers.hpp"
#include "yggdrasil/database/semantics/operations.hpp"
#include "yggdrasil/database/semantics/relation_view.hpp"
#include "yggdrasil/database/syntax/query.hpp"

#include <optional>

namespace ygg::database
{
struct RelationStatistics
{
    double rows = 0;
    UnorderedMap<Index<Column>, double> distinct;
};

/// What is known about the data. Everything is optional; nothing known means unbounded.
struct Statistics
{
    /// Domain size; empty means an unbounded domain.
    std::optional<size_t> objects;
    /// Measured inputs, keyed by input slot.
    UnorderedMap<size_t, RelationStatistics> inputs;
    /// Observed results of queries in the roots' repository; they override estimates.
    UnorderedMap<Index<Query<>>, RelationStatistics> expressions;
};

/// Row and per-column distinct counts of one relation.
template<RelationViewConcept Relation>
RelationStatistics measure_relation(const Relation& relation)
{
    RelationStatistics stats;
    stats.rows = static_cast<double>(relation.size());
    const auto columns = relation.columns();
    for (const auto& column : columns.span())
        stats.distinct[column.label] = static_cast<double>(project(relation, { column.label }).size());
    return stats;
}
}  // namespace ygg::database

#endif
