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

#ifndef YGG_CORE_TYPES_HPP_
#define YGG_CORE_TYPES_HPP_

#include "yggdrasil/core/config.hpp"
#include "yggdrasil/core/dependent_false.hpp"
#include "yggdrasil/semantics/comparison.hpp"

#include <cista/containers/vector.h>
#include <concepts>
#include <limits>
#include <tuple>
#include <type_traits>

namespace ygg
{

template<typename T>
struct Data;

template<typename T>
using DataList = ::cista::offset::vector<Data<T>>;

template<typename T>
using DataMatrix = ::cista::offset::vector<DataList<T>>;

template<typename T>
struct Builder;

/// The index of a record of type T in its owning repository. Specialize it only for
/// indices that are not a single integer, e.g. formalism::RelationBinding.
template<typename T>
struct Index
{
    using value_type = uint_t;

    value_type value {};

    static constexpr value_type MAX = std::numeric_limits<value_type>::max();

    constexpr Index() noexcept : value(MAX) {}
    constexpr explicit Index(value_type value) noexcept : value(value) {}

    static constexpr Index max() noexcept { return Index(MAX); }

    constexpr bool is_max() const noexcept { return value == MAX; }

    explicit constexpr operator value_type() const noexcept { return value; }

    constexpr value_type get_value() const noexcept { return value; }

    static constexpr auto cista_members() noexcept { return ygg::Members<&Index::value> {}; }
    static constexpr auto identifying_members() noexcept { return ygg::Members<&Index::value> {}; }
};

template<typename T>
using IndexList = ::cista::offset::vector<Index<T>>;

template<typename T>
using IndexMatrix = ::cista::offset::vector<IndexList<T>>;

template<typename T, typename C>
struct View
{
};

template<typename T, typename C>
class IndexViewBase;
template<typename T, typename C>
class DataViewBase;
template<typename T, typename C>
class BuilderViewBase;

/// Whether a context exposes a canonical owner for a handle.
template<typename C, typename T>
concept CanonicalizableContext = requires(const C& context, const T& handle) {
    { context.get_canonical_context(handle) } -> std::same_as<const C&>;
};

/// Domain overloads may select the owner of an embedded handle.
template<typename T, typename C>
const C& get_canonical_context(const T&, const C& context YGG_LIFETIMEBOUND) noexcept
{
    return context;
}

template<typename T, CanonicalizableContext<T> C>
const C& get_canonical_context(const T& handle, const C& context) noexcept(noexcept(context.get_canonical_context(handle)))
{
    return context.get_canonical_context(handle);
}

/// Construct the representation using its canonical owner, discovered by ADL.
template<typename T, typename C>
    requires std::constructible_from<View<T, C>, const T&, const C&> && requires(const T& handle, const C& context) {
        { get_canonical_context(handle, context) } -> std::same_as<const C&>;
    }
View<T, C> make_view(const T& handle, const C& context) noexcept(noexcept(get_canonical_context(handle, context))
                                                                 && std::is_nothrow_constructible_v<View<T, C>, const T&, const C&>)
{
    return View<T, C>(handle, get_canonical_context(handle, context));
}

/// True if V is a view of handle H in any context.
template<typename V, typename H>
inline constexpr bool is_view_of_v = false;

template<typename H, typename C>
inline constexpr bool is_view_of_v<View<H, C>, H> = true;

template<typename T, typename C>
concept ViewConcept = requires(const T& type, const C& context, const View<T, C>& view) {
    // Constructor
    View<T, C>(type, context);
    // Helper
    { make_view(type, context) } -> std::same_as<View<T, C>>;
    // Method to retrieve the underlying Data.
    view.get_data();
    // Method to retrieve the underlying context.
    { view.get_context() } -> std::same_as<const C&>;
    // Method to retrieve the underlying lightweight handle or data.
    { view.get_handle() } -> std::same_as<const T&>;
};

/// The type a container view yields for a stored T: View<T, C> if T is viewable in C, else T itself.
template<typename T, typename C>
using ViewOrValue = std::conditional_t<ViewConcept<T, C>, View<T, C>, T>;

// Storage decision: trivially copyable data types are treated as flat in-memory
// values.
template<typename T>
inline constexpr bool uses_trivial_storage_v = std::is_trivially_copyable_v<Data<T>> && std::is_default_constructible_v<Data<T>>;

/// Whether views of a record hold it by value rather than borrowing it. Defaults to
/// trivially copyable records; small records built on the fly may opt in by specialization.
template<typename T>
inline constexpr bool stores_view_by_value_v = uses_trivial_storage_v<T>;
}  // namespace ygg

#include "yggdrasil/core/details/types.hpp"

#endif
