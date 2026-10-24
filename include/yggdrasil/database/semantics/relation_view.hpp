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
#include <span>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

namespace ygg::database
{
/// Raw read-only row access shared by builders and contextual Builder/Data/Index views.
/// Contextual indexing and iteration may resolve stored elements into semantic views.
template<typename V>
concept RelationViewConcept = requires(const std::remove_reference_t<V>& view, size_t i, Index<Column> column, std::span<const std::byte> row, Row typed_row) {
    { view.columns() } -> ColumnsViewConcept;
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
template<typename R>
concept RelationViewRange =
    std::ranges::random_access_range<const R> && std::ranges::sized_range<const R> && RelationViewConcept<std::ranges::range_reference_t<const R>>;

/// A row consumer: called with the bytes of each row; returning false stops the enumeration.
template<typename F>
concept RowPredicate = std::predicate<const F&, std::span<const std::byte>>;

/// The inputs of a query by slot: f(slot) returns the relation view bound to input slot `slot`.
template<typename F>
concept SlotRelations = std::invocable<const F&, size_t> && RelationViewConcept<std::invoke_result_t<const F&, size_t>>;

}  // namespace ygg::database

namespace ygg::database::detail
{
/// Reads row i of a relation view, so the iterator can name the row type it yields.
struct RowAt
{
    template<typename V>
        requires requires(const V& view, size_t index) { view[index]; }
    auto operator()(const V& view, size_t index) const
    { return view[index]; }
};

/// Keeps the lightweight relation view alive, independently of the range expression.
/// Comparing or subtracting iterators requires that they belong to the same sequence.
template<RelationViewConcept V>
    requires std::copyable<V> && requires(const V& view, size_t position) { view[position]; }
class RelationIterator
{
    std::optional<V> m_view;
    std::ptrdiff_t m_position = 0;

public:
    using difference_type = std::ptrdiff_t;
    using value_type = std::invoke_result_t<RowAt, const V&, size_t>;
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
template<typename C>
class View<Builder<database::Relation>, C> : public BuilderViewBase<database::Relation, C>
{
public:
    using BuilderViewBase<database::Relation, C>::BuilderViewBase;

    auto get_index() const noexcept { return this->get_handle().get_index(); }
    auto columns() const noexcept { return make_view(this->get_handle().columns(), this->get_context()); }
    size_t arity() const noexcept { return this->get_handle().arity(); }
    size_t size() const noexcept { return this->get_handle().size(); }
    bool empty() const noexcept { return this->get_handle().empty(); }
    const void* get_storage_address() const noexcept { return this->get_handle().get_storage_address(); }
    size_t get_storage_index() const noexcept { return this->get_handle().get_storage_index(); }
    size_t column_index(Index<database::Column> column) const { return this->get_handle().column_index(column); }
    std::span<const std::byte> row(size_t index) const noexcept { return this->get_handle().row(index); }
    auto operator[](size_t index) const noexcept { return make_view(database::Row(row(index), columns().span()), this->get_repository()); }
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

    bool contains(std::span<const std::byte> row) const { return this->get_handle().contains(row); }
    bool contains(database::Row row) const { return this->get_handle().contains(row); }
    template<ygg::ByteCodable... Ts>
    bool contains(const std::tuple<Ts...>& values) const
    { return this->get_handle().contains(values); }
};
}  // namespace ygg

namespace ygg
{
/// Read access to an interned relation record, shared by its index and data views.
/// The builder view is specialized separately, since it reads the builder directly.
template<typename V>
class ViewMixin<database::Relation, V>
{
    const V& self() const noexcept { return static_cast<const V&>(*this); }
    const Data<database::Relation>& record() const noexcept { return self().get_record(); }
    bool contains_canonical(std::span<const std::byte> row) const
    {
        const auto index = self().get_repository().get_row_repository().find(row);
        return index && std::ranges::binary_search(row_indices(), Index<database::RelationRow>(*index));
    }

public:
    auto columns() const noexcept { return self().get_repository().get_columns(record().columns_index); }
    size_t arity() const noexcept { return columns().size(); }
    size_t size() const noexcept { return row_indices().size(); }
    bool empty() const noexcept { return row_indices().empty(); }
    std::span<const Index<database::RelationRow>> row_indices() const noexcept
    { return self().get_repository().get_row_set_repository()[record().row_set_index.get_value()]; }
    const void* get_storage_address() const noexcept { return row_indices().data(); }
    size_t get_storage_index() const noexcept { return self().get_repository().get_storage_index(record().row_set_index); }
    size_t column_index(Index<database::Column> column) const { return columns().column_index(column); }

    std::span<const std::byte> row(size_t index) const noexcept
    {
        assert(index < size());
        return self().get_repository().get_row_repository()[row_indices()[index].get_value()];
    }

    /// Rows resolve indices the owning repository can view; others stay raw for the caller to resolve.
    auto operator[](size_t index) const noexcept { return make_view(database::Row(row(index), columns().span()), self().get_repository()); }

    auto at(size_t index) const
    {
        if (index >= size())
            throw std::out_of_range("Relation: row index out of range.");
        return (*this)[index];
    }

    auto begin() const noexcept { return database::detail::RelationIterator<V>(self(), 0); }
    auto end() const noexcept { return database::detail::RelationIterator<V>(self(), size()); }
    auto cbegin() const noexcept { return begin(); }
    auto cend() const noexcept { return end(); }

    bool contains(std::span<const std::byte> row) const
    {
        if (row.size() != columns().row_size())
            throw std::invalid_argument("Relation: row byte width does not match its schema.");
        return contains_canonical(row);
    }

    bool contains(database::Row row) const
    {
        if (!std::ranges::equal(row.columns(), columns().span()))
            throw std::invalid_argument("Relation: row schema does not match.");
        return contains_canonical(row.bytes());
    }

    template<ygg::ByteCodable... Ts>
    bool contains(const std::tuple<Ts...>& values) const
    { return contains_canonical(database::encode_row(values, columns().span())); }
};
}  // namespace ygg

namespace std::ranges
{
template<typename C>
inline constexpr bool enable_borrowed_range<ygg::View<ygg::Builder<ygg::database::Relation>, C>> = true;
template<typename C>
inline constexpr bool enable_borrowed_range<ygg::View<ygg::Data<ygg::database::Relation>, C>> = true;
template<typename C>
inline constexpr bool enable_borrowed_range<ygg::View<ygg::Index<ygg::database::Relation>, C>> = true;
}

#endif
