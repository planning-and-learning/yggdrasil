/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_RELATION_VIEW_HPP_
#define YGG_DATABASE_RELATION_VIEW_HPP_

#include "yggdrasil/containers/span.hpp"
#include "yggdrasil/database/relation_builder.hpp"
#include "yggdrasil/database/relation_data.hpp"

#include <algorithm>
#include <cassert>
#include <compare>
#include <concepts>
#include <iterator>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

namespace ygg::database::detail
{
/// Keeps the lightweight relation view alive, independently of the range expression.
/// Comparing or subtracting iterators requires that they belong to the same sequence.
template<typename V>
class RelationIterator
{
    std::optional<V> m_view;
    std::ptrdiff_t m_position = 0;

public:
    using difference_type = std::ptrdiff_t;
    using value_type = std::remove_cvref_t<decltype(std::declval<const V&>()[size_t {}])>;
    using reference = value_type;
    using pointer = void;
    using iterator_category = std::random_access_iterator_tag;
    using iterator_concept = std::random_access_iterator_tag;

    RelationIterator() noexcept = default;
    RelationIterator(V view, size_t position) noexcept : m_view(view), m_position(static_cast<difference_type>(position)) {}
    reference operator*() const noexcept { return (*m_view)[static_cast<size_t>(m_position)]; }
    reference operator[](difference_type n) const noexcept { return *(*this + n); }
    RelationIterator& operator++() noexcept
    {
        ++m_position;
        return *this;
    }
    RelationIterator operator++(int) noexcept
    {
        auto old = *this;
        ++*this;
        return old;
    }
    RelationIterator& operator--() noexcept
    {
        --m_position;
        return *this;
    }
    RelationIterator operator--(int) noexcept
    {
        auto old = *this;
        --*this;
        return old;
    }
    RelationIterator& operator+=(difference_type n) noexcept
    {
        m_position += n;
        return *this;
    }
    RelationIterator& operator-=(difference_type n) noexcept
    {
        m_position -= n;
        return *this;
    }
    friend RelationIterator operator+(RelationIterator it, difference_type n) noexcept { return it += n; }
    friend RelationIterator operator+(difference_type n, RelationIterator it) noexcept { return it += n; }
    friend RelationIterator operator-(RelationIterator it, difference_type n) noexcept { return it -= n; }
    friend difference_type operator-(const RelationIterator& lhs, const RelationIterator& rhs) noexcept { return lhs.m_position - rhs.m_position; }
    friend bool operator==(const RelationIterator& lhs, const RelationIterator& rhs) noexcept { return lhs.m_position == rhs.m_position; }
    friend auto operator<=>(const RelationIterator& lhs, const RelationIterator& rhs) noexcept { return lhs.m_position <=> rhs.m_position; }
};
}  // namespace ygg::database::detail

namespace ygg
{
template<TriviallyCopyable T, typename C>
class View<Builder<database::Relation<T>>, C>
{
    const C* m_context;
    const Builder<database::Relation<T>>* m_handle;

public:
    View(const Builder<database::Relation<T>>& handle, const C& context) noexcept : m_context(&context), m_handle(&handle) {}
    const auto& get_handle() const noexcept { return *m_handle; }
    const auto& get_data() const noexcept { return *m_handle; }
    using ElementType = T;
    const auto& get_context() const noexcept { return *m_context; }
    auto get_index() const noexcept { return m_handle->get_index(); }
    auto columns() const noexcept { return make_view(m_handle->columns(), *m_context); }
    size_t arity() const noexcept { return m_handle->arity(); }
    size_t size() const noexcept { return m_handle->size(); }
    bool empty() const noexcept { return m_handle->empty(); }
    const auto& storage() const noexcept { return m_handle->storage(); }
    const void* get_storage_address() const noexcept { return m_handle->get_storage_address(); }
    size_t get_storage_index() const noexcept { return m_handle->get_storage_index(); }
    size_t column_index(Index<database::Column> column) const { return m_handle->column_index(column); }
    std::span<const T> row(size_t index) const noexcept { return m_handle->row(index); }
    auto operator[](size_t index) const noexcept { return make_view(row(index), get_repository(*m_context)); }
    auto at(size_t index) const
    {
        if (index >= size())
            throw std::out_of_range("Relation: row index out of range.");
        return (*this)[index];
    }
    auto begin() const noexcept { return database::detail::RelationIterator<View>(*this, 0); }
    auto end() const noexcept { return database::detail::RelationIterator<View>(*this, size()); }
    auto cbegin() const noexcept { return begin(); }
    auto cend() const noexcept { return end(); }

    template<SizedForwardRangeOf<T> R>
    bool contains(const R& row) const
    {
        return m_handle->contains(row);
    }

    bool contains(std::span<const T> row) const { return m_handle->contains(row); }
    bool contains(std::initializer_list<T> row) const { return m_handle->contains(row); }
};

template<TriviallyCopyable T, typename C>
class View<Data<database::Relation<T>>, C>
{
    const C* m_context;
    const Data<database::Relation<T>>* m_handle;

    const auto& repository() const noexcept { return get_relation_repository(*m_context); }

public:
    using ElementType = T;

    View(const Data<database::Relation<T>>& handle, const C& context) noexcept : m_context(&context), m_handle(&handle) {}
    const auto& get_handle() const noexcept { return *m_handle; }
    const auto& get_data() const noexcept { return *m_handle; }
    const auto& get_context() const noexcept { return *m_context; }
    auto get_index() const noexcept { return get_data().index; }
    auto columns() const noexcept { return repository().get_columns(get_data().columns_index); }
    size_t arity() const noexcept { return columns().size(); }
    size_t size() const noexcept { return row_indices().size(); }
    bool empty() const noexcept { return row_indices().empty(); }
    std::span<const Index<database::RelationRow<T>>> row_indices() const noexcept
    {
        return repository().get_row_set_repository()[get_data().row_set_index.get_value()];
    }
    const void* get_storage_address() const noexcept { return row_indices().data(); }
    size_t get_storage_index() const noexcept { return repository().get_storage_index(get_data().row_set_index); }
    size_t column_index(Index<database::Column> column) const { return columns().column_index(column); }

    std::span<const T> row(size_t index) const noexcept
    {
        assert(index < size());
        return repository().get_row_repository()[row_indices()[index].get_value()];
    }

    auto operator[](size_t index) const noexcept { return make_view(row(index), get_repository(*m_context)); }

    auto at(size_t index) const
    {
        if (index >= size())
            throw std::out_of_range("Relation: row index out of range.");
        return (*this)[index];
    }

    auto begin() const noexcept { return database::detail::RelationIterator<View>(*this, 0); }
    auto end() const noexcept { return database::detail::RelationIterator<View>(*this, size()); }
    auto cbegin() const noexcept { return begin(); }
    auto cend() const noexcept { return end(); }

    template<SizedForwardRangeOf<T> R>
    bool contains(const R& row) const
    {
        if (std::ranges::size(row) != arity())
            throw std::invalid_argument("Relation: row arity does not match schema.");
        const auto index = repository().get_row_repository().find(row);
        return index && std::ranges::binary_search(row_indices(), Index<database::RelationRow<T>>(*index));
    }

    bool contains(std::span<const T> row) const { return contains<std::span<const T>>(row); }

    bool contains(std::initializer_list<T> row) const { return contains(std::span<const T>(row.begin(), row.size())); }
};

template<TriviallyCopyable T, typename C>
class View<Index<database::Relation<T>>, C>
{
    const C* m_context;
    Index<database::Relation<T>> m_handle;

    const auto& repository() const noexcept { return get_relation_repository(*m_context); }

public:
    using ElementType = T;

    View(Index<database::Relation<T>> handle, const C& context) noexcept : m_context(&context), m_handle(handle) {}
    const auto& get_handle() const noexcept { return m_handle; }
    const auto& get_data() const noexcept { return repository()[m_handle]; }
    const auto& get_context() const noexcept { return *m_context; }
    auto get_index() const noexcept { return m_handle; }
    auto columns() const noexcept { return repository().get_columns(get_data().columns_index); }
    size_t arity() const noexcept { return columns().size(); }
    size_t size() const noexcept { return row_indices().size(); }
    bool empty() const noexcept { return row_indices().empty(); }
    std::span<const Index<database::RelationRow<T>>> row_indices() const noexcept
    {
        return repository().get_row_set_repository()[get_data().row_set_index.get_value()];
    }
    const void* get_storage_address() const noexcept { return row_indices().data(); }
    size_t get_storage_index() const noexcept { return repository().get_storage_index(get_data().row_set_index); }
    size_t column_index(Index<database::Column> column) const { return columns().column_index(column); }

    std::span<const T> row(size_t index) const noexcept
    {
        assert(index < size());
        return repository().get_row_repository()[row_indices()[index].get_value()];
    }

    auto operator[](size_t index) const noexcept { return make_view(row(index), get_repository(*m_context)); }

    auto at(size_t index) const
    {
        if (index >= size())
            throw std::out_of_range("Relation: row index out of range.");
        return (*this)[index];
    }

    auto begin() const noexcept { return database::detail::RelationIterator<View>(*this, 0); }
    auto end() const noexcept { return database::detail::RelationIterator<View>(*this, size()); }
    auto cbegin() const noexcept { return begin(); }
    auto cend() const noexcept { return end(); }

    template<SizedForwardRangeOf<T> R>
    bool contains(const R& row) const
    {
        if (std::ranges::size(row) != arity())
            throw std::invalid_argument("Relation: row arity does not match schema.");
        const auto index = repository().get_row_repository().find(row);
        return index && std::ranges::binary_search(row_indices(), Index<database::RelationRow<T>>(*index));
    }

    bool contains(std::span<const T> row) const { return contains<std::span<const T>>(row); }

    bool contains(std::initializer_list<T> row) const { return contains(std::span<const T>(row.begin(), row.size())); }
    auto identifying_members() const noexcept { return std::make_tuple(m_handle, repository().get_index()); }
};
}  // namespace ygg

namespace ygg::database
{
/// Raw read-only row access shared by builders and contextual Builder/Data/Index views.
/// Contextual indexing and iteration may resolve stored elements into semantic views.
template<typename V, typename T = void>
concept RelationViewConcept = requires(const V& view, size_t i, Index<Column> column, std::span<const typename V::ElementType> row) {
    typename V::ElementType;
    requires TriviallyCopyable<typename V::ElementType>;
    requires std::same_as<T, void> || std::same_as<T, typename V::ElementType>;
    { view.columns() } -> ColumnsViewConcept;
    { view.arity() } -> std::same_as<size_t>;
    { view.size() } -> std::same_as<size_t>;
    { view.empty() } -> std::same_as<bool>;
    { view.get_storage_address() } -> std::same_as<const void*>;
    { view.get_storage_index() } -> std::same_as<size_t>;
    { view.column_index(column) } -> std::same_as<size_t>;
    { view.row(i) } -> std::same_as<std::span<const typename V::ElementType>>;
    { view.contains(row) } -> std::same_as<bool>;
};

}  // namespace ygg::database

namespace std::ranges
{
template<ygg::TriviallyCopyable T, typename C>
inline constexpr bool enable_borrowed_range<ygg::View<ygg::Builder<ygg::database::Relation<T>>, C>> = true;
template<ygg::TriviallyCopyable T, typename C>
inline constexpr bool enable_borrowed_range<ygg::View<ygg::Data<ygg::database::Relation<T>>, C>> = true;
template<ygg::TriviallyCopyable T, typename C>
inline constexpr bool enable_borrowed_range<ygg::View<ygg::Index<ygg::database::Relation<T>>, C>> = true;
}

#endif
