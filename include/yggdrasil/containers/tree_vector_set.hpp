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

#include "yggdrasil/containers/detail/indexed_hash_set_impl.hpp"
#include "yggdrasil/core/types_utils.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <compare>
#include <concepts>
#include <cstddef>
#include <limits>
#include <span>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

namespace ygg::detail
{
/// Stands in for the leaf table of tree sets whose leaves are stored inline, so that no table is instantiated.
struct NoLeafSet
{
    void clear() noexcept {}
    static constexpr size_t memory_usage() noexcept { return 0; }
    static constexpr size_t size() noexcept { return 0; }
    static constexpr bool empty() noexcept { return true; }
};


template<typename T>
struct TreeVectorLeaf;

template<typename T, size_t Arity = 2>
struct TreeVectorNode;

/// The children of a subtree covering `size` >= 2 positions each cover `tree_chunk<Arity>(size)` positions, except
/// that the last may cover fewer: the smallest power of Arity whose Arity-fold covers `size`. Positions of a block
/// never depend on later positions, so changing a position only changes its path to the root. For Arity 2 this
/// is bit_floor(size - 1).
template<size_t Arity>
    requires(std::has_single_bit(Arity) && Arity >= 2)
constexpr uint_t tree_chunk(uint_t size) noexcept
{
    assert(size >= 2);
    constexpr auto bits = std::countr_zero(Arity);
    const auto levels = (std::bit_width(size - 1) + bits - 1) / bits;
    return uint_t { 1 } << (bits * (levels - 1));
}

/// Number of children used by a subtree covering `size` >= 2 positions, in [2, Arity].
template<size_t Arity>
constexpr uint_t tree_num_children(uint_t size) noexcept
{
    const auto chunk = tree_chunk<Arity>(size);
    return (size + chunk - 1) / chunk;
}

/// Value of child slots beyond the end of a subtree, which only occur in its last node per level.
/// Fixed so that equal vectors are equal trees; never read.
inline constexpr uint_t tree_unused_child = std::numeric_limits<uint_t>::max();

}  // namespace ygg::detail

namespace ygg
{

template<typename T>
struct Data<detail::TreeVectorLeaf<T>>
{
    T value;

    Data() = default;
    Data(T value_) : value(std::move(value_)) {}

    auto cista_members() noexcept { return std::tie(value); }
    auto cista_members() const noexcept { return std::tie(value); }
    static constexpr auto identifying_members() noexcept { return Members<&Data::value> {}; }
    void clear() noexcept
    {
        std::apply([](auto&... member) { (ygg::clear(member), ...); }, cista_members());
    }
};


template<typename T, size_t Arity>
struct Data<detail::TreeVectorNode<T, Arity>>
{
    std::array<uint_t, Arity> children;

    Data() = default;
    explicit Data(const std::array<uint_t, Arity>& children_) : children(children_) {}

    auto cista_members() noexcept { return std::tie(children); }
    auto cista_members() const noexcept { return std::tie(children); }
    static constexpr auto identifying_members() noexcept { return Members<&Data::children> {}; }
    void clear() noexcept
    {
        std::apply([](auto&... member) { (ygg::clear(member), ...); }, cista_members());
    }
};


template<typename T>
struct TreeVectorIndex
{
    uint_t root {};
    uint_t size {};

    friend bool operator==(const TreeVectorIndex&, const TreeVectorIndex&) = default;
    friend auto operator<=>(const TreeVectorIndex&, const TreeVectorIndex&) = default;

    static constexpr auto identifying_members() noexcept { return Members<&TreeVectorIndex::root, &TreeVectorIndex::size> {}; }
};

/// Overwrites the value at `position`.
template<typename T>
struct TreeVectorWrite
{
    uint_t position {};
    T value {};
};

/// Stores canonical vectors as balanced trees of canonical scalar leaves and
/// internal nodes with Arity children each (see detail::tree_chunk). The subtree length determines whether an
/// index identifies a leaf or an internal node, so nodes only need to store their child indices.
/// Values with the size of a child index are stored inline in the child slot, so num_leaves() stays 0.
/// ThreadSafe permits concurrent insertion, size queries, and reads of published indices.
/// Clear, memory inspection, move, and destruction require quiescence.
template<std::copyable T, size_t FirstSegmentSize = 32, bool ThreadSafe = false, size_t Arity = 2>
    requires Hashable<T> && EqualityComparableByEqualTo<T> && (std::has_single_bit(Arity) && Arity >= 2)
class TreeVectorSet
{
private:
    using Leaf = detail::TreeVectorLeaf<T>;
    using Node = detail::TreeVectorNode<T, Arity>;
    using Children = std::array<uint_t, Arity>;
    // Unique object representations make bitwise identity agree with equality (rules out float, padding).
    static constexpr bool inline_leaves = std::has_unique_object_representations_v<T> && sizeof(T) == sizeof(uint_t);

    using LeafSet = std::conditional_t<inline_leaves, detail::NoLeafSet, IndexedHashSet<Leaf, Hash<Data<Leaf>>, EqualTo<Data<Leaf>>, FirstSegmentSize, ThreadSafe>>;
    using NodeSet = IndexedHashSet<Node, Hash<Data<Node>>, EqualTo<Data<Node>>, FirstSegmentSize, ThreadSafe>;

    using Write = TreeVectorWrite<T>;

    uint_t insert_leaf(const T& value)
    {
        if constexpr (inline_leaves)
            return std::bit_cast<uint_t>(value);
        else
            return static_cast<uint_t>(m_leaves.insert(Data<Leaf> { value }).first);
    }

    uint_t insert_node(const Children& children) { return static_cast<uint_t>(m_nodes.insert(Data<Node> { children }).first); }

    uint_t insert_recursive(std::span<const T> values)
    {
        assert(!values.empty());

        if (values.size() == 1)
            return insert_leaf(values.front());

        const auto size = to_uint_t(values.size());
        const auto chunk = detail::tree_chunk<Arity>(size);
        auto children = Children {};
        children.fill(detail::tree_unused_child);
        for (uint_t k = 0, offset = 0; offset < size; ++k, offset += chunk)
            children[k] = insert_recursive(values.subspan(offset, std::min(chunk, size - offset)));
        return insert_node(children);
    }

    /// Rebuilds the subtree `base_root` covering [offset, offset + size) with `writes` applied.
    uint_t insert_writes_recursive(uint_t base_root, uint_t offset, uint_t size, std::span<const Write> writes)
    {
        assert(size > 0);

        if (writes.empty())
            return base_root;

        if (size == 1)
            return insert_leaf(writes.front().value);

        auto children = m_nodes[Index<Node>(base_root)].children;
        const auto chunk = detail::tree_chunk<Arity>(size);
        for (uint_t k = 0, child_offset = offset; child_offset < offset + size; ++k, child_offset += chunk)
        {
            const auto child_size = std::min(chunk, offset + size - child_offset);
            const auto split = std::ranges::lower_bound(writes, child_offset + child_size, {}, &Write::position) - writes.begin();
            children[k] = insert_writes_recursive(children[k], child_offset, child_size, writes.first(split));
            writes = writes.subspan(split);
        }
        return insert_node(children);
    }

    void read_recursive(uint_t root, std::span<T> values) const
    {
        assert(!values.empty());

        if (values.size() == 1)
        {
            if constexpr (inline_leaves)
                values.front() = std::bit_cast<T>(root);
            else
                values.front() = m_leaves[Index<Leaf>(root)].value;
            return;
        }

        const auto& node = m_nodes[Index<Node>(root)];
        const auto size = to_uint_t(values.size());
        const auto chunk = detail::tree_chunk<Arity>(size);
        for (uint_t k = 0, offset = 0; offset < size; ++k, offset += chunk)
            read_recursive(node.children[k], values.subspan(offset, std::min(chunk, size - offset)));
    }

public:
    using value_type = T;
    using index_type = TreeVectorIndex<T>;
    using write_type = Write;

    static constexpr bool thread_safe = ThreadSafe;
    static constexpr size_t arity = Arity;

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

    /// Inserts the vector obtained from `base` by applying `writes`, which must be sorted by unique position
    /// within the vector. Only the paths from the written positions to the root are rebuilt.
    index_type insert(index_type base, std::span<const write_type> writes)
    {
        assert(std::ranges::is_sorted(writes, std::ranges::less {}, &Write::position));
        assert(std::ranges::adjacent_find(writes, {}, &Write::position) == writes.end());
        assert(writes.empty() || writes.back().position < base.size);

        return base.size == 0 ? base : index_type { insert_writes_recursive(base.root, 0, base.size, writes), base.size };
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
    [[no_unique_address]] LeafSet m_leaves;
    NodeSet m_nodes;
};

}  // namespace ygg

#endif
