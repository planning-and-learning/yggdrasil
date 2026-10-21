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

#ifndef YGG_BUFFER_DETAIL_INDEXED_HASH_SET_IMPL_HPP_
#define YGG_BUFFER_DETAIL_INDEXED_HASH_SET_IMPL_HPP_

#include "yggdrasil/buffer/indexed_hash_set.hpp"

#include <cista/serialization.h>

namespace ygg::buffer
{

template<typename Tag, HashFor<Data<Tag>> H, EqualToFor<Data<Tag>> E, size_t FirstSegmentSize, bool ThreadSafe>
template<::cista::mode Mode>
const Data<Tag>* IndexedHashSet<Tag, H, E, FirstSegmentSize, ThreadSafe>::serialize(const Data<Tag>& element)
{
    m_buf->reset();
    ::cista::serialize<Mode>(*m_buf, element);
    auto begin = m_arena->write(m_buf->base(), m_buf->size(), alignof(Data<Tag>));
    return ::cista::deserialize<const Data<Tag>, Mode>(begin, begin + m_buf->size());
}

}  // namespace ygg::buffer

#endif
