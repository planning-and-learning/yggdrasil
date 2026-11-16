/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_FORMALISM_RELATION_VIEW_HPP_
#define YGG_FORMALISM_RELATION_VIEW_HPP_

#include "yggdrasil/formalism/relation_builder.hpp"
#include "yggdrasil/formalism/row_view.hpp"
#include "yggdrasil/formalism/view_reads.hpp"

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
 * values by position, ygg::Value; their types are the columns') and membership.
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
struct urd::view_mixin<ygg::Relation<F>, V> : ygg::detail::relation_reads<F, V>
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
    [[nodiscard]] std::size_t size() const { return std::ranges::size(get_rows()); }
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
    using ygg::detail::relation_reads<F, V>::contains;
};

namespace ygg
{
/// A relation builder read through a layer C (layer.view(builder), urd::resolve(builder, layer)): its columns, its rows
/// by position, each through the builder (its rows' context), membership and the builder itself. Not a record's view: a
/// builder is no record. The builder and the layer must outlive it; the builder's next mutation invalidates its rows.
template<typename F, typename C>
class RelationBuilderView : public detail::relation_reads<F, RelationBuilderView<F, C>>
{
public:
    RelationBuilderView(const urd::builder<Relation<F>>* builder, const C* context) noexcept : m_builder(builder), m_context(context) {}

    /// The layer it is read through.
    [[nodiscard]] const C& context() const noexcept { return *m_context; }
    [[nodiscard]] std::span<const ColumnLayout> columns() const noexcept { return m_builder->columns(); }
    /// The builder it reads.
    [[nodiscard]] const urd::builder<Relation<F>>& get_builder() const noexcept { return *m_builder; }
    [[nodiscard]] std::size_t size() const noexcept { return m_builder->size(); }
    /// The rows by position: a borrowed range over the builder's storage.
    [[nodiscard]] auto get_rows() const noexcept
    {
        return urd::transform(std::views::iota(std::size_t { 0 }, size()),
                              m_builder,
                              [](const auto* builder, std::size_t index) { return builder->view(position(index)); });
    }
    /// Whether the builder holds these values (a range of Values the builder takes: a span, a packed row).
    template<std::ranges::input_range Values>
        requires std::same_as<std::ranges::range_value_t<Values>, Value>
                 && requires(const urd::builder<Relation<F>>& builder, const Values& values) { builder.contains(values); }
    [[nodiscard]] bool contains(const Values& values) const
    { return m_builder->contains(values); }
    using detail::relation_reads<F, RelationBuilderView<F, C>>::contains;

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
