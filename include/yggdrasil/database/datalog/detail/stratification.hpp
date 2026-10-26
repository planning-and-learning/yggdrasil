/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef YGG_DATABASE_DATALOG_DETAIL_STRATIFICATION_HPP_
#define YGG_DATABASE_DATALOG_DETAIL_STRATIFICATION_HPP_

#include <cstddef>
#include <span>
#include <vector>

namespace ygg::database::datalog::detail
{
/// The head of a rule reads the body relation; negatively through negation or aggregation.
struct Dependency
{
    size_t head = 0;
    size_t body = 0;

    bool negative = false;
};

/// A strongly connected component of the dependencies, a stratum of the program.
struct Component
{
    std::vector<size_t> relations;
    /// Whether a relation of the stratum reads one of the stratum's relations, itself included.
    bool recursive = false;
};

/// The strongly connected components of the relations' dependencies, in dependency order. Relations are 0, ..., n - 1;
/// dependencies on other relations (inputs) are ignored. Throws std::invalid_argument if a negative dependency lies on
/// a cycle.
std::vector<Component> stratify(size_t relations, std::span<const Dependency> dependencies);
}  // namespace ygg::database::datalog::detail

#endif
