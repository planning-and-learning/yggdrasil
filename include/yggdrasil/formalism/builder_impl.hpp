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

#include <memory>
#include <type_traits>
#include <urd/elements/concepts.hpp>
#include <urd/elements/flat.hpp>
#include <urd/repr/slots/slots.hpp>
#include <urd/store.hpp>

namespace ygg::formalism
{
namespace detail
{
template<typename T>
using RecordSlot = urd::Store<urd::slots<urd::data<T>>>;

template<typename C>
struct BuilderSlotOf
{
    template<typename T>
    using fn = urd::Store<urd::slots<urd::builder<T, C>>>;
};
}  // namespace detail

template<typename C, typename... Ts>
struct BasicBuilderStorage<C, boost::mp11::mp_list<Ts...>>::Impl
{
    Family<detail::BuilderSlotOf<C>::template fn, Ts...> builders;
    Family<detail::RecordSlot, Ts...> records;
};

template<typename C, typename... Ts>
BasicBuilderStorage<C, boost::mp11::mp_list<Ts...>>::BasicBuilderStorage() : m_impl(std::make_unique<Impl>())
{
}

template<typename C, typename... Ts>
BasicBuilderStorage<C, boost::mp11::mp_list<Ts...>>::~BasicBuilderStorage()  // NOLINT(modernize-use-equals-default): GCC rejects an explicit instantiation
                                                                             // of an out-of-line = default
{
}

template<typename C, typename... Ts>
template<typename T>
urd::handle<urd::builder<T, C>> BasicBuilderStorage<C, boost::mp11::mp_list<Ts...>>::do_checkout()
{
    auto built = m_impl->builders.template get<T>().make_handle();
    urd::clear_fields(*built);
    return built;
}

template<typename C, typename... Ts>
template<typename T>
urd::handle<urd::data<T>> BasicBuilderStorage<C, boost::mp11::mp_list<Ts...>>::do_scratch()
{
    auto record = m_impl->records.template get<T>().make_handle();
    urd::clear_fields(*record);
    return record;
}

}  // namespace ygg::formalism

#endif
