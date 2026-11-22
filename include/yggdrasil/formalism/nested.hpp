/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_FORMALISM_NESTED_HPP_
#define YGG_FORMALISM_NESTED_HPP_

#include "yggdrasil/formalism/declarations.hpp"
#include "yggdrasil/formalism/interning.hpp"

#include <boost/mp11.hpp>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <urd/elements/view.hpp>
#include <utility>

namespace ygg::formalism
{

/// The repositories nested in a repository: those holding the records further down its resolution tree (a match tree's
/// nodes refer to the planning formalism's atoms, so the planning repository is nested in the match tree repository).
/// `const R`: read only (viewed and searched); `R`: also interned into. Each type once.
template<typename... Rs>
struct Nested
{
};

namespace detail
{
template<typename R>
struct nested_of
{
    using type = Nested<>;
};

template<NestsRepositories R>
struct nested_of<R>
{
    using type = typename R::NestedRepositories;
};

template<typename K, typename List>
struct resolves_any;
}  // namespace detail

/// Whether repository R resolves keys K (R[key]): one of its own KeyTypes, or through one of its nested repositories,
/// at any depth.
template<typename R, typename K>
inline constexpr bool resolves = HoldsKey<R, K> || detail::resolves_any<K, typename detail::nested_of<R>::type>::value;

namespace detail
{
template<typename K, typename... Rs>
struct resolves_any<K, Nested<Rs...>> : std::bool_constant<(resolves<std::remove_const_t<Rs>, K> || ...)>
{
};

}  // namespace detail

namespace detail
{
/// The member of Nests holding the nested repository R (`const R` where read only).
template<typename R>
struct NestedMember
{
    std::shared_ptr<R> repository;
};
}  // namespace detail

template<typename Derived, typename List>
class Nests;

/// The nested repositories of a repository Derived (CRTP), held by std::shared_ptr as flat bases (not a std::tuple): the
/// parts after its own that its keys resolve through (key_parts, KeyedRepository), so that a key of a kind held below
/// is read, viewed, stored and found there, at any depth, and a view's context is always the repository owning its
/// record. A key two parts resolve resolves through none: a compile error where it is resolved.
template<typename Derived, typename... Rs>
class Nests<Derived, Nested<Rs...>> : private detail::NestedMember<Rs>...
{
    static_assert(boost::mp11::mp_is_set<boost::mp11::mp_list<std::remove_const_t<Rs>...>>::value, "ygg::formalism::Nests: each repository once");

    using List = boost::mp11::mp_list<Rs...>;

    template<typename R>
    [[nodiscard]] R& member() const noexcept
    { return *static_cast<const detail::NestedMember<R>&>(*this).repository.get(); }  // get(): operator* would complete R

public:
    using NestedRepositories = Nested<Rs...>;

    /// Over each nested repository, none null.
    explicit Nests(std::shared_ptr<Rs>... repositories) : detail::NestedMember<Rs> { std::move(repositories) }...
    { assert(((static_cast<const detail::NestedMember<Rs>&>(*this).repository != nullptr) && ...)); }

    /// The directly nested repository of type R (read only where nested as `const R`).
    template<typename R>
        requires boost::mp11::mp_contains<List, R>::value || boost::mp11::mp_contains<List, const R>::value
    [[nodiscard]] auto& get() const noexcept
    {
        if constexpr (boost::mp11::mp_contains<List, R>::value)
            return member<R>();
        else
            return member<const R>();
    }

    /// The shared pointer to the directly nested repository of type R.
    template<typename R>
        requires boost::mp11::mp_contains<List, R>::value || boost::mp11::mp_contains<List, const R>::value
    [[nodiscard]] auto get_ptr() const noexcept
    {
        if constexpr (boost::mp11::mp_contains<List, R>::value)
            return static_cast<const detail::NestedMember<R>&>(*this).repository;
        else
            return static_cast<const detail::NestedMember<const R>&>(*this).repository;
    }

    /// Checks that Derived may hold copies of `source`'s records (urd::ChecksCompatibility): each nested repository sees
    /// the one `source` nests in its place (the same one, or a layer it lies over), else std::invalid_argument. A record
    /// of `source` names the records further down by plain values too (a row's objects, a denotation's bits), which name
    /// the same records here only then. urd::copy checks it before it copies.
    void check_compatible(const Derived& source) const
    {
        const auto& nests = static_cast<const Nests&>(source);
        if (!(member<Rs>().sees(nests.template member<Rs>()) && ...))
            throw std::invalid_argument("ygg::formalism::Nests: a source over repositories its target does not see");
    }
};

}  // namespace ygg::formalism

#endif
