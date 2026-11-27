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
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <urd/config.hpp>
#include <urd/elements/children.hpp>
#include <urd/elements/index.hpp>
#include <utility>

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
    return []<typename... Ps>(boost::mp11::mp_list<Ps...> /*parts*/, const urd::data<T>& held)
    { return urd::built_key<Ps...>(held); }(known_parts<T> {}, record);
}
}  // namespace detail

/// Part P of repository O (read where O is const, read and written where it is not): the store O lists P-th among its
/// PartStores, which resolves the keys it lists P-th among its PartKeys (one type per repository, so that each
/// repository's library instantiates its own). Named by its owner and its position only, as the library instantiates
/// it (part_instantiations.hpp): never by the store's types or its keys, which would spell every kind of record in each
/// of its members' names (brokkr's template rule T20). Every key, an index or one of several parts, is read by urd's one
/// fold (urd::record_at, urd::contains_key: a family of stores first selects the store of the key's kind;
/// keyed_read_impl.hpp, included where the store is complete, rule 25: naming a part completes no store), and every
/// record stored and found by it (urd::insert_at, urd::find_at, under the parts of its key it holds), which
/// keyed_impl.hpp applies: only the sources that instantiate a library's repositories include it, so that every insert
/// runs the prepare_for_insert those sources see. It names its owner, the repository a record's view is through.
template<typename O, std::size_t P>
class HeldPart
{
public:
    /// The store it holds, as its owner holds it: read where O is const.
    using Store = std::conditional_t<std::is_const_v<O>,
                                     const boost::mp11::mp_at_c<typename std::remove_const_t<O>::PartStores, P>,
                                     boost::mp11::mp_at_c<typename std::remove_const_t<O>::PartStores, P>>;

private:
    O* m_owner;
    Store* m_store;

public:
    /// The keys it resolves.
    using KeyTypes = boost::mp11::mp_at_c<typename std::remove_const_t<O>::PartKeys, P>;
    /// The repository a record of T it holds is viewed through: its owner.
    template<typename T>
    using Owner = std::remove_const_t<O>;
    /// Whether it writes records under keys K: one of its keys, of a part that writes (O not const).
    template<typename K>
    static constexpr bool writes = !std::is_const_v<O> && HoldsKey<HeldPart, K>;

    HeldPart(O& owner, Store& store) noexcept : m_owner(&owner), m_store(&store) {}

    /// The record of a key, through the layers; some layer must hold it (unchecked, as std::vector's). Unconstrained, as
    /// a library instantiates it (Clang names a requires-clause in its symbol, g++ does not).
    template<typename... Ks>
    [[nodiscard]] urd::stored_ref<kind_of_key<urd::key<Ks...>>> operator[](urd::key<Ks...> key) const;

    /// The record of a key, through the layers; std::out_of_range where no layer holds it.
    template<typename... Ks>
    [[nodiscard]] urd::stored_ref<kind_of_key<urd::key<Ks...>>> at(urd::key<Ks...> key) const;

    /// Whether some layer holds the key (urd::contains_key).
    template<typename... Ks>
    [[nodiscard]] bool contains(urd::key<Ks...> key) const;

    /// Whether this layer holds the key itself, not through a layer below it (urd::owns_key: index comparisons).
    template<typename... Ks>
    [[nodiscard]] bool owns(urd::key<Ks...> key) const;

    /// The number of records of T under the parts `prefix` of their key, along the layers (urd::count_at).
    template<typename T, typename... Ps>
    [[nodiscard]] std::size_t size(urd::key<Ps...> prefix) const;

    /// The number of records of T under `prefix` that this layer holds itself (its parents' not counted).
    template<typename T, typename... Ps>
    [[nodiscard]] std::size_t own_size(urd::key<Ps...> prefix) const;

    /// The memory of this layer's records of T under `prefix`, in bytes.
    template<typename T, typename... Ps>
    [[nodiscard]] std::size_t memory_usage(urd::key<Ps...> prefix) const;

    /// The layer that owns the record of a key: of its owner and the layers below it (parent()), the one that holds the
    /// key itself (owns); nullptr where no layer holds it.
    template<typename... Ks>
    [[nodiscard]] const std::remove_const_t<O>* owner(urd::key<Ks...> key) const;

    /// A key as its view, through the layer that owns its record (owner()); std::out_of_range where no layer holds it.
    template<typename... Ks>
    [[nodiscard]] urd::index_view<kind_of_key<urd::key<Ks...>>, std::remove_const_t<O>, urd::key<Ks...>> view(urd::key<Ks...> key) const
    {
        if (const auto* layer = owner(key))
            return { key, layer };
        throw std::out_of_range("ygg::formalism: no layer holds the key.");
    }

    /// The repository that resolves keys K: its owner.
    template<typename K>
    [[nodiscard]] const std::remove_const_t<O>& resolving() const noexcept
    { return *m_owner; }

    /// The view of the record of a record's content, where some layer holds it, prepared first by its owner's domain as
    /// for inserting (prepare_for_insert), under the parts of its key it holds.
    template<typename T>
    [[nodiscard]] std::optional<urd::index_view<T, std::remove_const_t<O>, key_t<T>>> find(urd::data<T>& record) const;

    /// Stores a record where no layer holds it, prepared first by its owner's domain (prepare_for_insert: canonical,
    /// checked; a no-op where it has nothing to do), under the parts of its key it holds, its key's last part written into
    /// it: its view, and whether it was stored now. Of a part that writes (O not const).
    template<typename T>
    std::pair<urd::index_view<T, std::remove_const_t<O>, key_t<T>>, bool> insert(urd::data<T>& record) const;

    /// Interns a builder of T where its store interns builders itself (store.insert_built(builder, hints...): the key of
    /// the record, and whether it was stored now), with the hints the store takes: its view, and whether it was stored
    /// now. Of a part that writes (O not const).
    template<typename T, typename C, typename... Hints>
        requires(!std::is_const_v<O>) && requires(Store& store, const urd::builder<T, C>& built, const Hints&... hints) { store.insert_built(built, hints...); }
    std::pair<urd::index_view<T, std::remove_const_t<O>, key_t<T>>, bool> insert_built(const urd::builder<T, C>& built, const Hints&... hints) const
    {
        const auto [key, inserted] = m_store->insert_built(built, hints...);
        return { view(key), inserted };
    }

    /// The view of the record a builder of T writes, where its store finds builders itself (store.find_built(builder,
    /// hints...): the optional key of its record), with the hints the store takes.
    template<typename T, typename C, typename... Hints>
        requires requires(const Store& store, const urd::builder<T, C>& built, const Hints&... hints) { store.find_built(built, hints...); }
    [[nodiscard]] std::optional<urd::index_view<T, std::remove_const_t<O>, key_t<T>>> find_built(const urd::builder<T, C>& built, const Hints&... hints) const
    {
        if (const auto key = m_store->find_built(built, hints...))
            return view(*key);
        return std::nullopt;
    }

    /// Reads the stored record of a key into a builder, where its store unpacks records itself (store.unpack_built(key,
    /// builder): the builder's fields as the record's, its buffers reused).
    template<typename T, typename C, typename... Ks>
        requires requires(const Store& store, urd::key<Ks...> key, urd::builder<T, C>& built) { store.unpack_built(key, built); }
    void unpack_built(urd::key<Ks...> key, urd::builder<T, C>& built) const
    { m_store->unpack_built(key, built); }

private:
    /// The layer that owns the record of a key some layer holds (found or inserted just now): of its owner and the layers
    /// below it, the first that holds it itself.
    template<typename... Ks>
    [[nodiscard]] const std::remove_const_t<O>* held_owner(urd::key<Ks...> key) const;
};

namespace detail
{
/// X as a part of access Writable: written into (X) or read (const X).
template<bool Writable, typename X>
using access_t = std::conditional_t<Writable, X, const X>;

/// The repositories nested in repository R, as R reads them (every one, const) or writes into them (those its
/// Nested<...> names non-const: a const one is read only, and a key only it resolves has no insert).
template<typename R, bool Writable>
using nested_list = std::conditional_t<Writable,
                                       boost::mp11::mp_remove_if<boost::mp11::mp_rename<typename nested_of<R>::type, boost::mp11::mp_list>, std::is_const>,
                                       boost::mp11::mp_transform<std::add_const_t, boost::mp11::mp_rename<typename nested_of<R>::type, boost::mp11::mp_list>>>;

template<typename N>
using as_part = N&;

/// The repository N nested in repository R (Nests), as R reads it (N const) or writes into it.
template<typename N, typename R>
[[nodiscard]] N& nested_member(R& repository) noexcept
{
    using Held = std::remove_const_t<R>;
    return static_cast<const Nests<Held, typename nested_of<Held>::type>&>(repository).template get<std::remove_const_t<N>>();
}
}  // namespace detail

/// The parts of repository R, as it reads them (Writable false) or writes records into them (true), in order: its own
/// (R::OwnParts<Writable>, a std::tuple of what holds R's own records, each naming its KeyTypes), then a reference to
/// each repository it nests (detail::nested_list).
template<typename R, bool Writable>
using key_parts = boost::mp11::mp_append<typename R::template OwnParts<Writable>,
                                         boost::mp11::mp_rename<boost::mp11::mp_transform<detail::as_part, detail::nested_list<R, Writable>>, std::tuple>>;

/// The repositories nested in repository R (R const: as it reads them; else as it writes into them), as references, in
/// the order key_parts lists them: none where R nests none.
template<bool Writable, typename R>
[[nodiscard]] boost::mp11::mp_rename<boost::mp11::mp_transform<detail::as_part, detail::nested_list<std::remove_const_t<R>, Writable>>, std::tuple>
nested_parts(R& repository) noexcept
{
    return []<typename... Ns>(boost::mp11::mp_list<Ns...> /*nested*/, R& held)
    { return std::tuple<Ns&...>(detail::nested_member<Ns>(held)...); }(detail::nested_list<std::remove_const_t<R>, Writable> {}, repository);
}

/// Whether part P writes records under keys K: as it says (writes<K>: a held part, of the keys it holds; a keyed
/// repository, through its own parts written into and the repositories it nests).
template<typename P, typename K>
concept WritesKey = requires { std::remove_cvref_t<P>::template writes<K>; } && std::remove_cvref_t<P>::template writes<K>;

/// Whether part P holds keys K for its access: resolves them, as read (resolves: its own KeyTypes, or a repository it
/// nests, at any depth); writes records under them, as written into (WritesKey).
template<typename P, typename K, bool Writable>
concept PartHolds = (!Writable && resolves<std::remove_cvref_t<P>, K>) || (Writable && WritesKey<P, K>);

namespace detail
{
template<typename K, bool Writable>
struct holds_q
{
    template<typename P>
    using fn = std::bool_constant<PartHolds<P, K, Writable>>;
};
}  // namespace detail

/// Whether exactly one of Parts holds keys K for access Writable: the one a repository reads (writes) such a key through.
template<typename Parts, typename K, bool Writable>
concept OnePartHolds = boost::mp11::mp_count_if_q<Parts, detail::holds_q<K, Writable>>::value == 1;

/// The position among Parts of the one part that holds keys K: a compile error where none, or several, do.
template<typename Parts, typename K, bool Writable>
consteval std::size_t part_holding()
{
    static_assert(OnePartHolds<Parts, K, Writable>,
                  "ygg::formalism: exactly one part of a repository (its own stores, its nested repositories; written into: not const) holds a key");
    return boost::mp11::mp_find_if_q<Parts, detail::holds_q<K, Writable>>::value;
}

namespace detail
{
/// The one part of repository R that holds keys K for access Writable, as key_parts lists it (a class template, so that
/// naming it in a member of R's base waits for R to be complete). None where no part, or several, do: naming its type
/// is then a substitution failure (a constrained member's return type is formed before its constraint is checked, by
/// Clang), not the static_assert of part_holding. The choice is a base, so that it is made only where the type is asked
/// for, R complete.
template<bool Holds, typename R, typename K, bool Writable>
struct key_part_if
{
};

template<typename R, typename K, bool Writable>
struct key_part_if<true, R, K, Writable>
{
    using type = std::tuple_element_t<part_holding<key_parts<R, Writable>, K, Writable>(), key_parts<R, Writable>>;
};

template<typename R, typename K, bool Writable>
struct key_part : key_part_if<OnePartHolds<key_parts<R, Writable>, K, Writable>, R, K, Writable>
{
};
}  // namespace detail

/// Whether one of repository R's own parts (not a repository it nests) resolves keys K.
template<typename R, typename K>
concept OwnsKey = OnePartHolds<typename R::template OwnParts<false>, K, false>;

/// Whether a builder storage holds scratch records of T (storage.scratch<T>()), which a builder of T is written into.
template<typename Storage, typename T>
concept ScratchesRecordsOf = requires(Storage& storage) { storage.template scratch<T>(); };

/// Whether part P interns a builder of T of context C through a builder storage (part.insert(built, storage)).
template<typename P, typename T, typename C, typename Storage>
concept InsertsBuilder = requires(P& part, const urd::builder<T, C>& built, Storage& storage) { part.insert(built, storage); };

/// Whether part P finds the record a builder of T of context C writes (part.find(built, storage)).
template<typename P, typename T, typename C, typename Storage>
concept FindsBuilder = requires(P& part, const urd::builder<T, C>& built, Storage& storage) { part.find(built, storage); };

/// Whether repository R interns a builder of T of context C through `Storage`: one of its own (C is R), whose storage
/// holds scratch records of T; or one of another repository, interned through the part of R that writes T's key (a
/// repository it nests, or a part of its own that packs its records: a state's storages).
template<typename R, typename T, typename C, typename Storage>
concept InternsBuilder = (std::same_as<C, R> && ScratchesRecordsOf<Storage, T>)
                         || (!std::same_as<C, R> && OnePartHolds<key_parts<R, true>, key_t<T>, true>
                             && InsertsBuilder<std::remove_reference_t<typename detail::key_part<R, key_t<T>, true>::type>, T, C, Storage>);

/// Likewise for finding.
template<typename R, typename T, typename C, typename Storage>
concept FindsBuilderOf = (std::same_as<C, R> && ScratchesRecordsOf<Storage, T>)
                         || (!std::same_as<C, R> && OnePartHolds<key_parts<R, true>, key_t<T>, true>
                             && FindsBuilder<std::remove_reference_t<typename detail::key_part<R, key_t<T>, true>::type>, T, C, Storage>);

/// The base of a repository Derived (CRTP) that resolves keys: its one operator[] and at(), and its one insert and find
/// of a record. Derived names its own parts and hands them out (OwnParts<Writable>: own_parts() const as read,
/// own_parts() as written into, in the same order); the base adds the repositories Derived nests (Nests), where Derived
/// is complete: every part, as key_parts lists them. A record of a kind held below is stored and
/// found there, through the same dispatch.
template<typename Derived>
class KeyedRepository
{
public:
    /// The repository a record of T reached through Derived is viewed through: the owner of the part that resolves it.
    template<typename T>
    using Owner = typename std::remove_cvref_t<typename detail::key_part<Derived, key_t<T>, false>::type>::template Owner<T>;
    /// Whether it writes records under keys K: through exactly one of its parts written into (a const nested repository
    /// writes nothing).
    template<typename K>
    static constexpr bool writes = OnePartHolds<key_parts<Derived, true>, K, true>;

    /// The record of a key, of its last tag, through the one part that resolves it (chosen at compile time): one of its
    /// own stores, which folds over the key's indices through its nested containers (urd::record_at), or a nested
    /// repository, at any depth. A key no part, or several, resolve is no key of it. Some layer must hold the key
    /// (unchecked, as std::vector's).
    template<typename... Ks>
        requires OnePartHolds<key_parts<Derived, false>, urd::key<Ks...>, false>
    [[nodiscard]] urd::stored_ref<kind_of_key<urd::key<Ks...>>> operator[](urd::key<Ks...> key) const
    { return part_of<urd::key<Ks...>, false>(self())[key]; }

    /// As operator[], checked: std::out_of_range where no layer holds the key.
    template<typename... Ks>
        requires OnePartHolds<key_parts<Derived, false>, urd::key<Ks...>, false>
    [[nodiscard]] urd::stored_ref<kind_of_key<urd::key<Ks...>>> at(urd::key<Ks...> key) const
    { return part_of<urd::key<Ks...>, false>(self()).at(key); }

    /// Whether some layer holds the key, through the one part that resolves it.
    template<typename... Ks>
        requires OnePartHolds<key_parts<Derived, false>, urd::key<Ks...>, false>
    [[nodiscard]] bool contains(urd::key<Ks...> key) const
    { return part_of<urd::key<Ks...>, false>(self()).contains(key); }

    /// Whether this layer holds the key itself, not through a layer below it, through the one part that resolves it.
    template<typename... Ks>
        requires OnePartHolds<key_parts<Derived, false>, urd::key<Ks...>, false>
    [[nodiscard]] bool owns(urd::key<Ks...> key) const
    { return part_of<urd::key<Ks...>, false>(self()).owns(key); }

    /// The layer that owns the record of a key (its canonical context), through the one part that resolves it; nullptr
    /// where no layer holds it.
    template<typename... Ks>
        requires OnePartHolds<key_parts<Derived, false>, urd::key<Ks...>, false>
    [[nodiscard]] const Owner<kind_of_key<urd::key<Ks...>>>* owner(urd::key<Ks...> key) const
    { return part_of<urd::key<Ks...>, false>(self()).owner(key); }

    /// The number of records of T under the parts `prefix` of their key (none: every record of T), along the layers,
    /// through the one part that resolves their key: size<Predicate<F>>(), size<Row<F>>(relation).
    template<typename T, typename... Ps>
        requires OnePartHolds<key_parts<Derived, false>, key_t<T>, false>
    [[nodiscard]] std::size_t size(urd::key<Ps...> prefix = {}) const
    { return part_of<key_t<T>, false>(self()).template size<T>(prefix); }

    /// The number of records of T under `prefix` this layer holds itself (its parents' not counted).
    template<typename T, typename... Ps>
        requires OnePartHolds<key_parts<Derived, false>, key_t<T>, false>
    [[nodiscard]] std::size_t own_size(urd::key<Ps...> prefix = {}) const
    { return part_of<key_t<T>, false>(self()).template own_size<T>(prefix); }

    /// The memory of this layer's records of T under `prefix`, in bytes: memory_usage<Row<F>>() of every pool of F,
    /// memory_usage<Row<F>>(relation) of one.
    template<typename T, typename... Ps>
        requires OnePartHolds<key_parts<Derived, false>, key_t<T>, false>
    [[nodiscard]] std::size_t memory_usage(urd::key<Ps...> prefix = {}) const
    { return part_of<key_t<T>, false>(self()).template memory_usage<T>(prefix); }

    /// Whether a key of `source` names the same record here (urd::Sees): `source` is this layer or one it lies over.
    [[nodiscard]] bool sees(const Derived& source) const noexcept
    {
        for (const Derived* layer = &static_cast<const Derived&>(*this); layer != nullptr; layer = layer->parent())
        {
            if (layer == &source)
                return true;
        }
        return false;
    }

    /// Whether every record of T that `source` can name is one this layer sees (urd::SeesAll): no layer of `source`'s
    /// chain below the first this layer sees holds one of its own (a sibling layer without its own, say).
    template<typename T>
        requires OnePartHolds<key_parts<Derived, false>, key_t<T>, false>
    [[nodiscard]] bool sees_all(const Derived& source) const
    {
        for (const Derived* layer = &source; layer != nullptr && !sees(*layer); layer = layer->parent())
        {
            if (layer->template own_size<T>() != 0)
                return false;
        }
        return true;
    }

    /// Whether a view names a record here: some layer of this repository owns its key, the same layer that owns it seen
    /// from the view's context (a view made through a layer above the owner names the owner's record too).
    template<typename T, typename C, typename K>
        requires OnePartHolds<key_parts<Derived, false>, K, false>
    [[nodiscard]] bool contains(const urd::index_view<T, C, K>& view) const
    {
        const auto* layer = owner(view.index());
        return layer != nullptr && layer == view.context().owner(view.index());
    }

    /// A key as its view, through the part that resolves it (a held part's through the layer that owns the record, at
    /// any depth); std::out_of_range where no layer holds it.
    template<typename... Ks>
        requires OnePartHolds<key_parts<Derived, false>, urd::key<Ks...>, false>
    [[nodiscard]] urd::index_view<kind_of_key<urd::key<Ks...>>, Owner<kind_of_key<urd::key<Ks...>>>, urd::key<Ks...>> view(urd::key<Ks...> key) const
    { return part_of<urd::key<Ks...>, false>(self()).view(key); }

    /// A record held outside the repository, as its view (the record must outlive the view; never a temporary): through
    /// this repository where one of its own parts holds its kind; else as the repository nested in it that holds the
    /// kind views it (urd::resolve: at any depth).
    template<typename T>
        requires OnePartHolds<key_parts<Derived, false>, key_t<T>, false>
    [[nodiscard]] urd::borrowed_view<T, Owner<T>> view(const urd::data<T>& record URD_LIFETIMEBOUND) const
    {
        if constexpr (OwnsKey<Derived, key_t<T>>)
            return urd::borrowed_view<T, Derived>(&record, &self());
        else
            return urd::resolve(record, part_of<key_t<T>, false>(self()));
    }

    template<typename T>
    void view(const urd::data<T>&& record) const = delete;

    /// A builder as its view through its own repository C (this one, or one a part reaches, at any depth: urd::resolve),
    /// which points at it (the builder must outlive the view; never a temporary).
    template<typename T, typename C>
        requires OnePartHolds<key_parts<Derived, false>, key_t<T>, false>
    [[nodiscard]] urd::borrowed_view<T, C, urd::builder<T, C>> view(const urd::builder<T, C>& built URD_LIFETIMEBOUND) const
    {
        if constexpr (std::same_as<C, Derived>)
            return urd::borrowed_view<T, Derived, urd::builder<T, Derived>>(&built, &self());
        else
            return urd::resolve(built, part_of<key_t<T>, false>(self()));
    }

    template<typename T, typename C>
    void view(const urd::builder<T, C>&& built) const = delete;

    /// The repository that resolves keys K itself (Owner of their kind): this one, or one it nests, at any depth (whose
    /// sees() urd::copy asks: urd::ResolvesThrough).
    template<typename K>
        requires OnePartHolds<key_parts<Derived, false>, K, false>
    [[nodiscard]] const Owner<kind_of_key<K>>& resolving() const
    { return part_of<K, false>(self()).template resolving<K>(); }

    /// Interns a builder through the scratch records of a builder storage (its scratch<T>(), each worker's own; no
    /// allocation once they have grown): one of its own, its built children and records first (each prepared, as the
    /// repository's domain makes it canonical, and inserted), interned children kept as they are, then its record,
    /// written into a scratch record, prepared and inserted; one of a part's kinds, there. The view of its record through
    /// its owner, and whether it was new. A child interned in another repository of this kind, or a child of another
    /// repository not interned there, throws std::invalid_argument.
    template<typename T, typename C, IsBuilderStorage Storage>
        requires InternsBuilder<Derived, T, C, Storage>
    std::pair<urd::index_view<T, Owner<T>, key_t<T>>, bool> insert(const urd::builder<T, C>& built, Storage& storage)
    {
        if constexpr (std::same_as<C, Derived>)
            return detail::insert_child(static_cast<Derived&>(*this), storage, built);
        else
            return part_of<key_t<T>, true>(self()).insert(built, storage);
    }

    /// Interns a builder through the part that holds its kind, where that part's store interns builders itself
    /// (HeldPart::insert_built), with the hints it takes: what insert(builder, storage) gives.
    template<typename T, typename C, typename... Hints>
        requires requires(typename detail::key_part<Derived, key_t<T>, true>::type part, const urd::builder<T, C>& built, const Hints&... hints) {
            part.insert_built(built, hints...);
        }
    std::pair<urd::index_view<T, Owner<T>, key_t<T>>, bool> insert_built(const urd::builder<T, C>& built, const Hints&... hints)
    { return part_of<key_t<T>, true>(self()).insert_built(built, hints...); }

    /// Finds the record a builder writes through the part that holds its kind, where that part's store finds builders
    /// itself (HeldPart::find_built), with the hints it takes.
    template<typename T, typename C, typename... Hints>
        requires requires(typename detail::key_part<Derived, key_t<T>, false>::type part, const urd::builder<T, C>& built, const Hints&... hints) {
            part.find_built(built, hints...);
        }
    [[nodiscard]] std::optional<urd::index_view<T, Owner<T>, key_t<T>>> find_built(const urd::builder<T, C>& built, const Hints&... hints) const
    { return part_of<key_t<T>, false>(self()).find_built(built, hints...); }

    /// Finds the record a builder writes as find(builder, storage) does, with hints: each child whose part finds builders
    /// itself (find_built) gets the hints that part takes.
    template<typename T, typename C, IsBuilderStorage Storage, typename Hint, typename... Hints>
        requires std::same_as<C, Derived>
    std::optional<urd::index_view<T, Owner<T>, key_t<T>>> find(const urd::builder<T, C>& built, Storage& storage, const Hint& hint, const Hints&... hints)
    { return detail::find_child(static_cast<Derived&>(*this), storage, built, hint, hints...); }

    /// Reads the stored record of a key into a builder through the part that holds it, where that part's store unpacks
    /// records itself (HeldPart::unpack_built).
    template<typename T, typename C, typename... Ks>
        requires requires(typename detail::key_part<Derived, urd::key<Ks...>, false>::type part, urd::key<Ks...> key, urd::builder<T, C>& built) {
            part.unpack_built(key, built);
        }
    void unpack_built(urd::key<Ks...> key, urd::builder<T, C>& built) const
    { part_of<urd::key<Ks...>, false>(self()).unpack_built(key, built); }

    /// Interns a builder as insert(builder, storage) does, with hints: each child whose part interns builders itself
    /// (insert_built) gets the hints that part takes (a state's storages, its parent and its changes, say); the others
    /// are interned as they are.
    template<typename T, typename C, IsBuilderStorage Storage, typename Hint, typename... Hints>
        requires std::same_as<C, Derived>
    std::pair<urd::index_view<T, Owner<T>, key_t<T>>, bool> insert(const urd::builder<T, C>& built, Storage& storage, const Hint& hint, const Hints&... hints)
    { return detail::insert_child(static_cast<Derived&>(*this), storage, built, hint, hints...); }

    /// Finds the record a builder writes, inserting nothing: its children looked up the same way (a child not found
    /// names no record here, so then nothing is found; a record of its own is prepared in place), then its record.
    template<typename T, typename C, IsBuilderStorage Storage>
        requires FindsBuilderOf<Derived, T, C, Storage>
    std::optional<urd::index_view<T, Owner<T>, key_t<T>>> find(const urd::builder<T, C>& built, Storage& storage)
    {
        if constexpr (std::same_as<C, Derived>)
            return detail::find_child(static_cast<Derived&>(*this), storage, built);
        else
            return part_of<key_t<T>, true>(self()).find(built, storage);
    }

    /// Interns a view of a record of its kinds as it reads it (what a child resolves to): an interned one is checked
    /// to be its own, a builder's is interned as that builder.
    template<ViewThrough<Derived> V, IsBuilderStorage Storage>
    std::pair<urd::index_view<typename urd::view_kind<V>::type, Owner<typename urd::view_kind<V>::type>, key_t<typename urd::view_kind<V>::type>>, bool>
    insert(const V& view, Storage& storage)
    { return detail::insert_view(static_cast<Derived&>(*this), storage, view); }

    /// Finds the record a view reads, inserting nothing, likewise.
    template<ViewThrough<Derived> V, IsBuilderStorage Storage>
    std::optional<urd::index_view<typename urd::view_kind<V>::type, Owner<typename urd::view_kind<V>::type>, key_t<typename urd::view_kind<V>::type>>>
    find(const V& view, Storage& storage)
    { return detail::find_view(static_cast<Derived&>(*this), storage, view); }

    /// Stores a record where no layer holds it, through the one part that holds its kind and is written into (chosen at
    /// compile time, as for reading: one of its own stores, or a repository it nests non-const, at any depth), prepared
    /// there and stored under the parts of its key it holds (HeldPart::insert): its view through its owner, and whether
    /// it was stored now.
    template<typename T>
        requires OnePartHolds<key_parts<Derived, true>, key_t<T>, true>
    std::pair<urd::index_view<T, Owner<T>, key_t<T>>, bool> insert(urd::data<T>& record)
    { return part_of<key_t<T>, true>(self()).insert(record); }

    /// The view of the record of a record's content, where some layer holds it, through the same part as reading,
    /// prepared there first as for inserting (HeldPart::find).
    template<typename T>
        requires OnePartHolds<key_parts<Derived, false>, key_t<T>, false>
    [[nodiscard]] std::optional<urd::index_view<T, Owner<T>, key_t<T>>> find(urd::data<T>& record) const
    { return part_of<key_t<T>, false>(self()).find(record); }

private:
    [[nodiscard]] const Derived& self() const noexcept { return static_cast<const Derived&>(*this); }

    [[nodiscard]] Derived& self() noexcept { return static_cast<Derived&>(*this); }

    /// The one part of `repository` (Derived, as read: const; as written into) that holds keys K: of its own parts and
    /// the repositories it nests, in key_parts' order.
    template<typename K, bool Writable, typename R>
    [[nodiscard]] static typename detail::key_part<Derived, K, Writable>::type part_of(R& repository)
    { return std::get<part_holding<key_parts<Derived, Writable>, K, Writable>()>(std::tuple_cat(repository.own_parts(), nested_parts<Writable>(repository))); }
};

}  // namespace ygg::formalism

#endif
