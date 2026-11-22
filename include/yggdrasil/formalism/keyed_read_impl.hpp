/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_FORMALISM_KEYED_READ_IMPL_HPP_
#define YGG_FORMALISM_KEYED_READ_IMPL_HPP_

// The definitions of a held part's key reads (keyed.hpp): included where its store is complete (symbol_repository.hpp),
// so that reads inline. Its value lookup and insert are in keyed_impl.hpp, which only instantiating sources include.

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

}  // namespace ygg::formalism

#endif
