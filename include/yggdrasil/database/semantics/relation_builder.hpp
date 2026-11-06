/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SEMANTICS_RELATION_BUILDER_HPP_
#define YGG_DATABASE_SEMANTICS_RELATION_BUILDER_HPP_

#include <urd/repr/sequences/sequence.hpp>
#include <urd/repr/pools/pools.hpp>
#include <urd/store.hpp>
#include <urd/elements/flat.hpp>
#include "yggdrasil/core/config.hpp"
#include "yggdrasil/database/declarations.hpp"
#include "yggdrasil/database/syntax/columns.hpp"
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
concept ColumnSchema = std::same_as<S, std::span<const ColumnLayout>> || std::same_as<S, std::span<const urd::index<Column>>>;
}  // namespace ygg::database

namespace urd
{
/// A set of canonical packed rows. The schema determines each column's type.
/// Erase moves the final row into its place; clear retains tuple and hash storage.
/// A zero-column relation is true exactly when it contains the empty tuple.
template<>
struct builder<ygg::database::Relation>
{
private:
    friend class ygg::database::RelationPool;

    urd::index<ygg::database::Relation> m_index;
    size_t m_storage_index = std::numeric_limits<size_t>::max();
    urd::builder<ygg::database::Columns> m_columns;
    /// The rows, interned in a urd pool (stored flat): a handle's value is the row's position.
    using Rows = urd::Store<urd::pool<urd::Store<urd::sequence<std::byte>, urd::frozen>>>;
    Rows m_rows;

    [[nodiscard]] std::optional<ygg::uint_t> position_of(std::span<const std::byte> row) const
    {
        const auto found = m_rows.find(row);
        return found.is_none() ? std::nullopt : std::optional<ygg::uint_t>(found.value);
    }

    template<ygg::database::ColumnSchema Schema>
    void reinitialize(size_t width, Schema columns);

public:
    explicit builder(urd::builder<ygg::database::Columns> columns);
    explicit builder(std::span<const ygg::database::ColumnLayout> columns);
    explicit builder(std::span<const urd::index<ygg::database::Column>> columns = {});
    builder(std::initializer_list<urd::index<ygg::database::Column>> columns);
    builder(const builder&) = delete;
    builder& operator=(const builder&) = delete;
    builder(builder&&) noexcept;
    builder& operator=(builder&&) noexcept;
    ~builder();

    [[nodiscard]] urd::index<ygg::database::Relation> get_index() const noexcept { return m_index; }
    void set_index(urd::index<ygg::database::Relation> index) noexcept { m_index = index; }
    /// Factory-local identity, stable across clear/initialize/rename.
    [[nodiscard]] size_t get_storage_index() const noexcept { return m_storage_index; }
    [[nodiscard]] const urd::builder<ygg::database::Columns>& columns() const& noexcept { return m_columns; }
    [[nodiscard]] const urd::builder<ygg::database::Columns>& columns() const&& = delete;
    [[nodiscard]] size_t arity() const noexcept { return m_columns.size(); }
    [[nodiscard]] size_t size() const noexcept { return m_rows.size(); }
    [[nodiscard]] bool empty() const noexcept { return m_rows.empty(); }
    [[nodiscard]] size_t memory_usage() const noexcept { return m_columns.memory_usage() + urd::memory_usage(m_rows); }
    [[nodiscard]] const auto& storage() const noexcept { return m_rows; }
    [[nodiscard]] const void* get_storage_address() const noexcept { return &m_rows; }
    [[nodiscard]] size_t column_index(urd::index<ygg::database::Column> column) const { return m_columns.column_index(column); }

    /// Raw bytes are checked for width and canonical encoding before mutation.
    ygg::uint_t insert(std::span<const std::byte> row);
    /// A borrowed row already has canonical bytes; its layout must match.
    ygg::uint_t insert(ygg::database::Row row);
    template<ygg::ByteCodable... Ts>
    ygg::uint_t insert(const std::tuple<Ts...>& values)
    {
        const auto bytes = ygg::database::encode_row(values, m_columns.span());
        return insert(ygg::database::Row(bytes, m_columns.span()));
    }

    [[nodiscard]] std::optional<ygg::uint_t> find(std::span<const std::byte> row) const;
    [[nodiscard]] std::optional<ygg::uint_t> find(ygg::database::Row row) const;
    template<ygg::ByteCodable... Ts>
    [[nodiscard]] std::optional<ygg::uint_t> find(const std::tuple<Ts...>& values) const
    {
        const auto bytes = ygg::database::encode_row(values, m_columns.span());
        return position_of(bytes);
    }
    [[nodiscard]] bool contains(std::span<const std::byte> row) const { return find(row).has_value(); }
    [[nodiscard]] bool contains(ygg::database::Row row) const { return find(row).has_value(); }
    template<ygg::ByteCodable... Ts>
    [[nodiscard]] bool contains(const std::tuple<Ts...>& values) const
    { return find(values).has_value(); }

    /// Invalidates the erased and moved rows' positions and borrowed rows.
    void erase(size_t index);
    [[nodiscard]] std::span<const std::byte> row(size_t index) const noexcept;
    ygg::database::Row operator[](size_t index) const noexcept { return { row(index), m_columns.span() }; }
    [[nodiscard]] ygg::database::Row at(size_t index) const;
    void clear() noexcept;
    void rename(std::span<const urd::index<ygg::database::Column>> columns);
    void rename(std::span<const ygg::database::ColumnLayout> columns);
    /// Clears rows and replaces the schema; equal byte widths retain row storage.
    void initialize(std::span<const ygg::database::ColumnLayout> columns);
    void initialize(std::span<const urd::index<ygg::database::Column>> columns);
    void initialize(std::initializer_list<urd::index<ygg::database::Column>> columns);

    /// The rows, in order, as byte spans.
    [[nodiscard]] auto all_rows() const noexcept
    { return std::views::iota(size_t { 0 }, size()) | std::views::transform([this](size_t i) { return row(i); }); }
    [[nodiscard]] std::span<const ygg::database::ColumnLayout> column_layouts() const noexcept { return columns().span(); }
    static constexpr auto identifying_members() noexcept { return urd::Members<&builder::column_layouts, &builder::all_rows> {}; }
};

template<ygg::database::ColumnSchema Schema>
void urd::builder<ygg::database::Relation>::reinitialize(size_t width, Schema columns)
{
    if (width != m_columns.row_size())
    {
        auto replacement = builder(columns);
        replacement.m_storage_index = m_storage_index;
        *this = std::move(replacement);
        return;
    }
    m_columns.assign(columns);
    clear();
}

}  // namespace urd
#endif
