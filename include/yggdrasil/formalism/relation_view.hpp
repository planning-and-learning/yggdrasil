/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_FORMALISM_RELATION_VIEW_HPP_
#define YGG_FORMALISM_RELATION_VIEW_HPP_

#include "yggdrasil/formalism/columns_view.hpp"
#include "yggdrasil/formalism/relation_data.hpp"
#include "yggdrasil/formalism/relation_view_concepts.hpp"
#include "yggdrasil/formalism/row_view.hpp"
#include "yggdrasil/formalism/schema_view.hpp"
#include "yggdrasil/formalism/row_view_concepts.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
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

/// A relation's reads, for an interned one (index_view: its rows' keys in the pool its record names) and a builder
/// (borrowed_view of urd::builder<Relation<F>, C>: its children read as the views of the forms they hold) alike: its rows
/// in tuple order (by their values, lexicographically: ygg::canonicalize), so that membership and a row's place are a binary search by values in both.
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

    /// Its rows in tuple order: a borrowed range of their views.
    [[nodiscard]] auto get_rows() const { return self().template member<&urd::record_of<V>::rows>(); }

    /// Its columns, labeled and typed.
    [[nodiscard]] std::span<const ygg::ColumnLayout> columns() const
    {
        return urd::visit_view(get_columns(),
                               [](const auto& columns)
                               {
                                   const auto& values = columns.get_values();
                                   return std::span<const ygg::ColumnLayout>(values.data(), values.size());
                               });
    }

    /// The number of its columns.
    [[nodiscard]] std::size_t arity() const { return self().columns().size(); }

    /// The number of its rows.
    [[nodiscard]] std::size_t size() const { return std::ranges::size(self().get_rows()); }

    [[nodiscard]] bool empty() const { return size() == 0; }

    /// The row at a position; std::out_of_range past the last.
    [[nodiscard]] auto row(std::size_t index) const
    {
        if (index >= size())
            throw std::out_of_range("Relation: row index out of range.");
        return self().get_rows()[index];
    }

    /// Where these values are, or would go, in tuple order (the first row not before them), and whether it holds them
    /// there: one binary search.
    template<std::ranges::input_range Values>
        requires std::same_as<std::ranges::range_value_t<Values>, ygg::Value>
    [[nodiscard]] std::pair<std::size_t, bool> locate(const Values& values) const
    {
        const auto rows = self().get_rows();
        const auto before = [&values](const auto& row) { return std::ranges::lexicographical_compare(row.get_values(), values); };
        const auto after = [&values](const auto& row) { return std::ranges::lexicographical_compare(values, row.get_values()); };
        const auto at = *std::ranges::partition_point(std::views::iota(std::size_t { 0 }, size()),
                                                      [&](std::size_t index) { return urd::visit_view(rows[index], before); });
        return { at, at < size() && !urd::visit_view(rows[at], after) };
    }

    /// The position of the first row not before these values in tuple order: where they are, or would go.
    template<std::ranges::input_range Values>
        requires std::same_as<std::ranges::range_value_t<Values>, ygg::Value>
    [[nodiscard]] std::size_t lower_bound(const Values& values) const
    { return locate(values).first; }

    /// The position of the row of these values, if it holds one.
    template<std::ranges::input_range Values>
        requires std::same_as<std::ranges::range_value_t<Values>, ygg::Value>
    [[nodiscard]] std::optional<std::size_t> find(const Values& values) const
    {
        const auto [at, held] = locate(values);
        return held ? std::optional<std::size_t>(at) : std::nullopt;
    }

    /// Whether it holds a row of these values (values that do not fit its columns, or not one per column, are in none).
    template<std::ranges::input_range Values>
        requires std::same_as<std::ranges::range_value_t<Values>, ygg::Value>
    [[nodiscard]] bool contains(const Values& values) const
    { return find(values).has_value(); }

    /// Whether it holds a row with the values of `row`, of any relation of F.
    template<ygg::RowViewConcept<F> R>
    [[nodiscard]] bool contains(const R& row) const
    { return self().contains(row.get_values()); }

    /// Its reads; find and contains by their instantiations Python reaches: a sequence of Values, and a stored row of its
    /// context's.
    static constexpr auto functions() noexcept
    {
        using Context = std::remove_cvref_t<decltype(std::declval<const V&>().context())>;
        return urd::Functions<urd::function<"columns", &view_mixin::columns>,
                              urd::function<"arity", &view_mixin::arity>,
                              urd::function<"size", &view_mixin::size>,
                              urd::function<"empty", &view_mixin::empty>,
                              urd::function<"row", &view_mixin::row, urd::arg<"index">>,
                              urd::function<"find", &view_mixin::template find<std::span<const ygg::Value>>, urd::arg<"values">>,
                              urd::function<"contains", &view_mixin::template contains<std::span<const ygg::Value>>, urd::arg<"values">>,
                              urd::function<"contains", &view_mixin::template contains<ygg::RowView<F, Context>>, urd::arg<"row">>> {};
    }
};

#endif
