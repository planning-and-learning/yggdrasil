/*
 * Copyright (C) 2026 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef YGG_DATABASE_OPERATORS_RELATIONAL_PROJECT_HPP_
#define YGG_DATABASE_OPERATORS_RELATIONAL_PROJECT_HPP_

#include "yggdrasil/database/operators/relational/common.hpp"

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

namespace ygg::database
{
/// The column type list is deduced from output/workspace arguments.
/// Return-by-value overloads require an explicit type list.
/// Keeps columns in the requested order and eliminates duplicate result rows.
/// Output overloads replace rows, retain capacity, and require a matching
/// schema and storage distinct from every input (including renamed views).
template<RelationViewConcept V>
void project(const V& input, const ProjectionPlan& plan, Builder<Relation>& out, Workspace& workspace);

template<RelationViewConcept V>
void project(const V& input, std::span<const Index<Column>> columns, Builder<Relation>& out, Workspace& workspace);

template<RelationViewConcept V>
void project(const V& input, std::initializer_list<Index<Column>> columns, Builder<Relation>& out, Workspace& workspace);

template<RelationViewConcept V>
void project(const V& input, std::span<const Index<Column>> columns, Builder<Relation>& out);

template<RelationViewConcept V>
void project(const V& input, std::initializer_list<Index<Column>> columns, Builder<Relation>& out);

template<RelationViewConcept V>
Builder<Relation> project(const V& input, Builder<Columns> columns);

template<RelationViewConcept V>
Builder<Relation> project(const V& input, std::span<const Index<Column>> columns);

template<RelationViewConcept V>
Builder<Relation> project(const V& input, std::initializer_list<Index<Column>> columns);

/// Replace ordered columns and rows, invalidating the destination's canonical
/// index. Self-assignment is supported. Storage identity is preserved, and
/// matching row size retains reusable capacity; changing row size replaces storage.
/// A source sharing destination storage must expose the same complete rows;
/// it may relabel columns.
/// Borrowed destination rows and schema spans are invalidated. An insertion
/// failure may leave partial output. Schema namespaces belong to publication.
template<RelationViewConcept V>
Builder<Relation>& assign(Builder<Relation>& destination, const V& source);

namespace detail
{
template<RelationViewConcept V>
void project_rows(const V& input, std::span<const ColumnLayout> columns, std::span<const ColumnSlice> positions, Builder<Relation>& out, Workspace& workspace)
{
    detail::prepare_output(out, columns, { input.get_storage_address() });

    if (positions.empty())
    {
        if (!input.empty())
            out.insert(Row({}, columns));
        return;
    }

    auto& row = workspace.row;
    row.reserve(out.columns().row_size());
    for (size_t i = 0; i < input.size(); ++i)
    {
        const auto source = input.row(i);
        row.clear();
        append_fields(row, source, positions);
        out.insert(Row(row, columns));
    }
}

}  // namespace detail

template<RelationViewConcept V>
void project(const V& input, const ProjectionPlan& plan, Builder<Relation>& out, Workspace& workspace)
{
    detail::require_plan_columns(input.columns().span(), plan.input_columns().span());
    detail::project_rows(input, plan.output_columns().span(), plan.positions(), out, workspace);
}

template<RelationViewConcept V>
void project(const V& input, std::span<const Index<Column>> columns, Builder<Relation>& out, Workspace& workspace)
{
    detail::projection_positions(input.columns().span(), columns, workspace.columns, workspace.lhs_keys);
    detail::project_rows(input, workspace.columns, workspace.lhs_keys, out, workspace);
}

template<RelationViewConcept V>
void project(const V& input, std::initializer_list<Index<Column>> columns, Builder<Relation>& out, Workspace& workspace)
{ project(input, std::span<const Index<Column>>(columns), out, workspace); }

template<RelationViewConcept V>
void project(const V& input, std::span<const Index<Column>> columns, Builder<Relation>& out)
{
    auto workspace = Workspace();
    project(input, columns, out, workspace);
}

template<RelationViewConcept V>
void project(const V& input, std::initializer_list<Index<Column>> columns, Builder<Relation>& out)
{ project(input, std::span<const Index<Column>>(columns), out); }

template<RelationViewConcept V>
Builder<Relation> project(const V& input, Builder<Columns> columns)
{
    auto out = Builder<Relation>(std::move(columns));
    auto workspace = Workspace();
    const auto input_columns = input.columns().span();
    for (const auto& column : out.columns())
    {
        const auto& source = input_columns[input.column_index(column.label)];
        if (source.type != column.type)
            throw std::invalid_argument("Relational operation: projection output has an incompatible column type.");
        workspace.lhs_keys.push_back(detail::column_slice(source));
    }
    detail::project_rows(input, out.columns().span(), workspace.lhs_keys, out, workspace);
    return out;
}

template<RelationViewConcept V>
Builder<Relation> project(const V& input, std::span<const Index<Column>> columns)
{
    auto workspace = Workspace();
    detail::projection_positions(input.columns().span(), columns, workspace.columns, workspace.lhs_keys);
    auto out = Builder<Relation>(workspace.columns);
    detail::project_rows(input, workspace.columns, workspace.lhs_keys, out, workspace);
    return out;
}

template<RelationViewConcept V>
Builder<Relation> project(const V& input, std::initializer_list<Index<Column>> columns)
{ return project(input, std::span<const Index<Column>>(columns)); }

template<RelationViewConcept V>
Builder<Relation>& assign(Builder<Relation>& destination, const V& source)
{
    if (destination.get_storage_address() == source.get_storage_address())
    {
        destination.rename(source.columns().span());
        return destination;
    }
    destination.initialize(source.columns().span());
    for (size_t i = 0; i < source.size(); ++i)
        destination.insert(Row(source.row(i), source.columns().span()));
    return destination;
}
}  // namespace ygg::database

#endif
