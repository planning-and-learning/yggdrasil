/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_COLUMNS_BUILDER_HPP_
#define YGG_DATABASE_COLUMNS_BUILDER_HPP_

#include "yggdrasil/database/columns_data.hpp"

#include <cstddef>
#include <initializer_list>
#include <span>
#include <tuple>

namespace ygg
{
/// Owns a validated schema. Assigning labels retains allocation where possible
/// and invalidates borrowed schema spans and the last canonical index.
template<>
struct Builder<database::Columns>
{
private:
    Data<database::Columns> m_data;

public:
    Builder() = default;
    explicit Builder(std::span<const Index<database::Column>> columns);
    Builder(std::initializer_list<Index<database::Column>> columns);

    auto& get_data() noexcept { return m_data; }
    const auto& get_data() const noexcept { return m_data; }
    auto get_index() const noexcept { return m_data.index; }
    void set_index(Index<database::Columns> index) noexcept { m_data.index = index; }

    std::span<const Index<database::Column>> span() const& noexcept { return { m_data.values.data(), m_data.values.size() }; }
    std::span<const Index<database::Column>> span() const&& = delete;
    auto begin() const noexcept { return m_data.values.begin(); }
    auto end() const noexcept { return m_data.values.end(); }
    const Index<database::Column>* data() const noexcept { return m_data.values.data(); }
    size_t size() const noexcept { return m_data.values.size(); }
    bool empty() const noexcept { return m_data.values.empty(); }
    Index<database::Column> operator[](size_t index) const noexcept { return m_data.values[index]; }
    size_t column_index(Index<database::Column> column) const;
    size_t memory_usage() const noexcept { return m_data.values.allocated_size_ * sizeof(Index<database::Column>); }

    void assign(std::span<const Index<database::Column>> columns);
    void assign(std::initializer_list<Index<database::Column>> columns);
    void initialize(std::span<const Index<database::Column>> columns);
    void initialize(std::initializer_list<Index<database::Column>> columns);
    void clear() noexcept { m_data.clear(); }

    auto cista_members() noexcept { return std::tie(m_data); }
    auto cista_members() const noexcept { return std::tie(m_data); }
    auto identifying_members() const noexcept { return m_data.identifying_members(); }
};
}  // namespace ygg

#include "yggdrasil/database/columns_view.hpp"

#endif
