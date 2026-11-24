/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_FORMALISM_KEYED_IMPL_HPP_
#define YGG_FORMALISM_KEYED_IMPL_HPP_

// The definitions of a held part's find and insert (keyed.hpp; its key reads: keyed_read_impl.hpp): included only by
// the sources that instantiate a library's repositories (repository_impl.hpp, symbol_context_impl.hpp), so that a
// find or insert runs the prepare_for_insert of its domain that those sources see, and no other source instantiates it with
// another (template rules T2, T11).

#include "yggdrasil/formalism/keyed_read_impl.hpp"

#include <cassert>
#include <cstddef>
#include <type_traits>
#include <urd/elements/keyed.hpp>
#include <urd/memory.hpp>

namespace ygg::formalism
{

template<typename O, typename S, typename Keys>
template<typename T>
std::optional<urd::index_view<T, std::remove_const_t<O>, key_t<T>>> HeldPart<O, S, Keys>::find(urd::data<T>& record) const
{
    prepare_for_insert(*m_owner, record);
    const auto key = urd::find_at(*m_store, detail::known(record), record);
    if (key.is_none())
        return std::nullopt;
    return urd::index_view<T, std::remove_const_t<O>, key_t<T>>(key, held_owner(key));
}

template<typename O, typename S, typename Keys>
template<typename T>
std::pair<urd::index_view<T, std::remove_const_t<O>, key_t<T>>, bool> HeldPart<O, S, Keys>::insert(urd::data<T>& record) const
{
    prepare_for_insert(*m_owner, record);
    const auto [key, inserted] = urd::insert_at(*m_store, detail::known(record), record);
    // A record stored now is this layer's own.
    const auto* owner = inserted ? static_cast<const std::remove_const_t<O>*>(m_owner) : held_owner(key);
    return { urd::index_view<T, std::remove_const_t<O>, key_t<T>>(key, owner), inserted };
}

namespace detail
{
/// What urd::count_at counts of the store that holds the records: their number along the layers; those of one layer
/// (its parents' below its base); their memory in this layer. The last two add up over a layer's own containers.
struct CountRecords
{
    static constexpr bool per_layer = false;

    template<typename S>
    [[nodiscard]] std::size_t records(const S& store) const
    { return store.size(); }
};

struct CountOwnRecords
{
    static constexpr bool per_layer = true;

    template<typename S>
    [[nodiscard]] std::size_t records(const S& store) const
    { return store.size() - store.base(); }
};

struct CountMemory
{
    static constexpr bool per_layer = true;

    template<typename S>
    [[nodiscard]] std::size_t records(const S& store) const
    { return urd::memory_usage(store); }
};
}  // namespace detail

template<typename O, typename S, typename Keys>
template<typename T, typename... Ps>
std::size_t HeldPart<O, S, Keys>::size(urd::key<Ps...> prefix) const
{ return urd::count_at<key_t<T>>(*m_store, prefix, detail::CountRecords {}); }

template<typename O, typename S, typename Keys>
template<typename T, typename... Ps>
std::size_t HeldPart<O, S, Keys>::own_size(urd::key<Ps...> prefix) const
{ return urd::count_at<key_t<T>>(*m_store, prefix, detail::CountOwnRecords {}); }

template<typename O, typename S, typename Keys>
template<typename T, typename... Ps>
std::size_t HeldPart<O, S, Keys>::memory_usage(urd::key<Ps...> prefix) const
{ return urd::count_at<key_t<T>>(*m_store, prefix, detail::CountMemory {}); }

template<typename O, typename S, typename Keys>
template<typename... Ks>
bool HeldPart<O, S, Keys>::contains(urd::key<Ks...> key) const
{ return urd::contains_key(*m_store, key); }

template<typename O, typename S, typename Keys>
template<typename... Ks>
const std::remove_const_t<O>* HeldPart<O, S, Keys>::owner(urd::key<Ks...> key) const
{
    if (!contains(key))
        return nullptr;
    return held_owner(key);
}

template<typename O, typename S, typename Keys>
template<typename... Ks>
bool HeldPart<O, S, Keys>::owns(urd::key<Ks...> key) const
{ return urd::owns_key(*m_store, key); }

template<typename O, typename S, typename Keys>
template<typename... Ks>
const std::remove_const_t<O>* HeldPart<O, S, Keys>::held_owner(urd::key<Ks...> key) const
{
    const std::remove_const_t<O>* layer = m_owner;
    while (!layer->owns(key))
    {
        layer = layer->parent();
        assert(layer != nullptr && "ygg::formalism::HeldPart: no layer holds the key");
    }
    return layer;
}

}  // namespace ygg::formalism

#endif
