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

#ifndef YGG_CONTAINERS_DETAIL_INDEXED_HASH_SET_IMPL_HPP_
#define YGG_CONTAINERS_DETAIL_INDEXED_HASH_SET_IMPL_HPP_

#include "yggdrasil/containers/detail/concurrency.hpp"
#include "yggdrasil/containers/indexed_hash_set.hpp"
#include "yggdrasil/containers/segmented_vector.hpp"

#include <cassert>
#include <limits>
#include <stdexcept>

namespace ygg
{

/// Owns the elements and maps values to indices. It stays at one address, so the owner's rows and the functors'
/// back pointers survive moves of the set.
template<typename Tag, HashFor<Data<Tag>> H, EqualToFor<Data<Tag>> E, size_t FirstSegmentSize, bool ThreadSafe>
class IndexedHashSet<Tag, H, E, FirstSegmentSize, ThreadSafe>::Store
{
public:
    class IndexableHash
    {
    private:
        const Store* m_store;
        H m_hash;

    public:
        using is_transparent = void;

        IndexableHash() noexcept(std::is_nothrow_default_constructible_v<H>) : m_store(nullptr) {}
        explicit IndexableHash(const Store& store) noexcept(std::is_nothrow_default_constructible_v<H>) : m_store(&store) {}

        size_t operator()(Index<Tag> index) const noexcept(std::is_nothrow_invocable_v<const H&, const Data<Tag>&>)
        { return m_hash(m_store->storage[uint_t(index)]); }
        size_t operator()(const Data<Tag>& element) const noexcept(std::is_nothrow_invocable_v<const H&, const Data<Tag>&>) { return m_hash(element); }
    };

    class IndexableEqualTo
    {
    private:
        const Store* m_store;
        E m_equal_to;

    public:
        using is_transparent = void;

        IndexableEqualTo() noexcept(std::is_nothrow_default_constructible_v<E>) : m_store(nullptr), m_equal_to() {}
        explicit IndexableEqualTo(const Store& store) noexcept(std::is_nothrow_default_constructible_v<E>) : m_store(&store), m_equal_to() {}

        bool operator()(Index<Tag> lhs, Index<Tag> rhs) const noexcept(std::is_nothrow_invocable_v<const E&, const Data<Tag>&, const Data<Tag>&>)
        { return m_equal_to(m_store->storage[uint_t(lhs)], m_store->storage[uint_t(rhs)]); }
        bool operator()(const Data<Tag>& lhs, Index<Tag> rhs) const noexcept(std::is_nothrow_invocable_v<const E&, const Data<Tag>&, const Data<Tag>&>)
        { return m_equal_to(lhs, m_store->storage[uint_t(rhs)]); }
        bool operator()(Index<Tag> lhs, const Data<Tag>& rhs) const noexcept(std::is_nothrow_invocable_v<const E&, const Data<Tag>&, const Data<Tag>&>)
        { return m_equal_to(m_store->storage[uint_t(lhs)], rhs); }
        bool operator()(const Data<Tag>& lhs, const Data<Tag>& rhs) const noexcept(std::is_nothrow_invocable_v<const E&, const Data<Tag>&, const Data<Tag>&>)
        { return m_equal_to(lhs, rhs); }
    };

    using SetType = detail::HashSetType<Index<Tag>, IndexableHash, IndexableEqualTo, ThreadSafe>;

    Store() : storage(), set(0, IndexableHash(*this), IndexableEqualTo(*this)) {}
    Store(const Store&) = delete;
    ~Store() = default;
    Store& operator=(const Store&) = delete;

    SegmentedVector<Data<Tag>, FirstSegmentSize, ThreadSafe, uint_t> storage;
    SetType set;
};

template<typename Tag, HashFor<Data<Tag>> H, EqualToFor<Data<Tag>> E, size_t FirstSegmentSize, bool ThreadSafe>
IndexedHashSet<Tag, H, E, FirstSegmentSize, ThreadSafe>::IndexedHashSet() : m_store(new Store()), m_rows(m_store->storage.rows())
{
}

template<typename Tag, HashFor<Data<Tag>> H, EqualToFor<Data<Tag>> E, size_t FirstSegmentSize, bool ThreadSafe>
IndexedHashSet<Tag, H, E, FirstSegmentSize, ThreadSafe>::IndexedHashSet(IndexedHashSet&& other) noexcept :
    m_store(std::exchange(other.m_store, nullptr)),
    m_rows(std::exchange(other.m_rows, Rows()))
{
}

template<typename Tag, HashFor<Data<Tag>> H, EqualToFor<Data<Tag>> E, size_t FirstSegmentSize, bool ThreadSafe>
auto IndexedHashSet<Tag, H, E, FirstSegmentSize, ThreadSafe>::operator=(IndexedHashSet&& other) noexcept -> IndexedHashSet&
{
    if (this == &other)
        return *this;
    delete m_store;
    m_store = std::exchange(other.m_store, nullptr);
    m_rows = std::exchange(other.m_rows, Rows());
    return *this;
}

template<typename Tag, HashFor<Data<Tag>> H, EqualToFor<Data<Tag>> E, size_t FirstSegmentSize, bool ThreadSafe>
IndexedHashSet<Tag, H, E, FirstSegmentSize, ThreadSafe>::~IndexedHashSet()
{
    delete m_store;
}

template<typename Tag, HashFor<Data<Tag>> H, EqualToFor<Data<Tag>> E, size_t FirstSegmentSize, bool ThreadSafe>
void IndexedHashSet<Tag, H, E, FirstSegmentSize, ThreadSafe>::clear() noexcept
{
    if (!m_store)
        return;
    if (!m_store->storage.empty())
        m_store->set.clear();
    m_store->storage.clear();
}

template<typename Tag, HashFor<Data<Tag>> H, EqualToFor<Data<Tag>> E, size_t FirstSegmentSize, bool ThreadSafe>
template<typename Factory>
Index<Tag> IndexedHashSet<Tag, H, E, FirstSegmentSize, ThreadSafe>::append_new_with_index(Factory&& factory)
{
    return Index<Tag>(static_cast<uint_t>(m_store->storage.emplace_back_with_index([&](size_t index) -> decltype(auto)
                                                                            { return std::forward<Factory>(factory)(Index<Tag>(static_cast<uint_t>(index))); },
                                                                            std::numeric_limits<uint_t>::max())));
}

template<typename Tag, HashFor<Data<Tag>> H, EqualToFor<Data<Tag>> E, size_t FirstSegmentSize, bool ThreadSafe>
Index<Tag> IndexedHashSet<Tag, H, E, FirstSegmentSize, ThreadSafe>::append_new(const Data<Tag>& element)
{
    return append_new_with_index([&](Index<Tag>) -> const Data<Tag>& { return element; });
}

template<typename Tag, HashFor<Data<Tag>> H, EqualToFor<Data<Tag>> E, size_t FirstSegmentSize, bool ThreadSafe>
std::optional<Index<Tag>> IndexedHashSet<Tag, H, E, FirstSegmentSize, ThreadSafe>::find_with_hash(const Data<Tag>& element, size_t h) const
{
    assert(h == IndexedHashSet::hash(element) && "The given hash does not match container internal's hash.");
    assert(h == m_store->set.hash(element));

    return detail::find_value_with_hash<ThreadSafe>(m_store->set, element, h);
}

template<typename Tag, HashFor<Data<Tag>> H, EqualToFor<Data<Tag>> E, size_t FirstSegmentSize, bool ThreadSafe>
std::optional<Index<Tag>> IndexedHashSet<Tag, H, E, FirstSegmentSize, ThreadSafe>::find_unsafe_with_hash(const Data<Tag>& element, size_t h) const
{
    assert(h == IndexedHashSet::hash(element) && "The given hash does not match container internal's hash.");
    assert(h == m_store->set.hash(element));

    return detail::find_value_unsafe_with_hash<ThreadSafe>(m_store->set, element, h);
}

template<typename Tag, HashFor<Data<Tag>> H, EqualToFor<Data<Tag>> E, size_t FirstSegmentSize, bool ThreadSafe>
std::pair<Index<Tag>, bool> IndexedHashSet<Tag, H, E, FirstSegmentSize, ThreadSafe>::insert_with_hash(size_t h, const Data<Tag>& element)
{
    assert(h == IndexedHashSet::hash(element) && "The given hash does not match container internal's hash.");
    assert(h == m_store->set.hash(element));

    return detail::find_or_lazy_insert_value_with_hash<ThreadSafe>(m_store->set, element, h, [&] { return append_new(element); });
}

template<typename Tag, HashFor<Data<Tag>> H, EqualToFor<Data<Tag>> E, size_t FirstSegmentSize, bool ThreadSafe>
std::pair<Index<Tag>, bool> IndexedHashSet<Tag, H, E, FirstSegmentSize, ThreadSafe>::complete_miss_with_hash(size_t h, const Data<Tag>& element)
{
    assert(h == IndexedHashSet::hash(element) && "The given hash does not match container internal's hash.");
    assert(h == m_store->set.hash(element));

    return detail::complete_miss_value_with_hash<ThreadSafe>(m_store->set, element, h, [&] { return append_new(element); });
}

template<typename Tag, HashFor<Data<Tag>> H, EqualToFor<Data<Tag>> E, size_t FirstSegmentSize, bool ThreadSafe>
template<typename Factory>
std::pair<Index<Tag>, bool>
IndexedHashSet<Tag, H, E, FirstSegmentSize, ThreadSafe>::complete_miss_with_hash(size_t h, const Data<Tag>& element, Factory&& factory)
{
    assert(h == IndexedHashSet::hash(element) && "The given hash does not match container internal's hash.");
    assert(h == m_store->set.hash(element));

    return detail::complete_miss_value_with_hash<ThreadSafe>(m_store->set, element, h, [&] { return append_new_with_index(std::forward<Factory>(factory)); });
}

template<typename Tag, HashFor<Data<Tag>> H, EqualToFor<Data<Tag>> E, size_t FirstSegmentSize, bool ThreadSafe>
Index<Tag> IndexedHashSet<Tag, H, E, FirstSegmentSize, ThreadSafe>::insert_new_with_hash(size_t h, const Data<Tag>& element)
{
    const auto [index, inserted] = complete_miss_with_hash(h, element);
    if (!inserted)
        throw std::logic_error("IndexedHashSet::insert_new_with_hash requires an absent key.");
    return index;
}

template<typename Tag, HashFor<Data<Tag>> H, EqualToFor<Data<Tag>> E, size_t FirstSegmentSize, bool ThreadSafe>
size_t IndexedHashSet<Tag, H, E, FirstSegmentSize, ThreadSafe>::memory_usage() const noexcept
{ return m_store ? m_store->storage.memory_usage() + detail::hash_set_memory_usage(m_store->set) : 0; }

}  // namespace ygg

#endif
