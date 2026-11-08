/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_FORMALISM_RELATION_VIEW_HPP_
#define YGG_FORMALISM_RELATION_VIEW_HPP_

#include "yggdrasil/formalism/relation_builder.hpp"
#include <urd/elements/subset_view.hpp>
#include <urd/elements/view.hpp>

#include <cassert>
#include <cstdint>
#include <limits>
#include <ranges>
#include <span>
#include <utility>

/**
 * The views of relations: of an interned relation (index_view, through the layer that owns it) and of a relation
 * builder (urd::resolve(builder, layer) or layer.view(builder)). Both give their columns, their rows (stored rows:
 * values by position, ygg::Value; their types are the columns'), membership and their identity.
 */

namespace ygg
{
/// What tells relations apart while their rows stay unchanged: an interned relation's layer number and index, a
/// builder's address (and no index).
using RelationIdentity = std::pair<std::uintptr_t, std::uint32_t>;

template<typename F>
[[nodiscard]] RelationIdentity relation_identity(const urd::builder<Relation<F>>& builder) noexcept
{ return { reinterpret_cast<std::uintptr_t>(&builder), std::numeric_limits<std::uint32_t>::max() }; }
}  // namespace ygg

/// Read access to an interned relation record, shared by its index and data views: its rows lie in the layer's pool
/// its record names, its row set over that pool (urd::subset_view).
template<typename F, typename V>
struct urd::view_mixin<ygg::Relation<F>, V>
{
private:
    [[nodiscard]] const V& self() const noexcept { return static_cast<const V&>(*this); }
    [[nodiscard]] const urd::data<ygg::Relation<F>>& record() const noexcept { return self().data(); }
    [[nodiscard]] const auto& repository() const noexcept { return get_repository(self().context()); }
    [[nodiscard]] auto subset() const
    {
        const auto& rows = repository()[record().row_set].rows;
        return urd::subset_view(repository().relations().template get<F>().at(record().pool),
                                std::span<const urd::index<ygg::formalism::Row>>(rows.data(), rows.size()));
    }

public:
    [[nodiscard]] std::span<const ygg::ColumnLayout> columns() const noexcept
    {
        const auto& values = repository()[record().columns].values;
        return { values.data(), values.size() };
    }
    [[nodiscard]] ygg::RelationIdentity relation_identity() const noexcept { return { self().context().layer(), record().index.value }; }
    [[nodiscard]] size_t size() const { return subset().size(); }
    [[nodiscard]] bool empty() const { return subset().empty(); }
    [[nodiscard]] auto row(size_t index) const { return subset()[index]; }
    /// The rows by position: a borrowed range over the layer's storage.
    [[nodiscard]] auto rows() const { return subset(); }
    /// Values that do not fit their columns, or not one per column, are in no row.
    [[nodiscard]] bool contains(std::span<const ygg::Value> values) const { return subset().contains(values); }
    [[nodiscard]] bool contains(const ygg::database::RowPool::row_type& row) const { return subset().contains(row); }
};

/// A builder's view reads the builder's rows, its pool by position; its context is the layer it is read through.
template<typename F, typename V>
struct urd::view_mixin<urd::builder<ygg::Relation<F>>, V>
{
private:
    [[nodiscard]] const V& self() const noexcept { return static_cast<const V&>(*this); }

public:
    [[nodiscard]] std::span<const ygg::ColumnLayout> columns() const noexcept { return self().data().columns(); }
    [[nodiscard]] ygg::RelationIdentity relation_identity() const noexcept { return ygg::relation_identity(self().data()); }
    [[nodiscard]] size_t size() const noexcept { return self().data().size(); }
    [[nodiscard]] bool empty() const noexcept { return self().data().empty(); }
    [[nodiscard]] auto row(size_t index) const noexcept
    {
        assert(index < size());
        return self().data().storage().view(ygg::database::RowPool::handle_type(static_cast<ygg::uint_t>(index)));
    }
    /// The rows by position: a borrowed range over the builder's storage.
    [[nodiscard]] auto rows() const noexcept
    {
        return urd::transform(std::views::iota(size_t { 0 }, size()),
                              [pool = &self().data().storage()](size_t index)
                              { return pool->view(ygg::database::RowPool::handle_type(static_cast<ygg::uint_t>(index))); });
    }

    [[nodiscard]] bool contains(std::span<const ygg::Value> values) const { return self().data().contains(values); }
    [[nodiscard]] bool contains(const ygg::database::RowPool::row_type& row) const { return self().data().contains(row); }
};

#endif
