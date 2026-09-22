/*
 * Copyright (C) 2026 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef YGG_DATABASE_DETAILS_RELATION_HPP_
#define YGG_DATABASE_DETAILS_RELATION_HPP_

#include "yggdrasil/database/relation.hpp"

#include <cassert>
#include <stdexcept>
#include <utility>

namespace ygg::database
{

template<TriviallyCopyable T>
RelationView<T>::RelationView(const RawArraySet<T>& rows, ColumnsView columns) : m_rows(&rows), m_columns(columns)
{
    if (columns.size() != rows.array_size())
        throw std::invalid_argument("RelationView: schema arity does not match row storage.");
}

template<TriviallyCopyable T>
template<typename C, size_t Extent>
    requires std::same_as<std::remove_const_t<C>, Column>
RelationView<T>::RelationView(const RawArraySet<T>& rows, std::span<C, Extent> columns) : RelationView(rows, ColumnsView(columns))
{
}

template<TriviallyCopyable T>
ColumnsView RelationView<T>::columns() const noexcept
{
    return m_columns;
}

template<TriviallyCopyable T>
size_t RelationView<T>::column_index(Column column) const
{
    return columns().column_index(column);
}

template<TriviallyCopyable T>
std::span<const T> RelationView<T>::operator[](size_t index) const noexcept
{
    assert(index < size());
    return (*m_rows)[static_cast<uint_t>(index)];
}

template<TriviallyCopyable T>
std::span<const T> RelationView<T>::at(size_t index) const
{
    if (index >= size())
        throw std::out_of_range("Relation: row index out of range.");
    return (*this)[index];
}

template<TriviallyCopyable T>
bool RelationView<T>::contains(std::span<const T> row) const
{
    return m_rows->contains(row);
}

template<TriviallyCopyable T>
bool RelationView<T>::contains(std::initializer_list<T> row) const
{
    return contains(std::span<const T>(row.begin(), row.size()));
}

template<TriviallyCopyable T>
Relation<T>::Relation(Columns columns) : m_columns(std::move(columns)), m_rows(m_columns.size())
{
}

template<TriviallyCopyable T>
Relation<T>::Relation(ColumnsView columns) : Relation(Columns(columns))
{
}

template<TriviallyCopyable T>
Relation<T>::Relation(std::span<const Column> columns) : Relation(Columns(columns))
{
}

template<TriviallyCopyable T>
Relation<T>::Relation(std::initializer_list<Column> columns) : Relation(Columns(columns))
{
}

template<TriviallyCopyable T>
ColumnsView Relation<T>::columns() const& noexcept
{
    return m_columns.view();
}

template<TriviallyCopyable T>
uint_t Relation<T>::insert(std::span<const T> row)
{
    return m_rows.insert(row);
}

template<TriviallyCopyable T>
uint_t Relation<T>::insert(std::initializer_list<T> row)
{
    return insert(std::span<const T>(row.begin(), row.size()));
}

template<TriviallyCopyable T>
bool Relation<T>::contains(std::span<const T> row) const
{
    return m_rows.contains(row);
}

template<TriviallyCopyable T>
bool Relation<T>::contains(std::initializer_list<T> row) const
{
    return contains(std::span<const T>(row.begin(), row.size()));
}

template<TriviallyCopyable T>
std::span<const T> Relation<T>::operator[](size_t index) const noexcept
{
    assert(index < size());
    return m_rows[static_cast<uint_t>(index)];
}

template<TriviallyCopyable T>
std::span<const T> Relation<T>::at(size_t index) const
{
    if (index >= size())
        throw std::out_of_range("Relation: row index out of range.");
    return (*this)[index];
}

template<TriviallyCopyable T>
void Relation<T>::clear() noexcept
{
    m_rows.clear();
}

template<TriviallyCopyable T>
void Relation<T>::rename(ColumnsView columns)
{
    if (columns.size() != arity())
        throw std::invalid_argument("Relation: rename requires matching arity.");
    m_columns.assign(columns);
}

template<TriviallyCopyable T>
void Relation<T>::initialize(ColumnsView columns)
{
    if (columns.size() != m_rows.array_size())
    {
        *this = Relation(columns);
        return;
    }
    m_columns.assign(columns);
    clear();
}

template<TriviallyCopyable T>
void Relation<T>::initialize(std::span<const Column> columns)
{
    initialize(ColumnsView(columns));
}

template<TriviallyCopyable T>
void Relation<T>::initialize(std::initializer_list<Column> columns)
{
    initialize(std::span<const Column>(columns));
}

template<TriviallyCopyable T>
RelationView<T> Relation<T>::view() const&
{
    return RelationView<T>(m_rows, m_columns.view());
}

}  // namespace ygg::database

#endif
