/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_FORMALISM_RELATION_DATA_HPP_
#define YGG_FORMALISM_RELATION_DATA_HPP_

#include "yggdrasil/formalism/declarations.hpp"
#include "yggdrasil/formalism/values.hpp"

#include <compare>
#include <span>
#include <tuple>
#include <urd/elements/flat.hpp>

namespace ygg
{
/// A column: its label and type. Where its values lie in a row, the widths of its relation's pool say.
struct ColumnLayout
{
    urd::index<Column> label;
    TypeId type;

    static constexpr auto cista_members() noexcept { return urd::Members<&ColumnLayout::label, &ColumnLayout::type> {}; }
    static constexpr auto identifying_members() noexcept { return urd::Members<&ColumnLayout::label, &ColumnLayout::type> {}; }
    friend bool operator==(const ColumnLayout& lhs, const ColumnLayout& rhs) { return lhs.label == rhs.label && lhs.type == rhs.type; }
    friend auto operator<=>(const ColumnLayout& lhs, const ColumnLayout& rhs) { return std::tie(lhs.label, lhs.type) <=> std::tie(rhs.label, rhs.type); }
};
}  // namespace ygg

namespace urd
{
/// Column types: a pool's (every pool has one), and the key of free relations' pools (labels belong to their Columns).
template<>
struct data<ygg::Schema>
{
    urd::index<ygg::Schema> index;
    urd::flat_vector<ygg::TypeId> types;

    static constexpr auto cista_members() noexcept { return urd::Members<&data::index, &data::types> {}; }
    static constexpr auto identifying_members() noexcept { return urd::Members<&data::types> {}; }
};

/// The columns of a relation, labeled and typed.
template<>
struct data<ygg::Columns>
{
    urd::index<ygg::Columns> index;
    urd::flat_vector<ygg::ColumnLayout> values;

    data() = default;
    explicit data(std::span<const ygg::ColumnLayout> values_) : index() { values.set(values_.begin(), values_.end()); }

    static constexpr auto cista_members() noexcept { return urd::Members<&data::index, &data::values> {}; }
    static constexpr auto identifying_members() noexcept { return urd::Members<&data::values> {}; }
};

/// A relation of family F: its pool (the key of the layer's pool of its rows), its columns and the ids of its rows in
/// that pool, sorted and unique. Renaming keeps the pool and the rows.
template<typename F>
struct data<ygg::Relation<F>>
{
    urd::index<ygg::Relation<F>> index;
    urd::index<F> pool;
    urd::index<ygg::Columns> columns;
    urd::flat_vector<urd::index<ygg::Row<F>>> rows;

    static constexpr auto cista_members() noexcept { return urd::Members<&data::index, &data::pool, &data::columns, &data::rows> {}; }
    static constexpr auto identifying_members() noexcept { return urd::Members<&data::pool, &data::columns, &data::rows> {}; }
};
}  // namespace urd

#endif
