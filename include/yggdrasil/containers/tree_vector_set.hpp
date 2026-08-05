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

#ifndef YGG_CONTAINERS_TREE_VECTOR_SET_HPP_
#define YGG_CONTAINERS_TREE_VECTOR_SET_HPP_

#include "yggdrasil/containers/indexed_hash_set.hpp"

#include <bit>
#include <cassert>
#include <compare>
#include <concepts>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <tuple>

namespace ygg::detail
{

template<typename T>
struct TreeVectorLeaf;

template<typename T>
struct TreeVectorNode;

}  // namespace ygg::detail

namespace ygg
{

template<typename T>
struct Data<detail::TreeVectorLeaf<T>>
{
    T value;

    auto identifying_members() const noexcept { return std::tie(value); }
};

template<typename T>
struct Index<detail::TreeVectorLeaf<T>> : IndexMixin<Index<detail::TreeVectorLeaf<T>>>
{
    using Base = IndexMixin<Index<detail::TreeVectorLeaf<T>>>;
    using Base::Base;
};

template<typename T>
struct Data<detail::TreeVectorNode<T>>
{
    uint_t left;
    uint_t right;

    auto identifying_members() const noexcept { return std::tie(left, right); }
};

template<typename T>
struct Index<detail::TreeVectorNode<T>> : IndexMixin<Index<detail::TreeVectorNode<T>>>
{
    using Base = IndexMixin<Index<detail::TreeVectorNode<T>>>;
    using Base::Base;
};

template<typename T>
struct TreeVectorIndex
{
    uint_t root {};
    uint_t size {};

    friend bool operator==(const TreeVectorIndex&, const TreeVectorIndex&) = default;
    friend auto operator<=>(const TreeVectorIndex&, const TreeVectorIndex&) = default;

    auto identifying_members() const noexcept { return std::tie(root, size); }
};

/// Stores canonical vectors as balanced trees of canonical scalar leaves and
/// internal nodes. The subtree length determines whether an index identifies a
/// leaf or an internal node, so nodes only need to store their two child indices.
/// ThreadSafe permits concurrent insertion, size queries, and reads of published indices.
/// Clear, memory inspection, move, and destruction require quiescence.
template<std::copyable T, size_t FirstSegmentSize = 32, bool ThreadSafe = false>
    requires Hashable<T> && EqualityComparableByEqualTo<T>
class TreeVectorSet
{
private:
    using Leaf = detail::TreeVectorLeaf<T>;
    using Node = detail::TreeVectorNode<T>;
    using LeafSet = IndexedHashSet<Leaf, Hash<Data<Leaf>>, EqualTo<Data<Leaf>>, FirstSegmentSize, ThreadSafe>;
    using NodeSet = IndexedHashSet<Node, Hash<Data<Node>>, EqualTo<Data<Node>>, FirstSegmentSize, ThreadSafe>;

    uint_t insert_recursive(std::span<const T> values)
    {
        assert(!values.empty());

        if (values.size() == 1)
            return static_cast<uint_t>(m_leaves.insert(Data<Leaf> { values.front() }).first);

        const auto middle = std::bit_floor(values.size() - 1);
        const auto left = insert_recursive(values.first(middle));
        const auto right = insert_recursive(values.subspan(middle));
        return static_cast<uint_t>(m_nodes.insert(Data<Node> { left, right }).first);
    }

    void read_recursive(uint_t root, std::span<T> values) const
    {
        assert(!values.empty());

        if (values.size() == 1)
        {
            values.front() = m_leaves[Index<Leaf>(root)].value;
            return;
        }

        const auto& node = m_nodes[Index<Node>(root)];
        const auto middle = std::bit_floor(values.size() - 1);
        read_recursive(node.left, values.first(middle));
        read_recursive(node.right, values.subspan(middle));
    }

public:
    using value_type = T;
    using index_type = TreeVectorIndex<T>;

    static constexpr bool thread_safe = ThreadSafe;

    TreeVectorSet() = default;
    TreeVectorSet(const TreeVectorSet&) = delete;
    TreeVectorSet& operator=(const TreeVectorSet&) = delete;
    TreeVectorSet(TreeVectorSet&&) = default;
    TreeVectorSet& operator=(TreeVectorSet&&) = default;

    index_type insert(std::span<const T> values)
    {
        const auto size = to_uint_t(values.size());
        return values.empty() ? index_type {} : index_type { insert_recursive(values), size };
    }

    void read(index_type index, std::span<T> values) const
    {
        if (values.size() != index.size)
            throw std::invalid_argument("TreeVectorSet::read requires an output span matching the stored vector size.");
        if (!values.empty())
            read_recursive(index.root, values);
    }

    void clear() noexcept
    {
        m_nodes.clear();
        m_leaves.clear();
    }

    size_t memory_usage() const noexcept { return m_leaves.memory_usage() + m_nodes.memory_usage(); }

    size_t num_leaves() const noexcept { return m_leaves.size(); }
    size_t num_nodes() const noexcept { return m_nodes.size(); }
    bool empty() const noexcept { return m_leaves.empty() && m_nodes.empty(); }

private:
    LeafSet m_leaves;
    NodeSet m_nodes;
};

}  // namespace ygg

#endif
