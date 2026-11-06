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
std::optional<urd::index<T>> BasicSymbolRepository<T, ThreadSafe>::find_local_with_hash(const urd::data<T>& builder, size_t h) const noexcept
{
    assert(h == Slot::hash(builder));
    if (const auto found = m_slot.records.find(urd::prehashed<urd::data<T>> { builder, h }); !found.is_none())
        return urd::index<T>(static_cast<uint_t>(m_slot.parent_size + found.value));
    return std::nullopt;
}

template<typename T, bool ThreadSafe>
std::pair<urd::index<T>, bool> BasicSymbolRepository<T, ThreadSafe>::insert_local(urd::data<T>& builder)
{ return insert_local_with_hash(builder, BasicSymbolRepository::hash(builder)); }

template<typename T, bool ThreadSafe>
std::pair<urd::index<T>, bool> BasicSymbolRepository<T, ThreadSafe>::insert_local_with_hash(urd::data<T>& builder, size_t h)
{
    if (const auto index = find_local_with_hash(builder, h))
    {
        builder.index = *index;
        return { *index, false };
    }

    return create_local_with_hash(builder, h);
}

template<typename T, bool ThreadSafe>
std::pair<urd::index<T>, bool> BasicSymbolRepository<T, ThreadSafe>::create_local_with_hash(urd::data<T>& builder, size_t h)
{
    const auto [index, created] = m_slot.records.insert(urd::prehashed<urd::data<T>> { builder, h },
                                                        [&](urd::index<urd::data<T>> local_index) -> const urd::data<T>&
                                                        {
                                                            builder.index.value = static_cast<uint_t>(m_slot.parent_size + local_index.value);
                                                            assert(h == Slot::hash(builder) && "Symbol index must not affect identity.");
                                                            return builder;
                                                        });
    builder.index.value = static_cast<uint_t>(m_slot.parent_size + index.value);
    return { builder.index, created };
}

}  // namespace ygg::formalism

#endif
