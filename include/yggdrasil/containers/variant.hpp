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

#ifndef YGG_CONTAINERS_VARIANT_HPP_
#define YGG_CONTAINERS_VARIANT_HPP_

#include "yggdrasil/core/type_list.hpp"
#include "yggdrasil/core/types.hpp"
#include "yggdrasil/core/types_utils.hpp"

#include <cassert>
#include <cista/containers/variant.h>
#include <type_traits>
#include <utility>
#include <variant>

namespace ygg
{

template<typename C, typename... T>
class View<::cista::offset::variant<T...>, C>
{
public:
    using Variant = ::cista::offset::variant<T...>;

    View(const Variant& handle, const C& context) noexcept : m_context(&context), m_handle(&handle) {}

    const Variant& index_variant() const noexcept { return *m_handle; }

    bool valid() const noexcept { return index_variant().valid(); }

    explicit operator bool() const noexcept { return valid(); }

    template<typename U>
    bool is() const noexcept
    { return cista::holds_alternative<U>(index_variant()); }

    template<typename U>
    decltype(auto) get() const
    {
        if constexpr (ViewConcept<U, C>)
            return make_view(cista::get<U>(index_variant()), get_context());
        else
            return cista::get<U>(index_variant());
    }

    /// Requires valid() (asserted in Debug), like operator[] of a container.
    template<typename F>
    decltype(auto) apply(F&& f) const
    {
        assert(valid());
        return Variant::apply(
            [&]<typename Arg>(const Arg& arg) -> decltype(auto)
            {
                using U = std::decay_t<Arg>;

                if constexpr (ViewConcept<U, C>)
                    return std::forward<F>(f)(make_view(arg, get_context()));
                else
                    return std::forward<F>(f)(arg);
            },
            index_variant().index(),
            index_variant());
    }

    const auto& get_data() const noexcept { return *m_handle; }
    const auto& get_context() const noexcept { return *m_context; }
    const auto& get_handle() const noexcept { return *m_handle; }

private:
    const C* m_context;
    const Variant* m_handle;
};

template<typename Visitor, typename C, typename... T>
constexpr auto visit(Visitor&& vis, View<::cista::offset::variant<T...>, C>&& v)
{ return std::move(v).apply(std::forward<Visitor>(vis)); }

template<typename Visitor, typename C, typename... T>
constexpr auto visit(Visitor vis, const View<::cista::offset::variant<T...>, C>& v)
{ return v.apply(vis); }
}  // namespace ygg
namespace ygg
{

namespace detail
{

template<typename T, typename C>
struct ViewAlternative
{
    using type = T;
};

template<typename T, typename C>
struct ViewAlternative<Index<T>, C>
{
    using type = View<Index<T>, C>;
};

template<typename T, typename C>
struct ViewAlternative<Data<T>, C>
{
    using type = View<Data<T>, C>;
};

template<typename Variant, typename C>
struct ViewVariantBase;

template<typename... Ts, typename C>
struct ViewVariantBase<::cista::offset::variant<Ts...>, C>
{
    using type = std::variant<typename ViewAlternative<Ts, C>::type...>;
};

}  // namespace detail

/// The cista variant member holding one Index<T> per type T in the list.
template<typename List>
using IndexVariant = ApplyTypeListT<::cista::offset::variant, MapTypeListT<Index, List>>;

/// The std::variant that view-based Data constructors accept for a cista variant member:
/// every Index<T> or Data<T> alternative becomes its view in C, other alternatives are kept as is.
/// A class rather than an alias, so constructors taking ViewVariant<Variant, C> deduce C.
template<typename Variant, typename C>
struct ViewVariant : detail::ViewVariantBase<Variant, C>::type
{
    using base_type = typename detail::ViewVariantBase<Variant, C>::type;
    using base_type::base_type;
    ViewVariant(base_type variant) noexcept(std::is_nothrow_move_constructible_v<base_type>) : base_type(std::move(variant)) {}
};

}  // namespace ygg

#endif
