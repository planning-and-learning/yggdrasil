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

#include <boost/mp11.hpp>
#include <memory>
#include <urd/elements/builder.hpp>
#include <urd/elements/data.hpp>

namespace ygg::formalism
{

/// One pool of reusable records per type, behind a pointer: builder_impl.hpp defines the pools (a Family of urd slots)
/// and the members, and a library instantiates its storages in one source (builder_instantiations.hpp), so that its
/// other sources complete no pool. A specialization over its type list, so that clang
/// matches the out-of-line members, whose constraints name the types, to their declarations (as SymbolContext).
template<typename Types>
class BasicBuilderStorage;

template<typename... Ts>
class BasicBuilderStorage<boost::mp11::mp_list<Ts...>>
{
public:
    using Types = boost::mp11::mp_list<Ts...>;

    /// The pools: defined in builder_impl.hpp.
    struct Impl;

    BasicBuilderStorage();
    BasicBuilderStorage(const BasicBuilderStorage&) = delete;
    BasicBuilderStorage& operator=(const BasicBuilderStorage&) = delete;
    BasicBuilderStorage(BasicBuilderStorage&&) = delete;
    BasicBuilderStorage& operator=(BasicBuilderStorage&&) = delete;
    ~BasicBuilderStorage();

    /// Acquire pooled data without resetting its contents.
    template<typename T>
        requires(boost::mp11::mp_contains<boost::mp11::mp_list<Ts...>, T>::value)
    [[nodiscard]] urd::handle<urd::data<T>> get_builder()
    { return do_get_builder<T>(); }

    /// Clear pooled data once, member by member, retaining its reusable buffers.
    template<typename T>
        requires(boost::mp11::mp_contains<boost::mp11::mp_list<Ts...>, T>::value)
    [[nodiscard]] urd::handle<urd::data<T>> checkout()
    { return do_checkout<T>(); }

private:
    // What a library instantiates: the constrained members above, unconstrained, which they forward to. Clang names an
    // instantiation's requires-clause in its symbol and g++ does not, so only unconstrained symbols link between code
    // built by either.
    template<typename T>
    [[nodiscard]] urd::handle<urd::data<T>> do_get_builder();
    template<typename T>
    [[nodiscard]] urd::handle<urd::data<T>> do_checkout();

    std::unique_ptr<Impl> m_impl;
};

/// The builder storage of the types Ts.
template<typename... Ts>
using BuilderStorage = BasicBuilderStorage<boost::mp11::mp_list<Ts...>>;

template<typename T, typename... Ts>
    requires requires(BuilderStorage<Ts...>& builder) { builder.template checkout<T>(); }
[[nodiscard]] urd::handle<urd::data<T>> checkout(BuilderStorage<Ts...>& builder)
{ return builder.template checkout<T>(); }

}  // namespace ygg::formalism

#endif
