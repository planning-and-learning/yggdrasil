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
#include <cstdint>
#include <memory>
#include <urd/elements/data.hpp>

namespace ygg::formalism
{

/// What gives a builder back to its storage when its handle goes: the slot it came from, by its storage's pool of its
/// type and its number there, and the function that gives it back (builder_impl.hpp), so that a handle names no store.
struct BuilderRelease
{
    void* slots = nullptr;
    std::uint64_t handle = 0;
    void (*release)(void* slots, std::uint64_t handle, void* record) noexcept = nullptr;

    template<typename T>
    void operator()(urd::data<T>* record) const noexcept
    { release(slots, handle, record); }
};

/// A record of type T to build in, from a BuilderStorage: back to its storage, contents kept, when the handle goes.
template<typename T>
using BuilderHandle = std::unique_ptr<urd::data<T>, BuilderRelease>;

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
    [[nodiscard]] BuilderHandle<T> get_builder()
    { return do_get_builder<T>(); }

    /// Clear pooled data once, member by member, retaining its reusable buffers.
    template<typename T>
        requires(boost::mp11::mp_contains<boost::mp11::mp_list<Ts...>, T>::value)
    [[nodiscard]] BuilderHandle<T> checkout()
    { return do_checkout<T>(); }

private:
    // What a library instantiates: the constrained members above, unconstrained, which they forward to. Clang names an
    // instantiation's requires-clause in its symbol and g++ does not, so only unconstrained symbols link between code
    // built by either.
    template<typename T>
    [[nodiscard]] BuilderHandle<T> do_get_builder();
    template<typename T>
    [[nodiscard]] BuilderHandle<T> do_checkout();

    std::unique_ptr<Impl> m_impl;
};

/// The builder storage of the types Ts.
template<typename... Ts>
using BuilderStorage = BasicBuilderStorage<boost::mp11::mp_list<Ts...>>;

template<typename T, typename... Ts>
    requires requires(BuilderStorage<Ts...>& builder) { builder.template checkout<T>(); }
[[nodiscard]] BuilderHandle<T> checkout(BuilderStorage<Ts...>& builder)
{ return builder.template checkout<T>(); }

}  // namespace ygg::formalism

#endif
