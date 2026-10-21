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
#include "yggdrasil/formalism/basic_symbol_repository.hpp"

#include <cassert>
#include <utility>

namespace ygg::formalism
{

/// Inserting serialized symbols needs cista/serialization.h, so only instantiating sources include this definition.
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
