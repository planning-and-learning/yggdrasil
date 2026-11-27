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

#include "yggdrasil/formalism/declarations.hpp"

#include <boost/mp11.hpp>
#include <concepts>
#include <memory>
#include <urd/elements/builder.hpp>
#include <urd/elements/data.hpp>
#include <variant>

namespace ygg::formalism
{
/// One pool of reusable builders and one of reusable records per type, behind a pointer: a writer checks out a builder
/// of T (checkout<T>()), fills it, and interns it into a repository of its context C (repository.insert(builder,
/// storage)), which writes it into a scratch record of T (scratch<T>()). builder_impl.hpp defines the pools (Families of
/// urd slots) and the members, and a library instantiates its storages in one source (builder_instantiations.hpp), so
/// that its other sources complete no pool. A specialization over its type list, so that clang matches the out-of-line
/// members, whose constraints name the types, to their declarations (as SymbolContext).
template<typename C, typename Types>
class BasicBuilderStorage;

template<typename C, typename... Ts>
class BasicBuilderStorage<C, boost::mp11::mp_list<Ts...>>
{
public:
    /// The context the builders' children are read through: the repository they are interned into.
    using Context = C;
    using Types = boost::mp11::mp_list<Ts...>;
    /// The storage whose members a library instantiates (builder_instantiations.hpp): this one.
    using Base = BasicBuilderStorage;

    /// The pools: defined in builder_impl.hpp.
    struct Impl;

    BasicBuilderStorage();
    BasicBuilderStorage(const BasicBuilderStorage&) = delete;
    BasicBuilderStorage& operator=(const BasicBuilderStorage&) = delete;
    BasicBuilderStorage(BasicBuilderStorage&&) = delete;
    BasicBuilderStorage& operator=(BasicBuilderStorage&&) = delete;
    ~BasicBuilderStorage();

    /// A pooled builder of T, emptied field by field (urd::clear_fields), its buffers kept: what a writer fills.
    template<typename T>
        requires(boost::mp11::mp_contains<boost::mp11::mp_list<Ts...>, T>::value)
    [[nodiscard]] urd::handle<urd::builder<T, C>> checkout()
    { return do_checkout<T>(); }

    /// A pooled record of T, emptied field by field (urd::clear_fields), its buffers kept: what a builder is interned
    /// through.
    template<typename T>
        requires(boost::mp11::mp_contains<boost::mp11::mp_list<Ts...>, T>::value)
    [[nodiscard]] urd::handle<urd::data<T>> scratch()
    { return do_scratch<T>(); }

private:
    // What a library instantiates: the constrained members above, unconstrained, which they forward to. Clang names an
    // instantiation's requires-clause in its symbol and g++ does not, so only unconstrained symbols link between code
    // built by either.
    template<typename T>
    [[nodiscard]] urd::handle<urd::builder<T, C>> do_checkout();

    template<typename T>
    [[nodiscard]] urd::handle<urd::data<T>> do_scratch();

    std::unique_ptr<Impl> m_impl;
};

/// The builder types of a storage, as a list: a library derives a class of its own from one (NamesList), so that the
/// names of everything that takes its storage spell that class's name instead of every one of its types.
template<typename... Ts>
struct Builders
{
    using List = Builders;
};

namespace detail
{
template<typename List>
struct builder_types;

template<typename... Ts>
struct builder_types<Builders<Ts...>>
{
    using type = boost::mp11::mp_list<Ts...>;
};
}  // namespace detail

/// The builder storage of a named list of builder types (a class deriving from Builders<Ts...>): the storage of its
/// types, under the list's name.
template<typename C, NamesList Named>
class BasicBuilderStorage<C, Named> : public BasicBuilderStorage<C, typename detail::builder_types<typename Named::List>::type>
{
public:
    /// The storage whose members a library instantiates (builder_instantiations.hpp): the one of its types.
    using Base = BasicBuilderStorage<C, typename detail::builder_types<typename Named::List>::type>;
};

/// Whether S is a builder storage (a BasicBuilderStorage, of any repository): what interning takes its scratch
/// records from (scratch<T>()), for its own records and for those of the repositories nested in it alike.
template<typename S>
inline constexpr bool is_builder_storage = false;
template<typename C, typename Types>
inline constexpr bool is_builder_storage<BasicBuilderStorage<C, Types>> = true;

template<typename S>
concept IsBuilderStorage = is_builder_storage<S>;

/// Whether a builder storage pools T.
template<typename Storage, typename T>
concept Pools = boost::mp11::mp_contains<typename Storage::Types, T>::value;

template<typename T, typename Storage>
    requires Pools<Storage, T>
[[nodiscard]] urd::handle<urd::builder<T, typename Storage::Context>> checkout(Storage& storage)
{ return storage.template checkout<T>(); }

/// The builder a child holds, to fill: one checked out of `storage` (emptied, its buffers kept) where it holds none (it
/// is interned, a record, or its builder was moved out), as urd::builder_of(child, pool) does with a pool of slots.
template<typename T, typename C, typename K, typename Storage>
    requires Pools<Storage, T> && std::same_as<typename Storage::Context, C>
urd::builder<T, C>& builder_of(urd::child<T, C, K>& held, Storage& storage)
{
    if (const auto* built = std::get_if<urd::built_child<T, C>>(&held); built == nullptr || *built == nullptr)
        held.template emplace<urd::built_child<T, C>>(storage.template checkout<T>());
    return *std::get<urd::built_child<T, C>>(held);
}

}  // namespace ygg::formalism

#endif
