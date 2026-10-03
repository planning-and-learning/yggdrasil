/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_DETAILS_COLUMNS_HPP_
#define YGG_DATABASE_DETAILS_COLUMNS_HPP_

#include "yggdrasil/database/columns_view.hpp"

#include <algorithm>
#include <stdexcept>

namespace ygg::database
{
inline void validate_columns(std::span<const Index<Column>> columns)
{
    // ponytail: quadratic in arity; use a temporary set if wide schemas make validation costly.
    for (auto it = columns.begin(); it != columns.end(); ++it)
        if (std::find(columns.begin(), it, *it) != it)
            throw std::invalid_argument("Columns: duplicate column label.");
}

inline size_t column_index(std::span<const Index<Column>> columns, Index<Column> column)
{
    const auto it = std::ranges::find(columns, column);
    if (it == columns.end())
        throw std::out_of_range("Columns: unknown column label.");
    return static_cast<size_t>(it - columns.begin());
}
}  // namespace ygg::database

namespace ygg
{
inline Builder<database::Columns>::Builder(std::span<const Index<database::Column>> columns) { assign(columns); }
inline Builder<database::Columns>::Builder(std::initializer_list<Index<database::Column>> columns) : Builder(std::span<const Index<database::Column>>(columns))
{
}
inline size_t Builder<database::Columns>::column_index(Index<database::Column> column) const { return database::column_index(span(), column); }

inline void Builder<database::Columns>::assign(std::span<const Index<database::Column>> columns)
{
    database::validate_columns(columns);
    auto& values = m_data.values;
    if (columns.size() <= values.size())
    {
        // Copy before shrinking: columns may refer to a slice of this schema.
        if (columns.data() != values.data())
            std::copy(columns.begin(), columns.end(), values.begin());
        values.resize(columns.size());
    }
    else
        values.set(columns.begin(), columns.end());
    ygg::clear(m_data.index);
}

inline void Builder<database::Columns>::assign(std::initializer_list<Index<database::Column>> columns)
{
    assign(std::span<const Index<database::Column>>(columns));
}
inline void Builder<database::Columns>::initialize(std::span<const Index<database::Column>> columns) { assign(columns); }
inline void Builder<database::Columns>::initialize(std::initializer_list<Index<database::Column>> columns) { assign(columns); }
}  // namespace ygg

namespace ygg::database
{
inline Data<Columns>& make_data(const Builder<Columns>& builder, Data<Columns>& data)
{
    if (&builder.get_data() != &data)
        data.values.set(builder.get_data().values);
    ygg::clear(data.index);
    return data;
}
}  // namespace ygg::database

#endif
