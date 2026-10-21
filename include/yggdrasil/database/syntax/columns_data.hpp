/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SYNTAX_COLUMNS_DATA_HPP_
#define YGG_DATABASE_SYNTAX_COLUMNS_DATA_HPP_

#include "yggdrasil/core/types_utils.hpp"
#include "yggdrasil/database/declarations.hpp"

#include <cista/containers/vector.h>
#include <compare>
#include <cstddef>
#include <span>
#include <tuple>
#include <utility>

namespace ygg::database
{
/// Offsets and widths are derived from the ordered schema and validated before use.
struct ColumnLayout
{
    Index<Column> label;
    TypeId type;
    size_t offset;
    size_t size;

    auto cista_members() noexcept { return std::tie(label, type, offset, size); }
    auto cista_members() const noexcept { return std::tie(label, type, offset, size); }
    static constexpr auto identifying_members() noexcept { return Members<&ColumnLayout::label, &ColumnLayout::type> {}; }
    friend bool operator==(const ColumnLayout& lhs, const ColumnLayout& rhs) { return lhs.label == rhs.label && lhs.type == rhs.type; }
    friend auto operator<=>(const ColumnLayout& lhs, const ColumnLayout& rhs) { return std::tie(lhs.label, lhs.type) <=> std::tie(rhs.label, rhs.type); }
};
}  // namespace ygg::database

namespace ygg
{
template<>
struct Data<database::Columns>
{
    Index<database::Columns> index;
    ::cista::offset::vector<database::ColumnLayout> values;

    Data() = default;
    explicit Data(std::span<const database::ColumnLayout> values_) : index() { values.set(values_.begin(), values_.end()); }

    auto cista_members() noexcept { return std::tie(index, values); }
    auto cista_members() const noexcept { return std::tie(index, values); }
    static constexpr auto identifying_members() noexcept { return Members<&Data::values> {}; }
    void clear() noexcept
    {
        std::apply([](auto&... member) { (ygg::clear(member), ...); }, cista_members());
    }
};
}  // namespace ygg

#endif
