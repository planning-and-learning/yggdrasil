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

#include <cassert>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <tuple>

namespace ygg
{
/// A set of fixed-arity tuples. Values use ygg::Hash<T> and ygg::EqualTo<T>.
/// Rows are pooled, deduplicated, and immutable once inserted. A zero-column
/// relation is false when empty and true when it contains the empty tuple.
template<TriviallyCopyable T>
struct Builder<database::Relation<T>>
{
private:
    friend class database::RelationPool<T>;

    Index<database::Relation<T>> m_index;
    size_t m_storage_index = std::numeric_limits<size_t>::max();
    Builder<database::Columns> m_columns;
    RawArraySet<T> m_rows;

public:
    using ElementType = T;

    explicit Builder(Builder<database::Columns> columns);
    explicit Builder(std::span<const Index<database::Column>> columns = {});
    Builder(std::initializer_list<Index<database::Column>> columns);
    Builder(const Builder&) = delete;
    Builder& operator=(const Builder&) = delete;
    Builder(Builder&&) = default;
    Builder& operator=(Builder&&) = default;

    /// Last canonical record index; mutation invalidates it.
    Index<database::Relation<T>> get_index() const noexcept { return m_index; }
    void set_index(Index<database::Relation<T>> index) noexcept { m_index = index; }
    /// Factory-local row identity, stable across clear/initialize/rename.
    size_t get_storage_index() const noexcept { return m_storage_index; }
    const Builder<database::Columns>& columns() const& noexcept;
    const Builder<database::Columns>& columns() const&& = delete;
    size_t arity() const noexcept { return m_columns.size(); }
    size_t size() const noexcept { return m_rows.size(); }
    bool empty() const noexcept { return m_rows.empty(); }
    size_t memory_usage() const noexcept { return m_columns.memory_usage() + m_rows.memory_usage(); }
    const RawArraySet<T>& storage() const noexcept { return m_rows; }
    const void* get_storage_address() const noexcept { return &m_rows; }
    size_t column_index(Index<database::Column> column) const { return m_columns.column_index(column); }

    uint_t insert(std::span<const T> row);
    uint_t insert(std::initializer_list<T> row);
    template<SizedForwardRangeOf<T> R>
    uint_t insert(const R& row)
    {
        const auto index = m_rows.insert(row);
        ygg::clear(m_index);
        return index;
    }
    bool contains(std::span<const T> row) const;
    bool contains(std::initializer_list<T> row) const;
    template<SizedForwardRangeOf<T> R>
    bool contains(const R& row) const
    {
        return m_rows.contains(row);
    }

    std::span<const T> row(size_t index) const noexcept;
    std::span<const T> operator[](size_t index) const noexcept { return row(index); }
    std::span<const T> at(size_t index) const;

    /// Retains allocated tuple and hash-table storage for the next evaluation.
    void clear() noexcept;

    /// Relabels columns without changing rows or reallocating storage.
    /// Requires matching arity and invalidates borrowed schema views.
    void rename(std::span<const Index<database::Column>> columns);

    /// Clears rows and relabels columns, retaining storage when arity matches.
    /// Requires intact storage and invalidates outstanding views.
    /// Used by UniqueObjectPool on checkout.
    void initialize(std::span<const Index<database::Column>> columns);
    void initialize(std::initializer_list<Index<database::Column>> columns);

    auto identifying_members() const noexcept
    {
        return std::make_tuple(columns().span(), std::views::iota(size_t { 0 }, size()) | std::views::transform([this](size_t i) { return row(i); }));
    }
};

template<TriviallyCopyable T>
Builder<database::Relation<T>>::Builder(Builder<database::Columns> columns) : m_columns(std::move(columns)), m_rows(m_columns.size())
{
}

template<TriviallyCopyable T>
Builder<database::Relation<T>>::Builder(std::span<const Index<database::Column>> columns) : Builder(Builder<database::Columns>(columns))
{
}

template<TriviallyCopyable T>
Builder<database::Relation<T>>::Builder(std::initializer_list<Index<database::Column>> columns) : Builder(Builder<database::Columns>(columns))
{
}

template<TriviallyCopyable T>
const Builder<database::Columns>& Builder<database::Relation<T>>::columns() const& noexcept
{
    return m_columns;
}

template<TriviallyCopyable T>
uint_t Builder<database::Relation<T>>::insert(std::span<const T> row)
{
    const auto index = m_rows.insert(row);
    ygg::clear(m_index);
    return index;
}

template<TriviallyCopyable T>
uint_t Builder<database::Relation<T>>::insert(std::initializer_list<T> row)
{
    return insert(std::span<const T>(row.begin(), row.size()));
}

template<TriviallyCopyable T>
bool Builder<database::Relation<T>>::contains(std::span<const T> row) const
{
    return m_rows.contains(row);
}

template<TriviallyCopyable T>
bool Builder<database::Relation<T>>::contains(std::initializer_list<T> row) const
{
    return contains(std::span<const T>(row.begin(), row.size()));
}

template<TriviallyCopyable T>
std::span<const T> Builder<database::Relation<T>>::row(size_t index) const noexcept
{
    assert(index < size());
    return m_rows[static_cast<uint_t>(index)];
}

template<TriviallyCopyable T>
std::span<const T> Builder<database::Relation<T>>::at(size_t index) const
{
    if (index >= size())
        throw std::out_of_range("Relation: row index out of range.");
    return (*this)[index];
}

template<TriviallyCopyable T>
void Builder<database::Relation<T>>::clear() noexcept
{
    ygg::clear(m_index);
    m_rows.clear();
}

template<TriviallyCopyable T>
void Builder<database::Relation<T>>::rename(std::span<const Index<database::Column>> columns)
{
    if (columns.size() != arity())
        throw std::invalid_argument("Relation: rename requires matching arity.");
    m_columns.assign(columns);
    ygg::clear(m_index);
}

template<TriviallyCopyable T>
void Builder<database::Relation<T>>::initialize(std::span<const Index<database::Column>> columns)
{
    if (columns.size() != m_rows.array_size())
    {
        auto replacement = Builder(columns);
        replacement.m_storage_index = m_storage_index;
        *this = std::move(replacement);
        return;
    }
    m_columns.assign(columns);
    clear();
}

template<TriviallyCopyable T>
void Builder<database::Relation<T>>::initialize(std::initializer_list<Index<database::Column>> columns)
{
    initialize(std::span<const Index<database::Column>>(columns));
}

}  // namespace ygg

#endif
