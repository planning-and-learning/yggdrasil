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

#include "yggdrasil/core/dependent_false.hpp"

#include <cista/containers/vector.h>
#include <concepts>
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

template<typename T>
struct Index;

template<typename T>
using IndexList = ::cista::offset::vector<Index<T>>;

template<typename T>
using IndexMatrix = ::cista::offset::vector<IndexList<T>>;

template<typename T, typename C>
struct View
{
};

template<typename T, typename C>
struct View<Data<T>, C>
{
private:
    const Data<T>* m_handle;
    const C* m_context;

public:
    View(const Data<T>& handle, const C& context) noexcept : m_handle(&handle), m_context(&context) {}

    const auto& get_data() const noexcept { return *m_handle; }
    const auto& get_context() const noexcept { return *m_context; }
    const auto& get_handle() const noexcept { return *m_handle; }
};

template<typename T, typename C>
struct View<Index<T>, C>
{
private:
    Index<T> m_handle;
    const C* m_context;

public:
    View(Index<T> handle, const C& context) noexcept : m_handle(handle), m_context(&context) {}

    decltype(auto) get_data() const
        requires requires(const C& context, Index<T> index) { context[index]; }
    {
        return (*m_context)[m_handle];
    }
    const auto& get_context() const noexcept { return *m_context; }
    const auto& get_handle() const noexcept { return m_handle; }
    auto get_index() const noexcept { return m_handle; }
};

/// Whether a context exposes a canonical owner for a handle.
template<typename T, typename C>
concept CanonicalizableContext = requires(const C& context, const T& handle) {
    { context.get_canonical_context(handle) } -> std::same_as<const C&>;
};

template<typename C, typename T>
concept CanonicalizableContextFor = CanonicalizableContext<T, C>;

/// Domain overloads may select the owner of an embedded handle.
template<typename T, typename C>
const C& get_canonical_context(const T&, const C& context) noexcept
{
    return context;
}

template<typename T, CanonicalizableContextFor<T> C>
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

// Storage decision: trivially copyable data types are treated as flat in-memory
// values.
template<typename T>
inline constexpr bool uses_trivial_storage_v = std::is_trivially_copyable_v<Data<T>> && std::is_default_constructible_v<Data<T>>;
}  // namespace ygg

#endif
