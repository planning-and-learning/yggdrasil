/*
 * Copyright (C) 2025-2026 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef YGG_FORMALISM_BINDING_VIEW_HPP_
#define YGG_FORMALISM_BINDING_VIEW_HPP_

#include <urd/elements/flat.hpp>
#include "yggdrasil/formalism/binding_data.hpp"
#include "yggdrasil/formalism/declarations.hpp"

#include <iterator>
#include <ranges>
#include <span>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

/// A binding, published (its handle read through the context: a RelationBindingRow) or borrowed (its record): its
/// relation and objects, both read through the context. A borrowed binding's record and context must outlive the view
/// and all object ranges; keep the record unchanged while used.
template<typename RelationTag, typename ObjectTag, typename V>
struct urd::view_mixin<ygg::formalism::RelationBinding<RelationTag, ObjectTag>, V>
{
private:
    [[nodiscard]] const V& self() const noexcept { return static_cast<const V&>(*this); }

public:
    [[nodiscard]] auto get_relation() const { return self().context().view(self().data().relation); }
    [[nodiscard]] auto get_objects() const { return urd::resolve(self().data().objects, self().context()); }
    /// The logical binding across repositories: the relation index (a view's identity includes its repository) and
    /// the object row.
    [[nodiscard]] auto get_key() const { return std::make_pair(self().data().relation, std::views::all(self().data().objects)); }
};

namespace ygg::formalism
{
/// Shared read interface for published and borrowed bindings. Publication identity
/// is deliberately absent; a logical key consists of the relation and object row.
template<typename V, typename RelationTag, typename ObjectTag>
concept RelationBindingViewConcept = requires(const std::remove_reference_t<V>& view) {
    view.data();
    view.context();
    { view.get_relation().index() } -> std::same_as<urd::index<RelationTag>>;
    { view.get_objects()[size_t {}].index() } -> std::same_as<urd::index<Object<ObjectTag>>>;
    requires std::same_as<std::remove_cvref_t<decltype(view.get_key().first)>, urd::index<RelationTag>>;
    requires std::ranges::forward_range<const std::remove_reference_t<decltype(view.get_key().second)>>
                 && std::ranges::sized_range<const std::remove_reference_t<decltype(view.get_key().second)>>
                 && std::same_as<std::ranges::range_value_t<const std::remove_reference_t<decltype(view.get_key().second)>>, urd::index<Object<ObjectTag>>>
                 && std::convertible_to<std::ranges::range_reference_t<const std::remove_reference_t<decltype(view.get_key().second)>>,
                                        const urd::index<Object<ObjectTag>>&>;
};
}  // namespace ygg::formalism

#endif
