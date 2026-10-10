/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SYNTAX_COLUMNS_BUILDER_HPP_
#define YGG_DATABASE_SYNTAX_COLUMNS_BUILDER_HPP_

#include "yggdrasil/database/syntax/columns_data.hpp"

#include <cstddef>
#include <initializer_list>
#include <span>
#include <tuple>

namespace ygg::database
{
/// Validate labels, registered types, and contiguous packed offsets.
template<ColumnTypes Values>
void validate_columns(std::span<const ColumnLayout> columns);
size_t column_index(std::span<const ColumnLayout> columns, Index<Column> column);
size_t row_size(std::span<const ColumnLayout> columns) noexcept;
/// The schema is already validated; validate the width and canonical field encodings.
template<ColumnTypes Values>
void validate_row(std::span<const std::byte> bytes, std::span<const ColumnLayout> columns);

namespace detail
{
template<ColumnTypes Values, ColumnValueFor<Values> T>
T read_column(std::span<const std::byte> bytes, std::span<const ColumnLayout> columns, size_t position);
template<ColumnTypes Values, ColumnValueFor<Values> T>
void write_column(std::span<std::byte> bytes, std::span<const ColumnLayout> columns, size_t position, T value);
}  // namespace detail
}  // namespace ygg::database

namespace ygg
{
/// Owns a validated typed schema. Changing it clears its canonical index and
/// retains column storage. Row byte width is independent of logical arity.
template<database::ColumnTypes Values>
struct Builder<database::Columns<Values>>
{
private:
    Data<database::Columns<Values>> m_data;

public:
    Builder() = default;
    explicit Builder(std::span<const database::ColumnLayout> columns);
    Builder(std::initializer_list<database::ColumnLayout> columns);
    /// Labels alone select the first registered type for every column.
    explicit Builder(std::span<const Index<database::Column>> columns);
    Builder(std::initializer_list<Index<database::Column>> columns);

    auto& get_data() noexcept { return m_data; }
    const auto& get_data() const noexcept { return m_data; }
    auto get_index() const noexcept { return m_data.index; }
    void set_index(Index<database::Columns<Values>> index) noexcept { m_data.index = index; }

    std::span<const database::ColumnLayout> span() const& noexcept { return { m_data.values.data(), m_data.values.size() }; }
    std::span<const database::ColumnLayout> span() const&& = delete;
    auto begin() const noexcept { return m_data.values.begin(); }
    auto end() const noexcept { return m_data.values.end(); }
    const database::ColumnLayout* data() const noexcept { return m_data.values.data(); }
    size_t size() const noexcept { return m_data.values.size(); }
    bool empty() const noexcept { return m_data.values.empty(); }
    const database::ColumnLayout& operator[](size_t index) const noexcept { return m_data.values[index]; }
    size_t column_index(Index<database::Column> column) const { return database::column_index(span(), column); }
    size_t row_size() const noexcept { return database::row_size(span()); }
    size_t memory_usage() const noexcept { return m_data.values.allocated_size_ * sizeof(database::ColumnLayout); }

    template<database::ColumnValueFor<Values> T>
    void push_back(Index<database::Column> column);
    void assign(std::span<const database::ColumnLayout> columns);
    void assign(std::initializer_list<database::ColumnLayout> columns);
    void assign(std::span<const Index<database::Column>> columns);
    void assign(std::initializer_list<Index<database::Column>> columns);
    void initialize(std::span<const database::ColumnLayout> columns) { assign(columns); }
    void initialize(std::initializer_list<database::ColumnLayout> columns) { assign(columns); }
    void initialize(std::span<const Index<database::Column>> columns) { assign(columns); }
    void initialize(std::initializer_list<Index<database::Column>> columns) { assign(columns); }
    /// Relabel without changing types or layout; validates before mutating.
    void rename(std::span<const Index<database::Column>> columns);
    void rename(std::initializer_list<Index<database::Column>> columns) { rename(std::span<const Index<database::Column>>(columns)); }
    void clear() noexcept { m_data.clear(); }

    template<database::ColumnValueFor<Values> T>
    T get(std::span<const std::byte> bytes, Index<database::Column> column) const
    {
        return database::detail::read_column<Values, T>(bytes, span(), column_index(column));
    }
    template<database::ColumnValueFor<Values> T>
    void set(std::span<std::byte> bytes, Index<database::Column> column, T value) const
    {
        database::detail::write_column<Values>(bytes, span(), column_index(column), value);
    }
    void validate_row(std::span<const std::byte> bytes) const { database::validate_row<Values>(bytes, span()); }

    auto cista_members() noexcept { return std::tie(m_data); }
    auto cista_members() const noexcept { return std::tie(m_data); }
    auto identifying_members() const noexcept { return m_data.identifying_members(); }
};
}  // namespace ygg

#include "yggdrasil/database/syntax/columns_view.hpp"

#endif
