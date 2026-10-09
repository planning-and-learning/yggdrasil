/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_RELATION_BUILDER_HPP_
#define YGG_DATABASE_RELATION_BUILDER_HPP_

#include "yggdrasil/containers/raw_array_set.hpp"
#include "yggdrasil/core/types_utils.hpp"
#include "yggdrasil/database/columns.hpp"
#include "yggdrasil/database/relation_index.hpp"
#include "yggdrasil/database/row.hpp"

#include <cassert>
#include <limits>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <tuple>

namespace ygg
{
/// A set of canonical packed rows. The schema determines each column's type.
/// Erase moves the final row into its place; clear retains tuple and hash storage.
/// A zero-column relation is true exactly when it contains the empty tuple.
template<database::ColumnTypes Values>
struct Builder<database::Relation<Values>>
{
private:
    friend class database::RelationPool<Values>;

    Index<database::Relation<Values>> m_index;
    size_t m_storage_index = std::numeric_limits<size_t>::max();
    Builder<database::Columns<Values>> m_columns;
    RawArraySet<std::byte, 64> m_rows;

public:
    explicit Builder(Builder<database::Columns<Values>> columns);
    explicit Builder(std::span<const database::ColumnLayout> columns);
    explicit Builder(std::span<const Index<database::Column>> columns = {});
    Builder(std::initializer_list<Index<database::Column>> columns);
    Builder(const Builder&) = delete;
    Builder& operator=(const Builder&) = delete;
    Builder(Builder&&) = default;
    Builder& operator=(Builder&&) = default;

    Index<database::Relation<Values>> get_index() const noexcept { return m_index; }
    void set_index(Index<database::Relation<Values>> index) noexcept { m_index = index; }
    /// Factory-local identity, stable across clear/initialize/rename.
    size_t get_storage_index() const noexcept { return m_storage_index; }
    const Builder<database::Columns<Values>>& columns() const& noexcept { return m_columns; }
    const Builder<database::Columns<Values>>& columns() const&& = delete;
    size_t arity() const noexcept { return m_columns.size(); }
    size_t size() const noexcept { return m_rows.size(); }
    bool empty() const noexcept { return m_rows.empty(); }
    size_t memory_usage() const noexcept { return m_columns.memory_usage() + m_rows.memory_usage(); }
    const auto& storage() const noexcept { return m_rows; }
    const void* get_storage_address() const noexcept { return &m_rows; }
    size_t column_index(Index<database::Column> column) const { return m_columns.column_index(column); }

    /// Raw bytes are checked for width and canonical encoding before mutation.
    uint_t insert(std::span<const std::byte> row);
    /// A borrowed row already has canonical bytes; its layout must match.
    uint_t insert(database::Row<Values> row);
    template<database::ColumnValueFor<Values>... Ts>
    uint_t insert(const std::tuple<Ts...>& values)
    {
        const auto bytes = database::encode_row<Values>(values, m_columns.span());
        return insert(database::Row<Values>(bytes, m_columns.span()));
    }

    std::optional<uint_t> find(std::span<const std::byte> row) const;
    std::optional<uint_t> find(database::Row<Values> row) const;
    template<database::ColumnValueFor<Values>... Ts>
    std::optional<uint_t> find(const std::tuple<Ts...>& values) const
    {
        const auto bytes = database::encode_row<Values>(values, m_columns.span());
        return m_rows.find(bytes);
    }
    bool contains(std::span<const std::byte> row) const { return find(row).has_value(); }
    bool contains(database::Row<Values> row) const { return find(row).has_value(); }
    template<database::ColumnValueFor<Values>... Ts>
    bool contains(const std::tuple<Ts...>& values) const
    {
        return find(values).has_value();
    }

    /// Invalidates the erased and moved rows' positions and borrowed rows.
    void erase(size_t index);
    std::span<const std::byte> row(size_t index) const noexcept;
    database::Row<Values> operator[](size_t index) const noexcept { return { row(index), m_columns.span() }; }
    database::Row<Values> at(size_t index) const;
    void clear() noexcept;
    void rename(std::span<const Index<database::Column>> columns);
    void rename(std::span<const database::ColumnLayout> columns);
    /// Clears rows and replaces the schema; equal byte widths retain row storage.
    void initialize(std::span<const database::ColumnLayout> columns);
    void initialize(std::span<const Index<database::Column>> columns);
    void initialize(std::initializer_list<Index<database::Column>> columns);

    auto identifying_members() const noexcept
    {
        return std::make_tuple(columns().span(), std::views::iota(size_t { 0 }, size()) | std::views::transform([this](size_t i) { return row(i); }));
    }
};

template<database::ColumnTypes Values>
Builder<database::Relation<Values>>::Builder(Builder<database::Columns<Values>> columns) : m_columns(std::move(columns)), m_rows(m_columns.row_size())
{
}

template<database::ColumnTypes Values>
Builder<database::Relation<Values>>::Builder(std::span<const database::ColumnLayout> columns) : Builder(Builder<database::Columns<Values>>(columns))
{
}

template<database::ColumnTypes Values>
Builder<database::Relation<Values>>::Builder(std::span<const Index<database::Column>> columns) : Builder(Builder<database::Columns<Values>>(columns))
{
}

template<database::ColumnTypes Values>
Builder<database::Relation<Values>>::Builder(std::initializer_list<Index<database::Column>> columns) :
    Builder(std::span<const Index<database::Column>>(columns))
{
}

template<database::ColumnTypes Values>
uint_t Builder<database::Relation<Values>>::insert(std::span<const std::byte> row)
{
    database::validate_row<Values>(row, m_columns.span());
    const auto index = m_rows.insert(row);
    ygg::clear(m_index);
    return index;
}

template<database::ColumnTypes Values>
uint_t Builder<database::Relation<Values>>::insert(database::Row<Values> row)
{
    if (!std::ranges::equal(row.columns(), m_columns.span()))
        throw std::invalid_argument("Relation: row schema does not match.");
    const auto index = m_rows.insert(row.bytes());
    ygg::clear(m_index);
    return index;
}

template<database::ColumnTypes Values>
std::optional<uint_t> Builder<database::Relation<Values>>::find(std::span<const std::byte> row) const
{
    database::validate_row<Values>(row, m_columns.span());
    return m_rows.find(row);
}

template<database::ColumnTypes Values>
std::optional<uint_t> Builder<database::Relation<Values>>::find(database::Row<Values> row) const
{
    if (!std::ranges::equal(row.columns(), m_columns.span()))
        throw std::invalid_argument("Relation: row schema does not match.");
    return m_rows.find(row.bytes());
}

template<database::ColumnTypes Values>
void Builder<database::Relation<Values>>::erase(size_t index)
{
    if (index >= size())
        throw std::out_of_range("Relation: row index out of range.");
    m_rows.erase(static_cast<uint_t>(index));
    ygg::clear(m_index);
}

template<database::ColumnTypes Values>
std::span<const std::byte> Builder<database::Relation<Values>>::row(size_t index) const noexcept
{
    assert(index < size());
    return m_rows[static_cast<uint_t>(index)];
}

template<database::ColumnTypes Values>
database::Row<Values> Builder<database::Relation<Values>>::at(size_t index) const
{
    if (index >= size())
        throw std::out_of_range("Relation: row index out of range.");
    return (*this)[index];
}

template<database::ColumnTypes Values>
void Builder<database::Relation<Values>>::clear() noexcept
{
    ygg::clear(m_index);
    m_rows.clear();
}

template<database::ColumnTypes Values>
void Builder<database::Relation<Values>>::rename(std::span<const Index<database::Column>> columns)
{
    m_columns.rename(columns);
    ygg::clear(m_index);
}

template<database::ColumnTypes Values>
void Builder<database::Relation<Values>>::rename(std::span<const database::ColumnLayout> columns)
{
    database::validate_columns<Values>(columns);
    if (columns.size() != arity())
        throw std::invalid_argument("Relation: rename requires matching arity.");
    for (size_t i = 0; i < columns.size(); ++i)
        if (columns[i].type != m_columns.span()[i].type)
            throw std::invalid_argument("Relation: rename cannot change column types.");
    m_columns.assign(columns);
    ygg::clear(m_index);
}

template<database::ColumnTypes Values>
void Builder<database::Relation<Values>>::initialize(std::span<const database::ColumnLayout> columns)
{
    database::validate_columns<Values>(columns);
    const auto width = database::row_size(columns);
    if (width != m_rows.array_size())
    {
        auto replacement = Builder(columns);
        replacement.m_storage_index = m_storage_index;
        *this = std::move(replacement);
        return;
    }
    m_columns.assign(columns);
    clear();
}

template<database::ColumnTypes Values>
void Builder<database::Relation<Values>>::initialize(std::span<const Index<database::Column>> columns)
{
    database::detail::validate_column_labels(columns);
    const auto field_width = database::column_size<Values>(0);
    if (columns.size() > std::numeric_limits<size_t>::max() / field_width)
        throw std::length_error("Columns: row byte width exceeds addressable memory.");
    const auto width = columns.size() * field_width;
    if (width != m_rows.array_size())
    {
        auto replacement = Builder(columns);
        replacement.m_storage_index = m_storage_index;
        *this = std::move(replacement);
        return;
    }
    m_columns.assign(columns);
    clear();
}

template<database::ColumnTypes Values>
void Builder<database::Relation<Values>>::initialize(std::initializer_list<Index<database::Column>> columns)
{
    initialize(std::span<const Index<database::Column>>(columns));
}
}  // namespace ygg
#endif
