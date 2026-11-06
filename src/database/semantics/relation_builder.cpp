/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/database/semantics/relation_builder.hpp"

namespace urd
{
urd::builder<ygg::database::Relation>::builder(urd::builder<ygg::database::Columns> columns) : m_columns(std::move(columns)) {}
}  // namespace urd

namespace urd
{
urd::builder<ygg::database::Relation>::builder(std::span<const ygg::database::ColumnLayout> columns) : builder(urd::builder<ygg::database::Columns>(columns)) {}
}  // namespace urd

namespace urd
{
urd::builder<ygg::database::Relation>::builder(std::span<const urd::index<ygg::database::Column>> columns) : builder(urd::builder<ygg::database::Columns>(columns)) {}
}  // namespace urd

namespace urd
{
urd::builder<ygg::database::Relation>::builder(std::initializer_list<urd::index<ygg::database::Column>> columns) : builder(std::span<const urd::index<ygg::database::Column>>(columns)) {}
}  // namespace urd

namespace urd
{
ygg::uint_t urd::builder<ygg::database::Relation>::insert(std::span<const std::byte> row)
{
    if (row.size() != m_columns.row_size())
        throw std::invalid_argument("Relation: row byte width does not match its schema.");
    const auto index = m_rows.insert(row).first.value;
    m_index = {};
    return index;
}
}  // namespace urd

namespace urd
{
ygg::uint_t urd::builder<ygg::database::Relation>::insert(ygg::database::Row row)
{
    if (!std::ranges::equal(row.columns(), m_columns.span()))
        throw std::invalid_argument("Relation: row schema does not match.");
    const auto index = m_rows.insert(row.bytes()).first.value;
    m_index = {};
    return index;
}
}  // namespace urd

namespace urd
{
std::optional<ygg::uint_t> urd::builder<ygg::database::Relation>::find(std::span<const std::byte> row) const
{
    if (row.size() != m_columns.row_size())
        throw std::invalid_argument("Relation: row byte width does not match its schema.");
    return position_of(row);
}
}  // namespace urd

namespace urd
{
std::optional<ygg::uint_t> urd::builder<ygg::database::Relation>::find(ygg::database::Row row) const
{
    if (!std::ranges::equal(row.columns(), m_columns.span()))
        throw std::invalid_argument("Relation: row schema does not match.");
    return position_of(row.bytes());
}
}  // namespace urd

namespace urd
{
void urd::builder<ygg::database::Relation>::erase(size_t index)
{
    if (index >= size())
        throw std::out_of_range("Relation: row index out of range.");
    m_rows.erase(Rows::handle_type(static_cast<ygg::uint_t>(index)));
    m_index = {};
}
}  // namespace urd

namespace urd
{
std::span<const std::byte> urd::builder<ygg::database::Relation>::row(size_t index) const noexcept
{
    assert(index < size());
    return m_rows.view(Rows::handle_type(static_cast<ygg::uint_t>(index)));
}
}  // namespace urd

namespace urd
{
ygg::database::Row urd::builder<ygg::database::Relation>::at(size_t index) const
{
    if (index >= size())
        throw std::out_of_range("Relation: row index out of range.");
    return (*this)[index];
}
}  // namespace urd

namespace urd
{
void urd::builder<ygg::database::Relation>::clear() noexcept
{
    m_index = {};
    m_rows.clear();
}
}  // namespace urd

namespace urd
{
void urd::builder<ygg::database::Relation>::rename(std::span<const urd::index<ygg::database::Column>> columns)
{
    m_columns.rename(columns);
    m_index = {};
}
}  // namespace urd

namespace urd
{
void urd::builder<ygg::database::Relation>::rename(std::span<const ygg::database::ColumnLayout> columns)
{
    ygg::database::detail::validate_relabel(m_columns.span(), columns);
    m_columns.assign(columns);
    m_index = {};
}
}  // namespace urd

namespace urd
{
void urd::builder<ygg::database::Relation>::initialize(std::span<const ygg::database::ColumnLayout> columns)
{
    ygg::database::validate_columns(columns);
    reinitialize(ygg::database::row_size(columns), columns);
}
}  // namespace urd

namespace urd
{
void urd::builder<ygg::database::Relation>::initialize(std::span<const urd::index<ygg::database::Column>> columns)
{ reinitialize(ygg::database::detail::label_row_size<>(columns), columns); }
}  // namespace urd

namespace urd
{
void urd::builder<ygg::database::Relation>::initialize(std::initializer_list<urd::index<ygg::database::Column>> columns)
{ initialize(std::span<const urd::index<ygg::database::Column>>(columns)); }
}  // namespace urd

namespace urd
{
urd::builder<ygg::database::Relation>::builder(builder&&) noexcept = default;
urd::builder<ygg::database::Relation>& urd::builder<ygg::database::Relation>::operator=(builder&&) noexcept = default;
urd::builder<ygg::database::Relation>::~builder() = default;
}  // namespace urd
