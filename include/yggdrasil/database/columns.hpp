/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_COLUMNS_HPP_
#define YGG_DATABASE_COLUMNS_HPP_

#include "yggdrasil/core/config.hpp"

#include <concepts>
#include <cstddef>
#include <initializer_list>
#include <span>
#include <type_traits>
#include <vector>

namespace ygg::database
{

/// Query-local variable identifiers. Labels, not positions or object values.
using Column = uint_t;

class Columns;

/// Validated, ordered, unique labels. Borrowed labels must stay alive and
/// unchanged. Raw storage requires an explicit span; owning temporaries cannot
/// accidentally become borrowed schemas.
class ColumnsView
{
private:
    std::span<const Column> m_columns;

public:
    ColumnsView() = default;

    template<typename C, size_t Extent>
        requires std::same_as<std::remove_const_t<C>, Column>
    explicit ColumnsView(std::span<C, Extent> columns);

    ColumnsView(const Columns& columns) noexcept;
    ColumnsView(Columns&&) = delete;
    ColumnsView(const Columns&&) = delete;

    std::span<const Column> span() const noexcept { return m_columns; }
    operator std::span<const Column>() const noexcept;
    auto begin() const noexcept { return m_columns.begin(); }
    auto end() const noexcept { return m_columns.end(); }
    const Column* data() const noexcept { return m_columns.data(); }
    size_t size() const noexcept { return m_columns.size(); }
    bool empty() const noexcept { return m_columns.empty(); }
    Column operator[](size_t index) const noexcept { return m_columns[index]; }

    size_t column_index(Column column) const;
};

/// Owns a validated schema. Labels are read-only; replacing a schema preserves
/// its allocation where possible and invalidates outstanding borrowed views.
class Columns
{
private:
    std::vector<Column> m_columns;
    friend class ColumnsView;

public:
    explicit Columns(std::vector<Column> columns = {});
    Columns(std::initializer_list<Column> columns);
    explicit Columns(ColumnsView columns);

    ColumnsView view() const& noexcept;
    ColumnsView view() const&& = delete;
    size_t size() const noexcept { return m_columns.size(); }
    bool empty() const noexcept { return m_columns.empty(); }
    size_t memory_usage() const noexcept { return m_columns.capacity() * sizeof(Column); }
    size_t column_index(Column column) const;

    void assign(ColumnsView columns);
};

}  // namespace ygg::database

#include "yggdrasil/database/details/columns.hpp"

#endif
