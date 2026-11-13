/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_FORMALISM_RELATION_VIEW_HPP_
#define YGG_FORMALISM_RELATION_VIEW_HPP_

#include "yggdrasil/formalism/relation_builder.hpp"

#include <cassert>
#include <cstdint>
#include <ranges>
#include <span>
#include <urd/elements/view.hpp>
#include <utility>

/**
 * The views of relations: of an interned relation (index_view, through the layer that owns it) and of a relation
 * builder (urd::resolve(builder, layer) or layer.view(builder)). Both give their columns, their rows (Row<F> views:
 * values by position, ygg::Value; their types are the columns'), membership and their identity.
 */

/// Read access to an interned relation record, shared by its index and data views: its rows lie in the layer's pool
/// its record names, its row ids in that pool; each row is read as the layer's view of its key.
template<typename F, typename V>
struct urd::view_mixin<ygg::Relation<F>, V>
{
private:
    [[nodiscard]] const V& self() const noexcept { return static_cast<const V&>(*this); }
    [[nodiscard]] const urd::data<ygg::Relation<F>>& record() const noexcept { return self().data(); }
    [[nodiscard]] const auto& repository() const noexcept { return get_repository(self().context()); }
    [[nodiscard]] std::span<const urd::index<ygg::Row<F>>> ids() const
    {
        const auto& rows = record().rows;
        return { rows.data(), rows.size() };
    }

public:
    /// Its columns, interned from its labels and its pool's types.
    [[nodiscard]] std::span<const ygg::ColumnLayout> columns() const noexcept
    {
        const auto& values = repository()[record().columns].values;
        return { values.data(), values.size() };
    }
    [[nodiscard]] ygg::RelationIdentity relation_identity() const noexcept { return { self().context().layer(), record().index.value }; }
    [[nodiscard]] size_t size() const { return ids().size(); }
    [[nodiscard]] bool empty() const { return ids().empty(); }
    [[nodiscard]] auto row(size_t index) const { return repository().view(ygg::RowIndex<F> { record().pool, ids()[index] }); }
    /// The rows by position: a borrowed range over the layer's storage.
    [[nodiscard]] auto rows() const { return ygg::rows_view(record().pool, ids(), repository()); }
    /// Values that do not fit their columns, or not one per column, are in no row: a find in the pool and a binary
    /// search in its row ids.
    template<std::ranges::input_range Values>
        requires std::same_as<std::ranges::range_value_t<Values>, ygg::Value>
    [[nodiscard]] bool contains(const Values& values) const
    {
        const auto id = repository().relations().find(record().pool, values);
        return !id.is_none() && std::ranges::binary_search(ids(), id);
    }
    template<ygg::RowViewConcept<F> R>
    [[nodiscard]] bool contains(const R& row) const
    { return contains(row.get_values()); }
};

/// A builder's view reads the builder's rows, by position, each through the builder (its rows' context); its context
/// is the layer it is read through.
template<typename F, typename V>
struct urd::view_mixin<urd::builder<ygg::Relation<F>>, V>
{
private:
    [[nodiscard]] const V& self() const noexcept { return static_cast<const V&>(*this); }

public:
    [[nodiscard]] std::span<const ygg::ColumnLayout> columns() const noexcept { return self().data().columns(); }
    [[nodiscard]] ygg::RelationIdentity relation_identity() const noexcept { return self().data().relation_identity(); }
    [[nodiscard]] size_t size() const noexcept { return self().data().size(); }
    [[nodiscard]] bool empty() const noexcept { return self().data().empty(); }
    [[nodiscard]] auto row(size_t index) const noexcept
    {
        assert(index < size());
        return self().data().view(ygg::RowIndex<F> { {}, urd::index<ygg::Row<F>>(static_cast<ygg::uint_t>(index)) });
    }
    /// The rows by position: a borrowed range over the builder's storage.
    [[nodiscard]] auto rows() const noexcept
    {
        return urd::transform(std::views::iota(size_t { 0 }, size()),
                              &self().data(),
                              [](const auto* builder, size_t index)
                              { return builder->view(ygg::RowIndex<F> { {}, urd::index<ygg::Row<F>>(static_cast<ygg::uint_t>(index)) }); });
    }

    [[nodiscard]] bool contains(std::span<const ygg::Value> values) const { return self().data().contains(values); }
    template<ygg::RowViewConcept<F> R>
    [[nodiscard]] bool contains(const R& row) const
    { return self().data().contains(row.get_values()); }
};

#endif
