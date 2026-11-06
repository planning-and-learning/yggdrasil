/*
 * Copyright (C) 2026 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef YGG_FORMALISM_BUILDER_HPP_
#define YGG_FORMALISM_BUILDER_HPP_

#include <urd/repr/slots/slots.hpp>
#include <urd/store.hpp>
#include <boost/mp11.hpp>
#include <urd/elements/flat.hpp>

namespace ygg::formalism
{
namespace detail
{
template<typename T>
struct BuilderSlot
{
    urd::Store<urd::slots<urd::data<T>>> pool;
};

template<typename List>
struct BuilderSlots;

template<typename... Ts>
struct BuilderSlots<boost::mp11::mp_list<Ts...>> : BuilderSlot<Ts>...
{
};
}  // namespace detail

/// One pool per type, held in flat bases: a std::tuple over many types instantiates a recursive
/// _Tuple_impl chain and its constructor constraints. The bases are listed in reverse, so pools are
/// destroyed in type order as in std::tuple: pooled objects may own handles into pools of later types.
template<typename... Ts>
class BuilderStorage : private detail::BuilderSlots<boost::mp11::mp_reverse<boost::mp11::mp_list<Ts...>>>
{
public:
    using Types = boost::mp11::mp_list<Ts...>;

    BuilderStorage() = default;
    BuilderStorage(const BuilderStorage&) = delete;
    BuilderStorage& operator=(const BuilderStorage&) = delete;
    BuilderStorage(BuilderStorage&&) = delete;
    BuilderStorage& operator=(BuilderStorage&&) = delete;
    ~BuilderStorage() = default;

    /// Acquire pooled data without resetting its contents.
    template<typename T>
        requires(boost::mp11::mp_contains<Types, T>::value)
    [[nodiscard]] auto get_builder()
    { return static_cast<detail::BuilderSlot<T>&>(*this).pool.acquire_unique(); }

    /// Clear pooled data once, member by member, retaining its reusable buffers.
    template<typename T>
        requires(boost::mp11::mp_contains<Types, T>::value)
    [[nodiscard]] auto checkout()
    {
        auto data = get_builder<T>();
        urd::clear_members(*data);
        return data;
    }
};

template<typename T, typename... Ts>
    requires requires(BuilderStorage<Ts...>& builder) { builder.template checkout<T>(); }
[[nodiscard]] auto checkout(BuilderStorage<Ts...>& builder)
{ return builder.template checkout<T>(); }

}  // namespace ygg::formalism

#endif
