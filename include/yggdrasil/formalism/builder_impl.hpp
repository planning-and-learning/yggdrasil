/*
 * Copyright (C) 2026 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef YGG_FORMALISM_BUILDER_IMPL_HPP_
#define YGG_FORMALISM_BUILDER_IMPL_HPP_

// The definitions of BuilderStorage (builder.hpp): its pools (Impl, a Family of urd slots) and its members. Included
// by the source that instantiates a library's storages (builder_instantiations.hpp), and by code whose storages are
// not a library's (yggdrasil's tests).

#include "yggdrasil/core/family.hpp"
#include "yggdrasil/formalism/builder.hpp"

#include <cstdint>
#include <memory>
#include <type_traits>
#include <urd/elements/flat.hpp>
#include <urd/repr/slots/slots.hpp>
#include <urd/store.hpp>

namespace ygg::formalism
{
namespace detail
{
template<typename T>
using BuilderSlot = urd::Store<urd::slots<urd::data<T>>>;

/// Gives the record back to its pool, as urd's releaser of the slot does (BuilderRelease::release).
template<typename T>
void release_builder(void* slots, std::uint64_t handle, void* record) noexcept
{
    using Slots = BuilderSlot<T>;
    using Index = typename Slots::handle_type;
    typename Slots::releaser { static_cast<Slots*>(slots), Index(static_cast<typename Index::value_type>(handle)) }(static_cast<urd::data<T>*>(record));
}
}  // namespace detail

template<typename... Ts>
struct BasicBuilderStorage<boost::mp11::mp_list<Ts...>>::Impl
{
    Family<detail::BuilderSlot, Ts...> slots;
};

template<typename... Ts>
BasicBuilderStorage<boost::mp11::mp_list<Ts...>>::BasicBuilderStorage() : m_impl(std::make_unique<Impl>())
{
}

template<typename... Ts>
BasicBuilderStorage<boost::mp11::mp_list<Ts...>>::~BasicBuilderStorage()  // NOLINT(modernize-use-equals-default): GCC rejects an explicit instantiation of an out-of-line = default
{
}

template<typename... Ts>
template<typename T>
    requires(boost::mp11::mp_contains<boost::mp11::mp_list<Ts...>, T>::value)
BuilderHandle<T> BasicBuilderStorage<boost::mp11::mp_list<Ts...>>::get_builder()
{
    auto record = m_impl->slots.template get<T>().make_unique();
    const auto releaser = record.get_deleter();
    return BuilderHandle<T>(record.release(), BuilderRelease { releaser.store, releaser.handle.value, &detail::release_builder<T> });
}

template<typename... Ts>
template<typename T>
    requires(boost::mp11::mp_contains<boost::mp11::mp_list<Ts...>, T>::value)
BuilderHandle<T> BasicBuilderStorage<boost::mp11::mp_list<Ts...>>::checkout()
{
    auto data = get_builder<T>();
    urd::clear_members(*data);
    return data;
}

}  // namespace ygg::formalism

#endif
