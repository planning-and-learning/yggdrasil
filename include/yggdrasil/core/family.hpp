/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_CORE_FAMILY_HPP_
#define YGG_CORE_FAMILY_HPP_

#include <boost/mp11.hpp>
#include <concepts>
#include <cstddef>
#include <type_traits>
#include <urd/elements/index.hpp>

namespace ygg
{
namespace detail
{
/// The member of a Family for T: an M, made with the family's number where M numbers its elements (urd::layer).
template<typename T, typename M>
struct FamilyMember
{
    M member;

    explicit FamilyMember(std::size_t number) : member(make(number)) {}

private:
    static M make(std::size_t number)
    {
        if constexpr (std::constructible_from<M, urd::layer>)
            return M(urd::layer { number });
        else
            return M();
    }
};

template<template<typename> class F, typename List>
struct FamilyMembers;

template<template<typename> class F, typename... Ts>
struct FamilyMembers<F, boost::mp11::mp_list<Ts...>> : FamilyMember<Ts, F<Ts>>...
{
    explicit FamilyMembers(std::size_t number) : FamilyMember<Ts, F<Ts>>(number)... {}
};
}  // namespace detail

/// One F<T> for each type T in Ts, reached by its type: a repository per kind of entity, a builder slot per record type.
/// F is usually an alias of a urd Store. Not a urd kind: a fixed set of Stores named by type.
///
/// The members are flat bases, not a std::tuple (whose recursive _Tuple_impl chain and constructor constraints are
/// costly over the long type lists of the formalisms). The bases are listed in reverse, so members are destroyed in type
/// order as in std::tuple: a member may own handles into members of later types.
///
/// Where the members layer, a family layers too: Family(&parent) layers each member over the parent's member of its type
/// and numbers itself along the chain (urd::layer). Its operator[] reads an element by its member's handle (a pool of
/// urd::data<T> handing out index<T>), whichever layer holds it.
template<template<typename> class F, typename... Ts>
class Family : private detail::FamilyMembers<F, boost::mp11::mp_reverse<boost::mp11::mp_list<Ts...>>>
{
    using Members = detail::FamilyMembers<F, boost::mp11::mp_reverse<boost::mp11::mp_list<Ts...>>>;

    static_assert(boost::mp11::mp_is_set<boost::mp11::mp_list<Ts...>>::value, "ygg::Family: each type once");
    static constexpr bool layerable = (requires(F<Ts>& member, const F<Ts>* parent) { member.layer_over(parent); } && ...);

    const Family* m_parent = nullptr;
    std::size_t m_layer = 0;

public:
    Family() : Members(0) {}
    /// A layer over `parent` (nullptr: a root): each member layered over the parent's member of its type. The parent must
    /// outlive this family and not grow after the branch.
    explicit Family(const Family* parent)
        requires layerable
        : Members(parent == nullptr ? 0 : parent->m_layer + 1), m_parent(parent), m_layer(parent == nullptr ? 0 : parent->m_layer + 1)
    {
        if (parent != nullptr)
            (get<Ts>().layer_over(&parent->template get<Ts>()), ...);
    }

    /// The member for T, which must be one of Ts.
    template<typename T>
        requires boost::mp11::mp_contains<boost::mp11::mp_list<Ts...>, T>::value
    [[nodiscard]] F<T>& get() noexcept
    { return static_cast<detail::FamilyMember<T, F<T>>&>(*this).member; }
    template<typename T>
        requires boost::mp11::mp_contains<boost::mp11::mp_list<Ts...>, T>::value
    [[nodiscard]] const F<T>& get() const noexcept
    { return static_cast<const detail::FamilyMember<T, F<T>>&>(*this).member; }

    /// Calls `function` with each member in turn, in the order of Ts.
    template<typename Function>
    void for_each(Function function)
    { (function(get<Ts>()), ...); }
    template<typename Function>
    void for_each(Function function) const
    { (function(get<Ts>()), ...); }

    [[nodiscard]] const Family* parent() const noexcept { return m_parent; }
    /// This family's number along the chain: 0 for a root.
    [[nodiscard]] std::size_t layer() const noexcept { return m_layer; }

    /// The element of `at`, from the member for T, whichever layer holds it.
    template<typename T>
    [[nodiscard]] decltype(auto) operator[](urd::index<T> at) const
    { return get<T>()[at]; }
    /// Whether the member for T has a record at `at`.
    template<typename T>
    [[nodiscard]] bool contains(urd::index<T> at) const
    { return !at.is_none() && at.value < get<T>().size(); }
    /// Clears every member.
    void clear()
        requires(requires(F<Ts>& member) { member.clear(); } && ...)
    { (get<Ts>().clear(), ...); }
};

}  // namespace ygg

#endif
