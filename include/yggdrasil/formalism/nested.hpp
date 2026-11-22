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

template<typename T, typename List>
struct reaches_any;

template<typename C, typename List>
struct nests_any;

template<typename K, typename List>
struct resolves_any;
}  // namespace detail

/// Whether repository R holds records of T itself, or one of its nested repositories reaches them.
template<typename R, typename T>
inline constexpr bool reaches = holds_records<R, T> || detail::reaches_any<T, typename detail::nested_of<R>::type>::value;

/// Whether repository C is R, or nested in R at any depth.
template<typename R, typename C>
inline constexpr bool nests = std::same_as<R, C> || detail::nests_any<C, typename detail::nested_of<R>::type>::value;

namespace detail
{
template<typename T, typename... Rs>
struct reaches_any<T, Nested<Rs...>> : std::bool_constant<(reaches<std::remove_const_t<Rs>, T> || ...)>
{
};

template<typename C, typename... Rs>
struct nests_any<C, Nested<Rs...>> : std::bool_constant<(nests<std::remove_const_t<Rs>, C> || ...)>
{
};
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

template<typename K>
struct resolves_q
{
    template<typename P>
    using fn = std::bool_constant<resolves<std::remove_cvref_t<P>, K>>;
};
}  // namespace detail

namespace detail
{
template<typename R>
using as_part = const R&;
}  // namespace detail

/// The parts of repository R that resolve its keys (R[key]), in order, as KeyedRepository builds them: R::OwnParts, a
/// std::tuple of what holds R's own records (each naming its KeyTypes), then a reference to each repository nested in
/// R (Nests).
template<typename R>
using key_parts = boost::mp11::mp_append<typename R::OwnParts,
                                         boost::mp11::mp_transform<detail::as_part, boost::mp11::mp_rename<typename detail::nested_of<R>::type, std::tuple>>>;

/// Whether exactly one of Parts resolves keys K: the one a repository resolves a key through.
template<typename Parts, typename K>
concept OnePartResolves = boost::mp11::mp_count_if_q<Parts, detail::resolves_q<K>>::value == 1;

/// The position among Parts of the one part that resolves keys K: a compile error where none, or several, do.
template<typename Parts, typename K>
consteval std::size_t part_resolving()
{
    static_assert(OnePartResolves<Parts, K>, "ygg::formalism: exactly one part of a repository (its own stores, its nested repositories) resolves a key");
    return boost::mp11::mp_find_if_q<Parts, detail::resolves_q<K>>::value;
}

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

/// The nested repositories of a repository Derived (CRTP), held by std::shared_ptr as flat bases (not a std::tuple), and
/// what reaches them: a record of a kind Derived does not hold resolves through the one nested repository that reaches
/// its kind, at any depth (computed at compile time), so a view's context is always the repository owning its record.
/// A kind reached through two nested repositories resolves through neither: a compile error where it is resolved.
/// Derived brings the members in beside its own (`using Nests::view;` ...).
template<typename Derived, typename... Rs>
class Nests<Derived, Nested<Rs...>> : private detail::NestedMember<Rs>...
{
    static_assert(boost::mp11::mp_is_set<boost::mp11::mp_list<std::remove_const_t<Rs>...>>::value, "ygg::formalism::Nests: each repository once");

    template<typename T>
    static constexpr std::size_t reaching = (std::size_t { reaches<std::remove_const_t<Rs>, T> } + ... + 0);

    template<typename C>
    static constexpr std::size_t nesting = (std::size_t { nests<std::remove_const_t<Rs>, C> } + ... + 0);

    template<typename T>
    struct reaches_q
    {
        template<typename R>
        using fn = std::bool_constant<reaches<std::remove_const_t<R>, T>>;
    };

    template<typename C>
    struct nests_q
    {
        template<typename R>
        using fn = std::bool_constant<nests<std::remove_const_t<R>, C>>;
    };

    using List = boost::mp11::mp_list<Rs...>;

    /// The nested repository (as declared: `const R` or `R`) that reaches kind T, and the one that nests repository C.
    template<typename T>
    using NestedFor = boost::mp11::mp_at<List, boost::mp11::mp_find_if_q<List, reaches_q<T>>>;
    template<typename C>
    using NestedOver = boost::mp11::mp_at<List, boost::mp11::mp_find_if_q<List, nests_q<C>>>;

    /// A kind Derived does not hold and exactly one nested repository reaches.
    template<typename T>
    static constexpr bool forwards = !holds_records<Derived, T> && reaching<T> == 1;

    /// A repository other than Derived that exactly one nested repository nests.
    template<typename C>
    static constexpr bool forwards_over = !std::same_as<C, Derived> && nesting<C> == 1;

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

    /// The repository below that holds records of kind T, at any depth.
    template<typename T>
        requires forwards<T>
    [[nodiscard]] const auto& repository_for() const noexcept
    {
        const auto& nested = member<NestedFor<T>>();
        if constexpr (holds_records<std::remove_cvref_t<decltype(nested)>, T>)
            return nested;
        else
            return nested.template repository_for<T>();
    }

    /// An index of a kind held below, as its view there.
    template<typename T>
        requires forwards<T>
    [[nodiscard]] auto view(urd::index<T> index) const
    { return member<NestedFor<T>>().view(index); }

    /// A record of a kind held below, held in place, as its view there.
    template<typename T>
        requires forwards<T>
    [[nodiscard]] auto view(const urd::data<T>& record URD_LIFETIMEBOUND) const
    { return urd::resolve(record, member<NestedFor<T>>()); }

    template<typename T>
        requires forwards<T>
    void view(const urd::data<T>&& record) const = delete;

    /// A builder of a repository below, as its view there.
    template<typename T, typename C>
        requires forwards_over<C>
    [[nodiscard]] auto view(const urd::builder<T, C>& built URD_LIFETIMEBOUND) const
    { return urd::resolve(built, member<NestedOver<C>>()); }

    template<typename T, typename C>
        requires forwards_over<C>
    void view(const urd::builder<T, C>&& built) const = delete;

    /// Interns a builder of a repository below there, where it is nested to be interned into and takes the builder
    /// storage (a symbol context: where the storage holds its scratch records too); else it is interned there first.
    template<typename T, typename C, typename Storage>
        requires forwards_over<C> && requires(NestedOver<C>& nested, const urd::builder<T, C>& built, Storage& storage) { nested.insert(built, storage); }
    decltype(auto) insert(const urd::builder<T, C>& built, Storage& storage)
    { return member<NestedOver<C>>().insert(built, storage); }

    /// Finds the record a builder of a repository below writes, there, likewise.
    template<typename T, typename C, typename Storage>
        requires forwards_over<C> && requires(NestedOver<C>& nested, const urd::builder<T, C>& built, Storage& storage) { nested.find(built, storage); }
    decltype(auto) find(const urd::builder<T, C>& built, Storage& storage)
    { return member<NestedOver<C>>().find(built, storage); }

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
