/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_FORMALISM_RELATION_BUILDER_HPP_
#define YGG_FORMALISM_RELATION_BUILDER_HPP_

#include "yggdrasil/core/config.hpp"
#include "yggdrasil/formalism/declarations.hpp"
#include "yggdrasil/formalism/relation_data.hpp"
#include "yggdrasil/formalism/row_view.hpp"
#include "yggdrasil/formalism/values.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <urd/elements/flat.hpp>
#include <urd/elements/view.hpp>
#include <urd/repr/containers/schema_rows.hpp>
#include <urd/repr/pools/pools.hpp>
#include <urd/repr/sequences/sequence.hpp>
#include <urd/repr/wrappers/multi_index_pool.hpp>
#include <urd/store.hpp>
#include <variant>

namespace ygg
{
/// Rows of one set of column widths, each stored once (urd::by_column_widths): a handle's value is a row's ID. Built
/// with the widths of the columns; rows go in as a stored row of equal widths or as their values. Indexes on sets of
/// columns can be added (urd::multi_indexed); they are kept as rows come and go.
using RowPool = urd::Store<urd::pool<urd::Store<urd::sequence<Value>, urd::frozen>>, urd::by_column_widths, urd::multi_indexed>;

/// Whether a column before `column` has its label.
[[nodiscard]] inline bool repeats_label(std::span<const ColumnLayout> columns, std::span<const ColumnLayout>::iterator column)
{ return std::ranges::find(columns.begin(), column, column->label, &ColumnLayout::label) != column; }

/// Throws std::invalid_argument where a column's type is invalid or two columns share a label.
inline void validate_columns(std::span<const ColumnLayout> columns)
{
    for (auto it = columns.begin(); it != columns.end(); ++it)
    {
        if (static_cast<std::size_t>(it->type) >= std::variant_size_v<ColumnValue>)
            throw std::invalid_argument("Columns: invalid column type.");
        if (repeats_label(columns, it))
            throw std::invalid_argument("Columns: duplicate column label.");
    }
}
}  // namespace ygg

namespace urd
{
/// A set of rows of family F, stored by the widths of its columns (urd::by_column_widths); its columns give each
/// column's label and type, validated (valid types, distinct labels). It is read through a layer (relation_view.hpp:
/// urd::resolve(builder, layer) or layer.view(builder)), which interns it (insert, into the pool of a key given there)
/// and hands out pooled ones (get_relation_builder). It is the context of its rows' views (view(key), with no relation
/// in the key): context[key] is the stored row; it has its own number (layer()), drawn at construction from the counter
/// layers draw theirs from. Erase moves the final row into its place; clear retains row storage.
/// A zero-column relation is true exactly when it contains the empty row.
template<typename F>
struct builder<ygg::Relation<F>>
{
private:
    using Rows = ygg::RowPool;

    urd::data<ygg::Columns> m_columns;
    /// The rows, interned by the columns' widths: a handle's value is the row's position.
    Rows m_rows;
    std::size_t m_layer = ygg::next_layer();

    /// The widths of a builder's rows: each column whole (a bool one bit, an object index 32 bits).
    static urd::column_widths widths_of(std::span<const ygg::ColumnLayout> columns)
    {
        auto result = urd::column_widths();
        for (const auto& column : columns)
            result.push_back(ygg::column_bits(column.type, ygg::whole_bits<urd::index<ygg::formalism::Object<void>>>));
        return result;
    }
    static std::span<const ygg::ColumnLayout> validated(std::span<const ygg::ColumnLayout> columns)
    {
        ygg::validate_columns(columns);
        return columns;
    }

public:
    /// The nullary relation, false.
    builder() : builder(std::span<const ygg::ColumnLayout>()) {}
    explicit builder(std::span<const ygg::ColumnLayout> columns) : builder(widths_of(validated(columns)), columns) {}
    /// A builder with these columns, whose rows are stored in `widths` (the columns' widths in the layer: a slot's key,
    /// see Repository::get_relation_builder).
    builder(const urd::column_widths& widths, std::span<const ygg::ColumnLayout> columns) : m_columns(validated(columns)), m_rows(widths) {}
    builder(const builder&) = delete;
    builder& operator=(const builder&) = delete;
    builder(builder&&) noexcept = default;
    builder& operator=(builder&&) noexcept = default;
    ~builder() = default;

    [[nodiscard]] std::span<const ygg::ColumnLayout> columns() const noexcept { return { m_columns.values.data(), m_columns.values.size() }; }
    /// The widths its rows are stored in.
    [[nodiscard]] const urd::column_widths& widths() const noexcept { return m_rows.column_widths(); }
    // NOLINTNEXTLINE(bugprone-exception-escape): reads the pool's count; the empty storage an unwritten pool reads has no columns, so nothing is allocated
    [[nodiscard]] size_t size() const noexcept { return m_rows.size(); }
    // NOLINTNEXTLINE(bugprone-exception-escape): as size()
    [[nodiscard]] bool empty() const noexcept { return m_rows.empty(); }
    [[nodiscard]] size_t memory_usage() const noexcept { return urd::memory_usage(*this); }
    /// The members it owns memory through (urd::memory_of).
    static constexpr auto memory_members() noexcept { return urd::Members<&builder::m_columns, &builder::m_rows> {}; }
    /// Its rows, by position (a handle's value).
    [[nodiscard]] const Rows& storage() const noexcept { return m_rows; }
    /// The stored row at key.row (a position), as the views of its rows read it.
    // NOLINTNEXTLINE(bugprone-exception-escape): reads a stored row in place (key.row below size()); nothing is allocated or checked by throwing
    [[nodiscard]] Rows::row_type operator[](ygg::RowIndex<F> key) const noexcept { return m_rows.at(typename Rows::handle_type(key.row.value)); }
    /// The view of the row at key.row (a position) through this builder.
    [[nodiscard]] ygg::RowView<F, builder> view(ygg::RowIndex<F> key) const noexcept { return { key, this }; }
    /// Its number as the context of its rows' views (urd::layer), and its relation_identity: drawn at construction,
    /// kept across clear and initialize.
    [[nodiscard]] std::size_t layer() const noexcept { return m_layer; }

    /// A row of values, one per column (the pool's: a count other than the columns' throws std::invalid_argument, a
    /// value that does not fit its column std::out_of_range, before anything is stored). Returns its position.
    ygg::uint_t insert(std::span<const ygg::Value> values) { return m_rows.insert(values).first.value; }
    /// A stored row of equal widths.
    ygg::uint_t insert(const Rows::row_type& row) { return m_rows.insert(row).first.value; }
    /// The position of a row; values that do not fit their columns, or not one per column, are in no row.
    [[nodiscard]] std::optional<ygg::uint_t> find(std::span<const ygg::Value> values) const { return position(m_rows.find(values)); }
    [[nodiscard]] std::optional<ygg::uint_t> find(const Rows::row_type& row) const { return position(m_rows.find(row)); }
    [[nodiscard]] bool contains(std::span<const ygg::Value> values) const { return !m_rows.find(values).is_none(); }
    [[nodiscard]] bool contains(const Rows::row_type& row) const { return !m_rows.find(row).is_none(); }
    /// A row view (its values; a stored row of equal widths).
    template<ygg::RowViewConcept<F> R>
    ygg::uint_t insert(const R& row)
    { return insert(row.get_values()); }
    template<ygg::RowViewConcept<F> R>
    [[nodiscard]] std::optional<ygg::uint_t> find(const R& row) const
    { return find(row.get_values()); }
    template<ygg::RowViewConcept<F> R>
    [[nodiscard]] bool contains(const R& row) const
    { return contains(row.get_values()); }

    /// The number of the index on `columns` (positions, in key order), added where it is new; kept as rows are
    /// inserted and erased, and across clear; initialize drops them.
    std::size_t add_index(std::span<const std::size_t> columns) { return m_rows.add_index(columns); }
    /// The index of number `n`: count, contains, find and equal_range by a key (the columns' values, in its order) give
    /// the positions of the rows with that key.
    [[nodiscard]] auto index(std::size_t n) const { return m_rows.index(n); }

    /// Invalidates the erased and moved rows' positions and borrowed rows.
    void erase(size_t index)
    {
        if (index >= size())
            throw std::out_of_range("Relation: row index out of range.");
        m_rows.erase(typename Rows::handle_type(static_cast<ygg::uint_t>(index)));
    }
    // NOLINTNEXTLINE(bugprone-exception-escape): clearing allocates nothing: an empty pool is left as it is, one with rows has the storage it clears
    void clear() noexcept { m_rows.clear(); }
    /// Relabeled columns of the same types (validated).
    void rename(std::span<const ygg::ColumnLayout> columns)
    {
        if (!std::ranges::equal(this->columns(), columns, {}, &ygg::ColumnLayout::type, &ygg::ColumnLayout::type))
            throw std::invalid_argument("Columns: relabeling must keep the arity and column types.");
        ygg::validate_columns(columns);
        std::ranges::copy(columns, m_columns.values.begin());  // the same widths: the rows stay
    }
    /// Clears the rows and indexes and replaces the columns (validated); equal widths retain row storage where it had no
    /// index.
    void initialize(std::span<const ygg::ColumnLayout> columns)
    {
        ygg::validate_columns(columns);
        // A builder handed out again (Repository::get_relation_builder) keeps no index of its last user.
        if (auto widths = widths_of(columns); widths == m_rows.column_widths() && m_rows.index_count() == 0)
            m_rows.clear();
        else
            m_rows = Rows(widths);
        // Copy before shrinking: columns may be a slice of these.
        auto& values = m_columns.values;
        if (columns.size() <= values.size())
        {
            if (columns.data() != values.data())
                std::copy(columns.begin(), columns.end(), values.begin());
            values.resize(columns.size());
        }
        else
            values.set(columns.begin(), columns.end());
    }

private:
    static std::optional<ygg::uint_t> position(typename Rows::handle_type handle) noexcept
    { return handle.is_none() ? std::nullopt : std::optional<ygg::uint_t>(handle.value); }
};

}  // namespace urd

#endif
