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
    return m_owner->view(key);
}

template<typename O, typename S, typename Keys>
template<typename T>
std::pair<urd::index_view<T, std::remove_const_t<O>, key_t<T>>, bool> HeldPart<O, S, Keys>::insert(urd::data<T>& record) const
{
    prepare_for_insert(*m_owner, record);
    const auto [key, inserted] = urd::insert_at(*m_store, detail::known(record), record);
    return { m_owner->view(key), inserted };
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
    const std::remove_const_t<O>* found = nullptr;
    for (const std::remove_const_t<O>* layer = m_owner; layer != nullptr && layer->contains(key); layer = layer->parent())
        found = layer;
    return found;
}

}  // namespace ygg::formalism

#endif
