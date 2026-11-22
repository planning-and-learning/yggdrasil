/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_FORMALISM_KEYED_IMPL_HPP_
#define YGG_FORMALISM_KEYED_IMPL_HPP_

// The definitions of a held part's lookups (keyed.hpp): included where its store is complete, by the sources that
// instantiate a library's repositories (repository_impl.hpp, symbol_context_impl.hpp).

#include "yggdrasil/formalism/keyed.hpp"

#include <stdexcept>
#include <urd/elements/keyed.hpp>

namespace ygg::formalism
{

template<typename O, typename S, typename Keys>
template<typename... Ks>
urd::stored_ref<kind_of_key<urd::key<Ks...>>> HeldPart<O, S, Keys>::operator[](urd::key<Ks...> key) const
{ return urd::record_at(*m_store, key); }

template<typename O, typename S, typename Keys>
template<typename... Ks>
urd::stored_ref<kind_of_key<urd::key<Ks...>>> HeldPart<O, S, Keys>::at(urd::key<Ks...> key) const
{
    if (!urd::contains_key(*m_store, key))
        throw std::out_of_range("ygg::formalism: no layer holds the key.");
    return urd::record_at(*m_store, key);
}

template<typename O, typename S, typename Keys>
template<typename T>
std::optional<urd::index_view<T, std::remove_const_t<O>, key_t<T>>> HeldPart<O, S, Keys>::find(const urd::data<T>& record) const
{
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

}  // namespace ygg::formalism

#endif
