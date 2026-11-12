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

#include "yggdrasil/core/family.hpp"

#include <boost/mp11.hpp>
#include <urd/elements/flat.hpp>
#include <urd/repr/slots/slots.hpp>
#include <urd/store.hpp>

namespace ygg::formalism
{
namespace detail
{
template<typename T>
using BuilderSlot = urd::Store<urd::slots<urd::data<T>>>;
}

/// One pool of reusable records per type (a Family of urd slots).
template<typename... Ts>
class BuilderStorage
{
    Family<detail::BuilderSlot, Ts...> m_slots;

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
    { return m_slots.template get<T>().make_unique(); }

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
