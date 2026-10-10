/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SEMANTICS_RELATION_VIEW_HPP_
#define YGG_DATABASE_SEMANTICS_RELATION_VIEW_HPP_

#include "yggdrasil/containers/span.hpp"
#include "yggdrasil/database/semantics/relation_builder.hpp"
#include "yggdrasil/database/semantics/relation_data.hpp"

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

namespace ygg::database
{
/// Raw read-only row access shared by builders and contextual Builder/Data/Index views.
/// Contextual indexing and iteration may resolve stored elements into semantic views.
template<typename V, typename Values>
concept RelationViewConcept =
    ColumnTypes<Values>
    && requires(const std::remove_reference_t<V>& view, size_t i, Index<Column> column, std::span<const std::byte> row, Row<Values> typed_row) {
           { view.columns() } -> ColumnsViewConcept<Values>;
           { view.arity() } -> std::same_as<size_t>;
           { view.size() } -> std::same_as<size_t>;
           { view.empty() } -> std::same_as<bool>;
           { view.get_storage_address() } -> std::same_as<const void*>;
           { view.get_storage_index() } -> std::same_as<size_t>;
           { view.column_index(column) } -> std::same_as<size_t>;
           { view.row(i) } -> std::same_as<std::span<const std::byte>>;
           { view.contains(row) } -> std::same_as<bool>;
           { view.contains(typed_row) } -> std::same_as<bool>;
       };

/// Sized random-access inputs whose elements, possibly references, are relation views.
template<typename R, typename Values>
concept RelationViewRange = std::ranges::random_access_range<const R> && std::ranges::sized_range<const R>
                            && RelationViewConcept<std::ranges::range_reference_t<const R>, Values>;

}  // namespace ygg::database

namespace ygg::database::detail
{
/// Keeps the lightweight relation view alive, independently of the range expression.
/// Comparing or subtracting iterators requires that they belong to the same sequence.
template<ColumnTypes Values, RelationViewConcept<Values> V>
    requires std::copyable<V> && requires(const V& view, size_t position) { view[position]; }
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
    RelationIterator(V view, size_t position) noexcept(std::is_nothrow_copy_constructible_v<V>) :
        m_view(view),
        m_position(static_cast<difference_type>(position))
    {
    }
    reference operator*() const { return (*m_view)[static_cast<size_t>(m_position)]; }
    reference operator[](difference_type n) const { return (*m_view)[static_cast<size_t>(m_position + n)]; }
    RelationIterator& operator++() noexcept
    {
        ++m_position;
        return *this;
    }
    RelationIterator operator++(int)
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
    RelationIterator operator--(int)
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
    friend RelationIterator operator+(RelationIterator it, difference_type n) { return it += n; }
    friend RelationIterator operator+(difference_type n, RelationIterator it) { return it += n; }
    friend RelationIterator operator-(RelationIterator it, difference_type n) { return it -= n; }
    friend difference_type operator-(const RelationIterator& lhs, const RelationIterator& rhs) noexcept { return lhs.m_position - rhs.m_position; }
    friend bool operator==(const RelationIterator& lhs, const RelationIterator& rhs) noexcept { return lhs.m_position == rhs.m_position; }
    friend auto operator<=>(const RelationIterator& lhs, const RelationIterator& rhs) noexcept { return lhs.m_position <=> rhs.m_position; }
};
}  // namespace ygg::database::detail

namespace ygg
{
template<database::ColumnTypes Values, typename C>
class View<Builder<database::Relation<Values>>, C>
{
    const C* m_context;
    const Builder<database::Relation<Values>>* m_handle;

public:
    View(const Builder<database::Relation<Values>>& handle, const C& context) noexcept : m_context(&context), m_handle(&handle) {}
    const auto& get_handle() const noexcept { return *m_handle; }
    const auto& get_data() const noexcept { return *m_handle; }
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
    std::span<const std::byte> row(size_t index) const noexcept { return m_handle->row(index); }
    auto operator[](size_t index) const noexcept { return make_view(database::Row<Values>(row(index), columns().span()), get_repository(*m_context)); }
    auto at(size_t index) const
    {
        if (index >= size())
            throw std::out_of_range("Relation: row index out of range.");
        return (*this)[index];
    }
    auto begin() const noexcept { return database::detail::RelationIterator<Values, View>(*this, 0); }
    auto end() const noexcept { return database::detail::RelationIterator<Values, View>(*this, size()); }
    auto cbegin() const noexcept { return begin(); }
    auto cend() const noexcept { return end(); }

    bool contains(std::span<const std::byte> row) const { return m_handle->contains(row); }
    bool contains(database::Row<Values> row) const { return m_handle->contains(row); }
    template<database::ColumnValueFor<Values>... Ts>
    bool contains(const std::tuple<Ts...>& values) const
    {
        return m_handle->contains(values);
    }
};

template<database::ColumnTypes Values, typename C>
class View<Data<database::Relation<Values>>, C>
{
    const C* m_context;
    const Data<database::Relation<Values>>* m_handle;

    const auto& repository() const noexcept { return get_relation_repository(*m_context); }

public:
    View(const Data<database::Relation<Values>>& handle, const C& context) noexcept : m_context(&context), m_handle(&handle) {}
    const auto& get_handle() const noexcept { return *m_handle; }
    const auto& get_data() const noexcept { return *m_handle; }
    const auto& get_context() const noexcept { return *m_context; }
    auto get_index() const noexcept { return get_data().index; }
    auto columns() const noexcept { return repository().get_columns(get_data().columns_index); }
    size_t arity() const noexcept { return columns().size(); }
    size_t size() const noexcept { return row_indices().size(); }
    bool empty() const noexcept { return row_indices().empty(); }
    std::span<const Index<database::RelationRow<Values>>> row_indices() const noexcept
    {
        return repository().get_row_set_repository()[get_data().row_set_index.get_value()];
    }
    const void* get_storage_address() const noexcept { return row_indices().data(); }
    size_t get_storage_index() const noexcept { return repository().get_storage_index(get_data().row_set_index); }
    size_t column_index(Index<database::Column> column) const { return columns().column_index(column); }

    std::span<const std::byte> row(size_t index) const noexcept
    {
        assert(index < size());
        return repository().get_row_repository()[row_indices()[index].get_value()];
    }

    auto operator[](size_t index) const noexcept { return make_view(database::Row<Values>(row(index), columns().span()), get_repository(*m_context)); }

    auto at(size_t index) const
    {
        if (index >= size())
            throw std::out_of_range("Relation: row index out of range.");
        return (*this)[index];
    }

    auto begin() const noexcept { return database::detail::RelationIterator<Values, View>(*this, 0); }
    auto end() const noexcept { return database::detail::RelationIterator<Values, View>(*this, size()); }
    auto cbegin() const noexcept { return begin(); }
    auto cend() const noexcept { return end(); }

    bool contains(std::span<const std::byte> row) const
    {
        database::validate_row<Values>(row, columns().span());
        const auto index = repository().get_row_repository().find(row);
        return index && std::ranges::binary_search(row_indices(), Index<database::RelationRow<Values>>(*index));
    }

    bool contains(database::Row<Values> row) const
    {
        if (!std::ranges::equal(row.columns(), columns().span()))
            throw std::invalid_argument("Relation: row schema does not match.");
        const auto index = repository().get_row_repository().find(row.bytes());
        return index && std::ranges::binary_search(row_indices(), Index<database::RelationRow<Values>>(*index));
    }

    template<database::ColumnValueFor<Values>... Ts>
    bool contains(const std::tuple<Ts...>& values) const
    {
        const auto bytes = database::encode_row<Values>(values, columns().span());
        const auto index = repository().get_row_repository().find(bytes);
        return index && std::ranges::binary_search(row_indices(), Index<database::RelationRow<Values>>(*index));
    }
};

template<database::ColumnTypes Values, typename C>
class View<Index<database::Relation<Values>>, C>
{
    const C* m_context;
    Index<database::Relation<Values>> m_handle;

    const auto& repository() const noexcept { return get_relation_repository(*m_context); }

public:
    View(Index<database::Relation<Values>> handle, const C& context) noexcept : m_context(&context), m_handle(handle) {}
    const auto& get_handle() const noexcept { return m_handle; }
    const auto& get_data() const noexcept { return repository()[m_handle]; }
    const auto& get_context() const noexcept { return *m_context; }
    auto get_index() const noexcept { return m_handle; }
    auto columns() const noexcept { return repository().get_columns(get_data().columns_index); }
    size_t arity() const noexcept { return columns().size(); }
    size_t size() const noexcept { return row_indices().size(); }
    bool empty() const noexcept { return row_indices().empty(); }
    std::span<const Index<database::RelationRow<Values>>> row_indices() const noexcept
    {
        return repository().get_row_set_repository()[get_data().row_set_index.get_value()];
    }
    const void* get_storage_address() const noexcept { return row_indices().data(); }
    size_t get_storage_index() const noexcept { return repository().get_storage_index(get_data().row_set_index); }
    size_t column_index(Index<database::Column> column) const { return columns().column_index(column); }

    std::span<const std::byte> row(size_t index) const noexcept
    {
        assert(index < size());
        return repository().get_row_repository()[row_indices()[index].get_value()];
    }

    auto operator[](size_t index) const noexcept { return make_view(database::Row<Values>(row(index), columns().span()), get_repository(*m_context)); }

    auto at(size_t index) const
    {
        if (index >= size())
            throw std::out_of_range("Relation: row index out of range.");
        return (*this)[index];
    }

    auto begin() const noexcept { return database::detail::RelationIterator<Values, View>(*this, 0); }
    auto end() const noexcept { return database::detail::RelationIterator<Values, View>(*this, size()); }
    auto cbegin() const noexcept { return begin(); }
    auto cend() const noexcept { return end(); }

    bool contains(std::span<const std::byte> row) const
    {
        database::validate_row<Values>(row, columns().span());
        const auto index = repository().get_row_repository().find(row);
        return index && std::ranges::binary_search(row_indices(), Index<database::RelationRow<Values>>(*index));
    }

    bool contains(database::Row<Values> row) const
    {
        if (!std::ranges::equal(row.columns(), columns().span()))
            throw std::invalid_argument("Relation: row schema does not match.");
        const auto index = repository().get_row_repository().find(row.bytes());
        return index && std::ranges::binary_search(row_indices(), Index<database::RelationRow<Values>>(*index));
    }

    template<database::ColumnValueFor<Values>... Ts>
    bool contains(const std::tuple<Ts...>& values) const
    {
        const auto bytes = database::encode_row<Values>(values, columns().span());
        const auto index = repository().get_row_repository().find(bytes);
        return index && std::ranges::binary_search(row_indices(), Index<database::RelationRow<Values>>(*index));
    }

    auto identifying_members() const noexcept { return std::make_tuple(m_handle, repository().get_index()); }
};
}  // namespace ygg

namespace std::ranges
{
template<ygg::database::ColumnTypes Values, typename C>
inline constexpr bool enable_borrowed_range<ygg::View<ygg::Builder<ygg::database::Relation<Values>>, C>> = true;
template<ygg::database::ColumnTypes Values, typename C>
inline constexpr bool enable_borrowed_range<ygg::View<ygg::Data<ygg::database::Relation<Values>>, C>> = true;
template<ygg::database::ColumnTypes Values, typename C>
inline constexpr bool enable_borrowed_range<ygg::View<ygg::Index<ygg::database::Relation<Values>>, C>> = true;
}

#endif
