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

#include <cassert>
#include <cista/serialization.h>
#include <limits>
#include <stdexcept>
#include <utility>

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

template<typename Tag, HashFor<Data<Tag>> H, EqualToFor<Data<Tag>> E, size_t FirstSegmentSize, bool ThreadSafe>
template<::cista::mode Mode, typename Factory>
Index<Tag> IndexedHashSet<Tag, H, E, FirstSegmentSize, ThreadSafe>::append_new_with_index(Factory&& factory)
{
    if (!m_buf || !m_arena)
        throw std::logic_error("Buffer IndexedHashSet requires a buffer and "
                               "arena before insertion.");

    return Index<Tag>(static_cast<uint_t>(m_storage->emplace_back_with_index(
        [&](size_t index) { return serialize<Mode>(std::forward<Factory>(factory)(Index<Tag>(static_cast<uint_t>(index)))); },
        std::numeric_limits<uint_t>::max())));
}

template<typename Tag, HashFor<Data<Tag>> H, EqualToFor<Data<Tag>> E, size_t FirstSegmentSize, bool ThreadSafe>
template<::cista::mode Mode>
Index<Tag> IndexedHashSet<Tag, H, E, FirstSegmentSize, ThreadSafe>::append_new(const Data<Tag>& element)
{
    return append_new_with_index<Mode>([&](Index<Tag>) -> const Data<Tag>& { return element; });
}

template<typename Tag, HashFor<Data<Tag>> H, EqualToFor<Data<Tag>> E, size_t FirstSegmentSize, bool ThreadSafe>
template<::cista::mode Mode>
std::pair<Index<Tag>, bool> IndexedHashSet<Tag, H, E, FirstSegmentSize, ThreadSafe>::insert_with_hash(size_t h, const Data<Tag>& element)
{
    assert(h == IndexedHashSet::hash(element) && "The given hash does not match container internal's hash.");
    assert(h == m_set.hash(element));

    return ygg::detail::find_or_lazy_insert_value_with_hash<ThreadSafe>(m_set, element, h, [&] { return append_new<Mode>(element); });
}

template<typename Tag, HashFor<Data<Tag>> H, EqualToFor<Data<Tag>> E, size_t FirstSegmentSize, bool ThreadSafe>
template<::cista::mode Mode>
std::pair<Index<Tag>, bool> IndexedHashSet<Tag, H, E, FirstSegmentSize, ThreadSafe>::complete_miss_with_hash(size_t h, const Data<Tag>& element)
{
    assert(h == IndexedHashSet::hash(element) && "The given hash does not match container internal's hash.");
    assert(h == m_set.hash(element));

    return ygg::detail::complete_miss_value_with_hash<ThreadSafe>(m_set, element, h, [&] { return append_new<Mode>(element); });
}

template<typename Tag, HashFor<Data<Tag>> H, EqualToFor<Data<Tag>> E, size_t FirstSegmentSize, bool ThreadSafe>
template<::cista::mode Mode, typename Factory>
std::pair<Index<Tag>, bool> IndexedHashSet<Tag, H, E, FirstSegmentSize, ThreadSafe>::complete_miss_with_hash(size_t h, const Data<Tag>& element, Factory&& factory)
{
    assert(h == IndexedHashSet::hash(element) && "The given hash does not match container internal's hash.");
    assert(h == m_set.hash(element));

    return ygg::detail::complete_miss_value_with_hash<ThreadSafe>(m_set,
                                                                  element,
                                                                  h,
                                                                  [&] { return append_new_with_index<Mode>(std::forward<Factory>(factory)); });
}

template<typename Tag, HashFor<Data<Tag>> H, EqualToFor<Data<Tag>> E, size_t FirstSegmentSize, bool ThreadSafe>
template<::cista::mode Mode>
Index<Tag> IndexedHashSet<Tag, H, E, FirstSegmentSize, ThreadSafe>::insert_new_with_hash(size_t h, const Data<Tag>& element)
{
    const auto [index, inserted] = complete_miss_with_hash<Mode>(h, element);
    if (!inserted)
        throw std::logic_error("buffer::IndexedHashSet::insert_new_with_hash requires an absent key.");
    return index;
}

template<typename Tag, HashFor<Data<Tag>> H, EqualToFor<Data<Tag>> E, size_t FirstSegmentSize, bool ThreadSafe>
template<::cista::mode Mode>
std::pair<Index<Tag>, bool> IndexedHashSet<Tag, H, E, FirstSegmentSize, ThreadSafe>::insert(const Data<Tag>& element)
{
    return insert_with_hash<Mode>(IndexedHashSet::hash(element), element);
}

}  // namespace ygg::buffer

#endif
