/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SEMANTICS_RELATION_BUILDER_HPP_
#define YGG_DATABASE_SEMANTICS_RELATION_BUILDER_HPP_

#include "yggdrasil/containers/raw_array_set.hpp"
#include "yggdrasil/core/types_utils.hpp"
#include "yggdrasil/database/syntax/columns.hpp"
#include "yggdrasil/database/declarations.hpp"
#include "yggdrasil/database/syntax/row.hpp"

#include <cassert>
#include <concepts>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <tuple>

namespace ygg::database
{
/// A relation schema, given by its column layouts or by its column labels.
template<typename S>
concept ColumnSchema = std::same_as<S, std::span<const ColumnLayout>> || std::same_as<S, std::span<const Index<Column>>>;
}  // namespace ygg::database

namespace ygg
{
/// A set of canonical packed rows. The schema determines each column's type.
/// Erase moves the final row into its place; clear retains tuple and hash storage.
/// A zero-column relation is true exactly when it contains the empty tuple.
template<>
struct Builder<database::Relation>
{
private:
    friend class database::RelationPool;

    Index<database::Relation> m_index;
    size_t m_storage_index = std::numeric_limits<size_t>::max();
    Builder<database::Columns> m_columns;
    RawArraySet<std::byte, 64> m_rows;

    template<database::ColumnSchema Schema>
    void reinitialize(size_t width, Schema columns);

public:
    explicit Builder(Builder<database::Columns> columns);
    explicit Builder(std::span<const database::ColumnLayout> columns);
    explicit Builder(std::span<const Index<database::Column>> columns = {});
    Builder(std::initializer_list<Index<database::Column>> columns);
    Builder(const Builder&) = delete;
    Builder& operator=(const Builder&) = delete;
    Builder(Builder&&) noexcept;
    Builder& operator=(Builder&&) noexcept;
    ~Builder();

    Index<database::Relation> get_index() const noexcept { return m_index; }
    void set_index(Index<database::Relation> index) noexcept { m_index = index; }
    /// Factory-local identity, stable across clear/initialize/rename.
    size_t get_storage_index() const noexcept { return m_storage_index; }
    const Builder<database::Columns>& columns() const& noexcept { return m_columns; }
    const Builder<database::Columns>& columns() const&& = delete;
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
    uint_t insert(database::Row row);
    template<database::ColumnValue... Ts>
    uint_t insert(const std::tuple<Ts...>& values)
    {
        const auto bytes = database::encode_row(values, m_columns.span());
        return insert(database::Row(bytes, m_columns.span()));
    }

    std::optional<uint_t> find(std::span<const std::byte> row) const;
    std::optional<uint_t> find(database::Row row) const;
    template<database::ColumnValue... Ts>
    std::optional<uint_t> find(const std::tuple<Ts...>& values) const
    {
        const auto bytes = database::encode_row(values, m_columns.span());
        return m_rows.find(bytes);
    }
    bool contains(std::span<const std::byte> row) const { return find(row).has_value(); }
    bool contains(database::Row row) const { return find(row).has_value(); }
    template<database::ColumnValue... Ts>
    bool contains(const std::tuple<Ts...>& values) const
    {
        return find(values).has_value();
    }

    /// Invalidates the erased and moved rows' positions and borrowed rows.
    void erase(size_t index);
    std::span<const std::byte> row(size_t index) const noexcept;
    database::Row operator[](size_t index) const noexcept { return { row(index), m_columns.span() }; }
    database::Row at(size_t index) const;
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

template<database::ColumnSchema Schema>
void Builder<database::Relation>::reinitialize(size_t width, Schema columns)
{
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

}  // namespace ygg
#endif
