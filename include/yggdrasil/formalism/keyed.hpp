/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_FORMALISM_KEYED_HPP_
#define YGG_FORMALISM_KEYED_HPP_

// The one key lookup of a repository: repository[urd::key<Ts...>] through the one part that resolves the key (its own
// stores, its nested repositories: key_parts, nested.hpp), whichever repository it is (Repository, SymbolContext).

#include "yggdrasil/formalism/declarations.hpp"
#include "yggdrasil/formalism/nested.hpp"

#include <boost/mp11.hpp>
#include <cstddef>
#include <optional>
#include <tuple>
#include <urd/elements/children.hpp>
#include <utility>
#include <type_traits>
#include <urd/elements/index.hpp>

namespace ygg::formalism
{

/// The parts of T's key a record of T is known by before it is stored (all but the last, which its store gives): none
/// for an index, a row's relation; the record holds them (urd::built_key).
template<typename T>
using known_parts = boost::mp11::mp_pop_back<boost::mp11::mp_rename<key_t<T>, boost::mp11::mp_list>>;

namespace detail
{
/// The parts of its key a record holds (known_parts: all but the last), as the key it is inserted and found under.
template<typename T>
[[nodiscard]] boost::mp11::mp_rename<known_parts<T>, urd::key> known(const urd::data<T>& record)
{
    return []<typename... Ps>(boost::mp11::mp_list<Ps...> /*parts*/, const urd::data<T>& held) { return urd::built_key<Ps...>(held); }(known_parts<T> {},
                                                                                                                                   record);
}
}  // namespace detail

/// A store a repository O holds (behind its pointer, its Impl, or in place) as a part that resolves the keys Keys (one
/// type per repository, so that each repository's library instantiates its own): read where S is const, read and
/// written where it is not. Every key, an index or one of several parts, is read by urd's one fold (urd::record_at,
/// urd::contains_key: a family of stores first selects the store of the key's kind), and every record stored and found by
/// it (urd::insert_at, urd::find_at, under the parts of its key it holds), which keyed_impl.hpp applies, and a library
/// instantiates where the store is complete (rule 25: naming a part completes no store). It names its owner, the
/// repository a record's view is through.
template<typename O, typename S, typename Keys>
class HeldPart
{
    O* m_owner;
    S* m_store;

public:
    /// The keys it resolves.
    using KeyTypes = Keys;
    /// The repository a record of T it holds is viewed through: its owner.
    template<typename T>
    using Owner = std::remove_const_t<O>;
    /// Whether it writes records under keys K: one of its keys, of a part that writes (S not const).
    template<typename K>
    static constexpr bool writes = !std::is_const_v<S> && HoldsKey<HeldPart, K>;

    HeldPart(O& owner, S& store) noexcept : m_owner(&owner), m_store(&store) {}

    /// The record of a key, through the layers; some layer must hold it (unchecked, as std::vector's). Unconstrained, as
    /// a library instantiates it (Clang names a requires-clause in its symbol, g++ does not).
    template<typename... Ks>
    [[nodiscard]] urd::stored_ref<kind_of_key<urd::key<Ks...>>> operator[](urd::key<Ks...> key) const;

    /// The record of a key, through the layers; std::out_of_range where no layer holds it.
    template<typename... Ks>
    [[nodiscard]] urd::stored_ref<kind_of_key<urd::key<Ks...>>> at(urd::key<Ks...> key) const;

    /// The view of the record of a record's content, where some layer holds it, under the parts of its key it holds.
    template<typename T>
    [[nodiscard]] std::optional<urd::index_view<T, std::remove_const_t<O>, key_t<T>>> find(const urd::data<T>& record) const;

    /// Stores a record where no layer holds it, prepared first by its owner's domain (prepare_for_insert: canonical,
    /// checked; a no-op where it has nothing to do), under the parts of its key it holds, its key's last part written into
    /// it: its view, and whether it was stored now. Of a part that writes (S not const).
    template<typename T>
    std::pair<urd::index_view<T, std::remove_const_t<O>, key_t<T>>, bool> insert(urd::data<T>& record) const;
};

/// The repositories nested in repository R (Nests), as references, in the order key_parts lists them: none where R
/// nests none.
template<typename R, typename... Rs>
[[nodiscard]] std::tuple<const Rs&...> nested_parts(const R& repository, Nested<Rs...> /*nested*/) noexcept
{ return std::tuple<const Rs&...>(static_cast<const Nests<R, Nested<Rs...>>&>(repository).template get<std::remove_const_t<Rs>>()...); }

namespace detail
{
template<typename R>
using as_writable_part = R&;

/// The repositories nested in a repository R that it writes into: those its Nested<...> names non-const.
template<typename R>
using writable_nested = boost::mp11::mp_remove_if<boost::mp11::mp_rename<typename nested_of<R>::type, boost::mp11::mp_list>, std::is_const>;
}  // namespace detail

/// The parts of repository R that records are written into, in order: R::OwnWritableParts, then a reference to each
/// repository nested in R non-const (a const one is read only: a key only it resolves has no insert).
template<typename R>
using writable_key_parts =
    boost::mp11::mp_append<typename R::OwnWritableParts, boost::mp11::mp_rename<boost::mp11::mp_transform<detail::as_writable_part, detail::writable_nested<R>>, std::tuple>>;

namespace detail
{
/// The repository W nested in repository R (Nests), as R writes into it.
template<typename W, typename R>
[[nodiscard]] W& nested_writable(R& repository) noexcept
{ return static_cast<Nests<R, typename R::NestedRepositories>&>(repository).template get<W>(); }
}  // namespace detail

/// The repositories nested in repository R that it writes into, as references, in the order writable_key_parts lists them.
template<typename R, typename... Rs>
[[nodiscard]] boost::mp11::mp_rename<boost::mp11::mp_transform<detail::as_writable_part, detail::writable_nested<R>>, std::tuple>
nested_writable_parts(R& repository, Nested<Rs...> /*nested*/) noexcept
{
    return []<typename... Ws>(boost::mp11::mp_list<Ws...> /*writable*/, R& held)
    { return std::tuple<Ws&...>(detail::nested_writable<Ws>(held)...); }(detail::writable_nested<R> {}, repository);
}

namespace detail
{
template<typename K>
struct writes_q
{
    template<typename P>
    using fn = std::bool_constant<std::remove_cvref_t<P>::template writes<K>>;
};
}  // namespace detail

/// Whether exactly one of Parts writes records under keys K (a part names what it writes: writes<K>; a repository part,
/// through its own parts written into, at any depth): the one a repository stores such a record through.
template<typename Parts, typename K>
concept OnePartWrites = boost::mp11::mp_count_if_q<Parts, detail::writes_q<K>>::value == 1;

/// The position among Parts of the one part that writes records under keys K: a compile error where none, or several, do.
template<typename Parts, typename K>
consteval std::size_t part_writing()
{
    static_assert(OnePartWrites<Parts, K>, "ygg::formalism: exactly one part of a repository written into (its own stores, its nested repositories not const) holds a key");
    return boost::mp11::mp_find_if_q<Parts, detail::writes_q<K>>::value;
}

namespace detail
{
/// The one part of repository R that resolves keys K, as key_parts lists it (a class template, so that naming it in a
/// member of R's base waits for R to be complete); likewise of the parts it writes into.
template<typename R, typename K>
struct key_part
{
    using type = std::tuple_element_t<part_resolving<key_parts<R>, K>(), key_parts<R>>;
};

template<typename R, typename K>
struct writable_key_part
{
    using type = std::tuple_element_t<part_writing<writable_key_parts<R>, K>(), writable_key_parts<R>>;
};
}  // namespace detail

/// The base of a repository Derived (CRTP) that resolves keys: its one operator[] and at(), and its one insert and find
/// of a record. Derived names its own parts and hands them out (OwnParts, own_parts(); OwnWritableParts,
/// own_writable_parts(), in the same order); the base adds the repositories Derived nests (Nests), where Derived is
/// complete: every part, as key_parts and writable_key_parts list them. A record of a kind held below is stored and
/// found there, through the same dispatch.
template<typename Derived>
class KeyedRepository
{
public:
    /// The repository a record of T reached through Derived is viewed through: the owner of the part that resolves it.
    template<typename T>
    using Owner = typename std::remove_cvref_t<typename detail::key_part<Derived, key_t<T>>::type>::template Owner<T>;
    /// Whether it writes records under keys K: through exactly one of its parts written into (a const nested repository
    /// writes nothing).
    template<typename K>
    static constexpr bool writes = OnePartWrites<writable_key_parts<Derived>, K>;

    /// The record of a key, of its last tag, through the one part that resolves it (chosen at compile time): one of its
    /// own stores, which folds over the key's indices through its nested containers (urd::record_at), or a nested
    /// repository, at any depth. A key no part, or several, resolve is no key of it. Some layer must hold the key
    /// (unchecked, as std::vector's).
    template<typename... Ks>
        requires OnePartResolves<key_parts<Derived>, urd::key<Ks...>>
    [[nodiscard]] urd::stored_ref<kind_of_key<urd::key<Ks...>>> operator[](urd::key<Ks...> key) const
    { return part<urd::key<Ks...>>()[key]; }

    /// As operator[], checked: std::out_of_range where no layer holds the key.
    template<typename... Ks>
        requires OnePartResolves<key_parts<Derived>, urd::key<Ks...>>
    [[nodiscard]] urd::stored_ref<kind_of_key<urd::key<Ks...>>> at(urd::key<Ks...> key) const
    { return part<urd::key<Ks...>>().at(key); }

    /// Stores a record where no layer holds it, through the one part that holds its kind and is written into (chosen at
    /// compile time, as for reading: one of its own stores, or a repository it nests non-const, at any depth), prepared
    /// there and stored under the parts of its key it holds (HeldPart::insert): its view through its owner, and whether
    /// it was stored now.
    template<typename T>
        requires OnePartWrites<writable_key_parts<Derived>, key_t<T>>
    std::pair<urd::index_view<T, Owner<T>, key_t<T>>, bool> insert(urd::data<T>& record)
    { return writable_part<key_t<T>>().insert(record); }

    /// The view of the record of a record's content, where some layer holds it, through the same part as reading.
    template<typename T>
        requires OnePartResolves<key_parts<Derived>, key_t<T>>
    [[nodiscard]] std::optional<urd::index_view<T, Owner<T>, key_t<T>>> find(const urd::data<T>& record) const
    { return part<key_t<T>>().find(record); }

private:
    /// The one part that resolves keys K: of Derived's own parts and the repositories it nests, in key_parts' order.
    template<typename K>
    [[nodiscard]] typename detail::key_part<Derived, K>::type part() const
    {
        const auto& self = static_cast<const Derived&>(*this);
        return std::get<part_resolving<key_parts<Derived>, K>()>(std::tuple_cat(self.own_parts(), nested_parts(self, typename detail::nested_of<Derived>::type {})));
    }

    /// The one part written into that resolves keys K, in writable_key_parts' order.
    template<typename K>
    [[nodiscard]] typename detail::writable_key_part<Derived, K>::type writable_part()
    {
        auto& self = static_cast<Derived&>(*this);
        return std::get<part_writing<writable_key_parts<Derived>, K>()>(
            std::tuple_cat(self.own_writable_parts(), nested_writable_parts(self, typename detail::nested_of<Derived>::type {})));
    }
};

}  // namespace ygg::formalism

#endif
