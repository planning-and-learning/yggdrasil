/*
 * Copyright (C) 2025-2026 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef YGG_FORMALISM_SYMBOL_REPOSITORY_HPP_
#define YGG_FORMALISM_SYMBOL_REPOSITORY_HPP_

#include "yggdrasil/core/family.hpp"
#include "yggdrasil/formalism/declarations.hpp"
#include "yggdrasil/formalism/keyed_read_impl.hpp"

#include <boost/mp11.hpp>
#include <optional>
#include <tuple>
#include <type_traits>
#include <urd/config.hpp>
#include <urd/elements/flat.hpp>
#include <urd/memory.hpp>
#include <urd/repr/records/arena.hpp>
#include <urd/repr/records/records.hpp>
#include <urd/repr/wrappers/layered_pool.hpp>
#include <urd/store.hpp>
#include <utility>

namespace ygg::formalism
{

/// The records of type T in one layer: a urd pool of urd::data<T>, each once, handing out index<T>, stored flat with
/// references that hold while others are inserted (views borrow them); layered over a parent layer's pool, its indices
/// continuing after the parent's, its lookups finding the parent's records too.
template<typename T, bool ThreadSafe = false>
using SymbolStore = urd::Store<urd::pool<urd::data<T>, urd::index<T>>,
                               urd::flat,
                               urd::stable_refs,
                               urd::layered,
                               std::conditional_t<ThreadSafe, urd::concurrent, urd::single_threaded>>;

/// The store of a kind's records in a sequential or a concurrent repository, as a template of the kind alone (a
/// Family's member template takes one parameter).
template<bool ThreadSafe>
struct SymbolStoreOf
{
    template<typename T>
    using fn = SymbolStore<T, ThreadSafe>;
};

/// The symbols of a repository layer, a plain store: one SymbolStore per kind (a urd family: get<T>() selects the kind's
/// store, the first step of urd's key fold), each layered over the parent layer's, so that lookups and reads find the
/// parent's records too. A repository holds it as its symbol part (HeldPart): keys are resolved, counted, viewed,
/// stored and found by the repository's one keyed dispatch.
template<bool ThreadSafe, typename... Ts>
class SymbolStores : public Family<SymbolStoreOf<ThreadSafe>::template fn, Ts...>
{
    using Base = Family<SymbolStoreOf<ThreadSafe>::template fn, Ts...>;

public:
    static constexpr bool thread_safe = ThreadSafe;

    /// Parent layers must remain frozen and outlive this one.
    explicit SymbolStores(const SymbolStores* parent = nullptr) : Base(parent) {}
};

}  // namespace ygg::formalism

#endif
