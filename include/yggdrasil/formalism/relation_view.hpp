/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_FORMALISM_RELATION_VIEW_HPP_
#define YGG_FORMALISM_RELATION_VIEW_HPP_

#include "yggdrasil/formalism/relation_builder.hpp"
#include "yggdrasil/formalism/row_view.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <stdexcept>
#include <urd/elements/view.hpp>
#include <utility>

/**
 * The views of relations: of an interned relation (index_view, through the layer that owns it) and of a relation
 * builder (urd::resolve(builder, layer) or layer.view(builder)). Both give their columns, their rows (Row<F> views:
 * values by position, ygg::Value; their types are the columns'), membership and their identity.
 */

/// A Schema's view: its column types.
template<typename V>
struct urd::view_mixin<ygg::Schema, V>
{
private:
    [[nodiscard]] const V& self() const noexcept { return static_cast<const V&>(*this); }

public:
    [[nodiscard]] decltype(auto) get_types() const noexcept { return self().template member<&urd::record_of<V>::types>(); }
};

/// A Columns' view: its columns, labeled and typed.
template<typename V>
struct urd::view_mixin<ygg::Columns, V>
{
private:
    [[nodiscard]] const V& self() const noexcept { return static_cast<const V&>(*this); }

public:
    [[nodiscard]] decltype(auto) get_values() const noexcept { return self().template member<&urd::record_of<V>::values>(); }
};

/// An interned relation's view (index_view): its rows lie in the layer's pool its record names, its row indices in that
/// pool; each row is read as the layer's view of its index.
template<typename F, typename V>
struct urd::view_mixin<ygg::Relation<F>, V>
{
private:
    [[nodiscard]] const V& self() const noexcept { return static_cast<const V&>(*this); }

public:
    /// The view of its pool's key.
    [[nodiscard]] auto get_pool() const { return self().template member<&urd::record_of<V>::pool>(); }
    /// The view of its Columns.
    [[nodiscard]] auto get_columns() const { return self().template member<&urd::record_of<V>::columns>(); }
    /// Its rows, sorted by their indices: a borrowed range of their views over the layer's storage.
    [[nodiscard]] auto get_rows() const { return self().template member<&urd::record_of<V>::rows>(); }
    /// Its columns, interned from its labels and its pool's types.
    [[nodiscard]] std::span<const ygg::ColumnLayout> columns() const { return get_columns().get_values(); }
    /// The number of its columns.
    [[nodiscard]] std::size_t arity() const { return columns().size(); }
    [[nodiscard]] ygg::RelationIdentity relation_identity() const noexcept { return { self().context().layer(), self().index().value }; }
    [[nodiscard]] size_t size() const { return std::ranges::size(get_rows()); }
    [[nodiscard]] bool empty() const { return std::ranges::empty(get_rows()); }
    /// The row at a position; std::out_of_range past the last.
    [[nodiscard]] auto row(size_t index) const
    {
        if (index >= size())
            throw std::out_of_range("Relation: row index out of range.");
        return get_rows()[index];
    }
    /// The rows by position: a borrowed range over the layer's storage.
    [[nodiscard]] auto rows() const { return get_rows(); }
    /// Values that do not fit their columns, or not one per column, are in no row: a find in the pool and a binary
    /// search in its row indices.
    template<std::ranges::input_range Values>
        requires std::same_as<std::ranges::range_value_t<Values>, ygg::Value>
    [[nodiscard]] bool contains(const Values& values) const
    {
        const auto pool = get_pool().index();
        const auto id = get_repository(self().context()).find_row(pool, values);
        return !id.is_none() && std::ranges::binary_search(get_rows(), ygg::RowIndex<F> { pool, id }, {}, [](const auto& row) { return row.index(); });
    }
    template<ygg::RowViewConcept<F> R>
    [[nodiscard]] bool contains(const R& row) const
    { return contains(row.get_values()); }
};

namespace ygg
{
/// A relation builder read through a layer C (layer.view(builder), urd::resolve(builder, layer)): its columns, its rows
/// by position, each through the builder (its rows' context), membership and its identity. Not a record's view: a
/// builder is no record. The builder and the layer must outlive it; the builder's next mutation invalidates its rows.
template<typename F, typename C>
class RelationBuilderView
{
public:
    RelationBuilderView(const urd::builder<Relation<F>>* builder, const C* context) noexcept : m_builder(builder), m_context(context) {}

    /// The layer it is read through.
    [[nodiscard]] const C& context() const noexcept { return *m_context; }
    [[nodiscard]] std::span<const ColumnLayout> columns() const noexcept { return m_builder->columns(); }
    [[nodiscard]] std::size_t arity() const noexcept { return m_builder->arity(); }
    [[nodiscard]] RelationIdentity relation_identity() const noexcept { return m_builder->relation_identity(); }
    [[nodiscard]] std::size_t size() const noexcept { return m_builder->size(); }
    [[nodiscard]] bool empty() const noexcept { return m_builder->empty(); }
    /// The row at a position; std::out_of_range past the last.
    [[nodiscard]] auto row(std::size_t index) const
    {
        if (index >= size())
            throw std::out_of_range("Relation: row index out of range.");
        return m_builder->view(position(index));
    }
    /// The rows by position: a borrowed range over the builder's storage.
    [[nodiscard]] auto rows() const noexcept
    {
        return urd::transform(std::views::iota(std::size_t { 0 }, size()),
                              m_builder,
                              [](const auto* builder, std::size_t index) { return builder->view(position(index)); });
    }
    [[nodiscard]] bool contains(std::span<const Value> values) const { return m_builder->contains(values); }
    template<RowViewConcept<F> R>
    [[nodiscard]] bool contains(const R& row) const
    { return m_builder->contains(row.get_values()); }

private:
    /// The index of the row at a position of the builder (no relation: the builder is its rows' context).
    static RowIndex<F> position(std::size_t index) noexcept { return { {}, urd::index<Row<F>>(static_cast<uint_t>(index)) }; }

    const urd::builder<Relation<F>>* m_builder;
    const C* m_context;
};
}  // namespace ygg

/// A relation builder held in place, read through a context: its RelationBuilderView. Never a temporary.
template<typename F>
struct urd::resolution<urd::builder<ygg::Relation<F>>>
{
    template<typename C>
    static ygg::RelationBuilderView<F, C> of(const urd::builder<ygg::Relation<F>>& member, const C& context) noexcept
    { return { &member, &context }; }
    template<typename C>
    static void of(const urd::builder<ygg::Relation<F>>&& member, const C& context) = delete;
};

#endif
