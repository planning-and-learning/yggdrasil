/*
 * Copyright (C) 2026 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef YGG_DATABASE_RELATION_HPP_
#define YGG_DATABASE_RELATION_HPP_

#include "yggdrasil/containers/raw_array_set.hpp"
#include "yggdrasil/database/columns.hpp"

#include <concepts>
#include <cstddef>
#include <initializer_list>
#include <limits>
#include <span>
#include <type_traits>

namespace ygg::database
{

template<TriviallyCopyable T>
class RelationPool;

/// Borrows immutable row access and schema. Both must outlive the view; row
/// storage must remain stationary and schema labels unchanged. Neither may be
/// concurrently modified.
template<TriviallyCopyable T = uint_t>
class RelationView
{
private:
    const RawArraySet<T>* m_rows;
    ColumnsView m_columns;
    size_t m_index;

public:
    RelationView(const RawArraySet<T>& rows, ColumnsView columns, size_t index = std::numeric_limits<size_t>::max());

    /// Borrowing requires an explicit span; containers cannot convert implicitly.
    template<typename C, size_t Extent>
        requires std::same_as<std::remove_const_t<C>, Column>
    RelationView(const RawArraySet<T>& rows, std::span<C, Extent> columns, size_t index = std::numeric_limits<size_t>::max());

    template<typename SchemaType>
    RelationView(RawArraySet<T>&&, SchemaType&&, size_t = std::numeric_limits<size_t>::max()) = delete;
    template<typename SchemaType>
    RelationView(const RawArraySet<T>&&, SchemaType&&, size_t = std::numeric_limits<size_t>::max()) = delete;
    RelationView(RawArraySet<T>&&, std::initializer_list<Column>, size_t = std::numeric_limits<size_t>::max()) = delete;
    RelationView(const RawArraySet<T>&&, std::initializer_list<Column>, size_t = std::numeric_limits<size_t>::max()) = delete;

    /// Factory-local row-storage identity; max() means no factory assigned it.
    size_t get_index() const noexcept { return m_index; }
    ColumnsView columns() const noexcept;
    size_t arity() const noexcept { return columns().size(); }
    size_t size() const noexcept { return m_rows->size(); }
    bool empty() const noexcept { return m_rows->empty(); }
    const RawArraySet<T>& storage() const noexcept { return *m_rows; }

    size_t column_index(Column column) const;

    std::span<const T> operator[](size_t index) const noexcept;
    std::span<const T> at(size_t index) const;
    bool contains(std::span<const T> row) const;
    bool contains(std::initializer_list<T> row) const;
};

/// A set of fixed-arity tuples. Values use ygg::Hash<T> and ygg::EqualTo<T>.
/// Rows are pooled, deduplicated, and immutable once inserted. A zero-column
/// relation is false when empty and true when it contains the empty tuple.
template<TriviallyCopyable T = uint_t>
class Relation
{
private:
    friend class RelationPool<T>;

    size_t m_index = std::numeric_limits<size_t>::max();
    Columns m_columns;
    RawArraySet<T> m_rows;

public:
    explicit Relation(Columns columns);
    explicit Relation(ColumnsView columns);
    explicit Relation(std::span<const Column> columns = {});
    Relation(std::initializer_list<Column> columns);
    Relation(const Relation&) = delete;
    Relation& operator=(const Relation&) = delete;
    Relation(Relation&&) = default;
    Relation& operator=(Relation&&) = default;

    /// Stable across clear/initialize/rename; unique within the creating factory.
    /// Directly constructed relations have max() and cannot be indexed by JoinIndex.
    size_t get_index() const noexcept { return m_index; }
    ColumnsView columns() const& noexcept;
    ColumnsView columns() const&& = delete;
    size_t arity() const noexcept { return m_columns.size(); }
    size_t size() const noexcept { return m_rows.size(); }
    bool empty() const noexcept { return m_rows.empty(); }
    size_t memory_usage() const noexcept { return m_columns.memory_usage() + m_rows.memory_usage(); }
    const RawArraySet<T>& storage() const noexcept { return m_rows; }

    uint_t insert(std::span<const T> row);
    uint_t insert(std::initializer_list<T> row);
    bool contains(std::span<const T> row) const;
    bool contains(std::initializer_list<T> row) const;

    std::span<const T> operator[](size_t index) const noexcept;
    std::span<const T> at(size_t index) const;

    /// Retains allocated tuple and hash-table storage for the next evaluation.
    void clear() noexcept;

    /// Relabels columns without changing rows or reallocating storage.
    /// Requires matching arity and invalidates borrowed schema views.
    void rename(ColumnsView columns);

    /// Clears rows and relabels columns, retaining storage when arity matches.
    /// Requires intact storage and invalidates outstanding views.
    /// Used by UniqueObjectPool on checkout.
    void initialize(ColumnsView columns);
    void initialize(std::span<const Column> columns);
    void initialize(std::initializer_list<Column> columns);

    /// Borrows both rows and schema without allocating.
    RelationView<T> view() const&;
    RelationView<T> view() const&& = delete;
};

}  // namespace ygg::database

#include "yggdrasil/database/details/relation.hpp"

#endif
