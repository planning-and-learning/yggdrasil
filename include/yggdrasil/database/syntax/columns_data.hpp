/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SYNTAX_COLUMNS_DATA_HPP_
#define YGG_DATABASE_SYNTAX_COLUMNS_DATA_HPP_

#include "yggdrasil/core/types_utils.hpp"
#include "yggdrasil/database/syntax/columns_index.hpp"

#include <cista/containers/vector.h>
#include <compare>
#include <cstddef>
#include <tuple>

namespace ygg::database
{
/// Offsets and widths are derived from the ordered schema and validated before use.
struct ColumnLayout
{
    Index<Column> label;
    size_t type;
    size_t offset;
    size_t size;

    auto cista_members() noexcept { return std::tie(label, type, offset, size); }
    auto cista_members() const noexcept { return std::tie(label, type, offset, size); }
    auto identifying_members() const noexcept { return std::tie(label, type); }
    friend bool operator==(const ColumnLayout& lhs, const ColumnLayout& rhs) { return lhs.identifying_members() == rhs.identifying_members(); }
    friend auto operator<=>(const ColumnLayout& lhs, const ColumnLayout& rhs) { return lhs.identifying_members() <=> rhs.identifying_members(); }
};
}  // namespace ygg::database

namespace ygg
{
template<database::ColumnTypes Values>
struct Data<database::Columns<Values>>
{
    Index<database::Columns<Values>> index;
    ::cista::offset::vector<database::ColumnLayout> values;

    void clear() noexcept
    {
        ygg::clear(index);
        values.clear();
    }

    auto cista_members() noexcept { return std::tie(index, values); }
    auto cista_members() const noexcept { return std::tie(index, values); }
    auto identifying_members() const noexcept { return std::tie(values); }
};
}  // namespace ygg

#endif
