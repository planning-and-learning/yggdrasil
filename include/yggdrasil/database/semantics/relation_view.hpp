/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_DATABASE_SEMANTICS_RELATION_VIEW_HPP_
#define YGG_DATABASE_SEMANTICS_RELATION_VIEW_HPP_

#include <urd/elements/view.hpp>
#include "yggdrasil/database/semantics/relation_builder.hpp"
#include "yggdrasil/database/semantics/relation_data.hpp"

#include <algorithm>
#include <cassert>
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
concept RelationViewConcept = requires(const std::remove_reference_t<V>& view, size_t i, urd::index<Column> column, std::span<const std::byte> row, Row typed_row) {
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
/// The row of a relation view at a position. It holds a copy of the lightweight view, so that iterators outlive the
/// range expression; in a std::optional, which makes it semiregular (a view has no default).
template<typename V>
struct RowAt
{
    std::optional<V> view;

    auto operator()(ygg::uint_t position) const { return (*view)[position]; }
};

/// The rows of a relation view by position: a borrowed random-access range whose iterators hold a copy of the
/// lightweight view, independently of the range expression.
/// ponytail: positions are ygg::uint_t (so that the difference type is std::ptrdiff_t), as interned rows are numbered.
template<typename V>
auto rows_of(const V& view)
{ return urd::transform(std::views::iota(ygg::uint_t { 0 }, static_cast<ygg::uint_t>(view.size())), RowAt<V> { view }); }

/// Rows by position, shared by the relation view mixins: V gives row(index), columns() and size().
template<typename V>
struct RelationRows
{
    /// A row: its bytes and schema; index values stay raw, for the caller to resolve in their owner.
    auto operator[](size_t index) const noexcept { return Row(self().row(index), self().columns().span()); }
    [[nodiscard]] auto at(size_t index) const
    {
        if (index >= self().size())
            throw std::out_of_range("Relation: row index out of range.");
        return (*this)[index];
    }
    [[nodiscard]] auto begin() const noexcept { return rows_of(self()).begin(); }
    [[nodiscard]] auto end() const noexcept { return rows_of(self()).end(); }
    [[nodiscard]] auto cbegin() const noexcept { return begin(); }
    [[nodiscard]] auto cend() const noexcept { return end(); }

private:
    [[nodiscard]] const V& self() const noexcept { return static_cast<const V&>(*this); }
};
}  // namespace ygg::database::detail

/// The builder view reads the builder directly.
template<typename V>
struct urd::view_mixin<urd::builder<ygg::database::Relation>, V> : ygg::database::detail::RelationRows<V>
{
private:
    [[nodiscard]] const V& self() const noexcept { return static_cast<const V&>(*this); }

public:
    [[nodiscard]] auto index() const noexcept { return self().data().index(); }
    [[nodiscard]] auto columns() const noexcept { return urd::resolve(self().data().columns(), self().context()); }
    [[nodiscard]] size_t arity() const noexcept { return self().data().arity(); }
    [[nodiscard]] size_t size() const noexcept { return self().data().size(); }
    [[nodiscard]] bool empty() const noexcept { return self().data().empty(); }
    [[nodiscard]] const void* get_storage_address() const noexcept { return self().data().get_storage_address(); }
    [[nodiscard]] size_t get_storage_index() const noexcept { return self().data().get_storage_index(); }
    [[nodiscard]] size_t column_index(urd::index<ygg::database::Column> column) const { return self().data().column_index(column); }
    [[nodiscard]] std::span<const std::byte> row(size_t index) const noexcept { return self().data().row(index); }

    [[nodiscard]] bool contains(std::span<const std::byte> row) const { return self().data().contains(row); }
    [[nodiscard]] bool contains(ygg::database::Row row) const { return self().data().contains(row); }
    template<ygg::ByteCodable... Ts>
    [[nodiscard]] bool contains(const std::tuple<Ts...>& values) const
    { return self().data().contains(values); }
};

/// Read access to an interned relation record, shared by its index and data views.
template<typename V>
struct urd::view_mixin<ygg::database::Relation, V> : ygg::database::detail::RelationRows<V>
{
private:
    [[nodiscard]] const V& self() const noexcept { return static_cast<const V&>(*this); }
    [[nodiscard]] const urd::data<ygg::database::Relation>& record() const noexcept { return self().data(); }
    [[nodiscard]] bool contains_canonical(std::span<const std::byte> row) const
    {
        const auto index = get_repository(self().context()).get_row_repository().find(row);
        return !index.is_none() && std::ranges::binary_search(row_indices(), urd::index<ygg::database::RelationRow>(index.value));
    }

public:
    [[nodiscard]] auto columns() const noexcept { return get_repository(self().context()).view(record().columns_index); }
    [[nodiscard]] size_t arity() const noexcept { return columns().size(); }
    [[nodiscard]] size_t size() const noexcept { return row_indices().size(); }
    [[nodiscard]] bool empty() const noexcept { return row_indices().empty(); }
    [[nodiscard]] std::span<const urd::index<ygg::database::RelationRow>> row_indices() const noexcept
    {
        using Handle = typename std::remove_cvref_t<decltype(get_repository(self().context()))>::RowSetPool::handle_type;
        return get_repository(self().context()).get_row_set_repository().view(Handle(record().row_set_index.value));
    }
    [[nodiscard]] const void* get_storage_address() const noexcept { return row_indices().data(); }
    [[nodiscard]] size_t get_storage_index() const noexcept { return get_repository(self().context()).get_storage_index(record().row_set_index); }
    [[nodiscard]] size_t column_index(urd::index<ygg::database::Column> column) const { return columns().column_index(column); }

    [[nodiscard]] std::span<const std::byte> row(size_t index) const noexcept
    {
        assert(index < size());
        using Handle = typename std::remove_cvref_t<decltype(get_repository(self().context()))>::RowPool::handle_type;
        return get_repository(self().context()).get_row_repository().view(Handle(row_indices()[index].value));
    }

    [[nodiscard]] bool contains(std::span<const std::byte> row) const
    {
        if (row.size() != columns().row_size())
            throw std::invalid_argument("Relation: row byte width does not match its schema.");
        return contains_canonical(row);
    }

    [[nodiscard]] bool contains(ygg::database::Row row) const
    {
        if (!std::ranges::equal(row.columns(), columns().span()))
            throw std::invalid_argument("Relation: row schema does not match.");
        return contains_canonical(row.bytes());
    }

    template<ygg::ByteCodable... Ts>
    [[nodiscard]] bool contains(const std::tuple<Ts...>& values) const
    { return contains_canonical(ygg::database::encode_row(values, columns().span())); }
};


namespace std::ranges
{
template<typename C>
inline constexpr bool enable_borrowed_range<urd::borrowed_view<ygg::database::Relation, C, urd::builder<ygg::database::Relation>>> = true;
template<typename C>
inline constexpr bool enable_borrowed_range<urd::borrowed_view<ygg::database::Relation, C>> = true;
template<typename C>
inline constexpr bool enable_borrowed_range<urd::index_view<ygg::database::Relation, C>> = true;
}

#endif
