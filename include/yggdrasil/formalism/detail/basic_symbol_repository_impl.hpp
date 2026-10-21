/*
 * Copyright (C) 2026 Dominik Drexler
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

#ifndef YGG_FORMALISM_DETAIL_BASIC_SYMBOL_REPOSITORY_IMPL_HPP_
#define YGG_FORMALISM_DETAIL_BASIC_SYMBOL_REPOSITORY_IMPL_HPP_

#include "yggdrasil/buffer/detail/indexed_hash_set_impl.hpp"
#include "yggdrasil/containers/detail/indexed_hash_set_impl.hpp"
#include "yggdrasil/formalism/basic_symbol_repository.hpp"

#include <cassert>
#include <utility>

namespace ygg::formalism
{

template<typename T, bool ThreadSafe>
BasicSymbolRepository<T, ThreadSafe>::BasicSymbolRepository(const BasicSymbolRepository* parent) : m_parent(parent), m_slot()
{ clear_slot(); }

template<typename T, bool ThreadSafe>
BasicSymbolRepository<T, ThreadSafe>::~BasicSymbolRepository() = default;

template<typename T, bool ThreadSafe>
void BasicSymbolRepository<T, ThreadSafe>::clear() noexcept
{ clear_slot(); }

template<typename T, bool ThreadSafe>
size_t BasicSymbolRepository<T, ThreadSafe>::memory_usage() const noexcept
{ return m_slot.memory_usage(); }

template<typename T, bool ThreadSafe>
std::optional<Index<T>> BasicSymbolRepository<T, ThreadSafe>::find_local_with_hash(const Data<T>& builder, size_t h) const noexcept
{
    const auto& container = m_slot.container;
    assert(h == container.hash(builder));

    if (auto index_or_nullopt = container.find_with_hash(builder, h))
        return Index<T>(m_slot.parent_size + ygg::uint_t(*index_or_nullopt));

    return std::nullopt;
}

template<typename T, bool ThreadSafe>
std::optional<Index<T>> BasicSymbolRepository<T, ThreadSafe>::find_local_unsafe_with_hash(const Data<T>& builder, size_t h) const noexcept
{
    const auto& container = m_slot.container;
    assert(h == container.hash(builder));

    if (auto index_or_nullopt = container.find_unsafe_with_hash(builder, h))
        return Index<T>(m_slot.parent_size + ygg::uint_t(*index_or_nullopt));

    return std::nullopt;
}

template<typename T, bool ThreadSafe>
std::pair<Index<T>, bool> BasicSymbolRepository<T, ThreadSafe>::insert_local(Data<T>& builder)
{
    return insert_local_with_hash(builder, BasicSymbolRepository::hash(builder));
}

template<typename T, bool ThreadSafe>
std::pair<Index<T>, bool> BasicSymbolRepository<T, ThreadSafe>::insert_local_with_hash(Data<T>& builder, size_t h)
{
    if (const auto index = find_local_with_hash(builder, h))
    {
        builder.index = *index;
        return { *index, false };
    }

    return create_local_with_hash(builder, h);
}

template<typename T, bool ThreadSafe>
std::pair<Index<T>, bool> BasicSymbolRepository<T, ThreadSafe>::create_local_with_hash(Data<T>& builder, size_t h)
{
    auto& container = m_slot.container;
    const auto [index, created] = container.complete_miss_with_hash(h,
                                                                    builder,
                                                                    [&](Index<T> local_index) -> const Data<T>&
                                                                    {
                                                                        builder.index.value = m_slot.parent_size + ygg::uint_t(local_index);
                                                                        assert(h == container.hash(builder) && "Symbol index must not affect identity.");
                                                                        return builder;
                                                                    });
    builder.index.value = m_slot.parent_size + ygg::uint_t(index);
    return { builder.index, created };
}

}  // namespace ygg::formalism

#endif
