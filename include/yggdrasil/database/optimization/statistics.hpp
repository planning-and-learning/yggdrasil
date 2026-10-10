/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_OPTIMIZATION_STATISTICS_HPP_
#define YGG_DATABASE_OPTIMIZATION_STATISTICS_HPP_

#include "yggdrasil/containers/associative_containers.hpp"
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
template<ColumnTypes Values = DefaultColumnTypes>
struct Statistics
{
    /// Domain size; empty means an unbounded domain.
    std::optional<size_t> objects;
    /// Measured inputs, keyed by input slot.
    UnorderedMap<size_t, RelationStatistics> inputs;
    /// Observed results of queries in the roots' repository; they override estimates.
    UnorderedMap<Index<Query<Values>>, RelationStatistics> expressions;
};
}  // namespace ygg::database

#endif
