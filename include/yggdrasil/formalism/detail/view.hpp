/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_FORMALISM_DETAIL_VIEW_HPP_
#define YGG_FORMALISM_DETAIL_VIEW_HPP_

#include "yggdrasil/formalism/declarations.hpp"

#include <tuple>

namespace ygg::formalism::detail
{

template<typename Handle, typename C>
class View;

/// Shared storage and access for a symbol's published index.
template<typename T, SymbolContextFor<T> C>
class View<Index<T>, C>
{
protected:
    Index<T> m_handle;
    const C* m_context;

public:
    View(Index<T> handle, const C& context) noexcept : m_handle(handle), m_context(&context) {}

    const Data<T>& get_data() const noexcept(noexcept(get_repository(*m_context)[m_handle])) { return get_repository(*m_context)[m_handle]; }
    const C& get_context() const noexcept { return *m_context; }
    const Index<T>& get_handle() const noexcept { return m_handle; }
    Index<T> get_index() const noexcept { return m_handle; }
    auto identifying_members() const noexcept { return std::make_tuple(m_handle, get_repository(*m_context).get_index()); }
};

}  // namespace ygg::formalism::detail

#endif
