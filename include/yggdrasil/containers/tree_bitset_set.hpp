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

#ifndef YGG_CONTAINERS_TREE_BITSET_SET_HPP_
#define YGG_CONTAINERS_TREE_BITSET_SET_HPP_

#include "yggdrasil/containers/indexed_hash_set.hpp"
#include "yggdrasil/containers/tree_vector_set.hpp"
#include "yggdrasil/core/types_utils.hpp"

#include <algorithm>
#include <bit>
#include <cassert>
#include <compare>
#include <concepts>
#include <cstddef>
#include <optional>
#include <span>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace ygg
{

template<std::unsigned_integral Block>
struct TreeBitsetIndex
{
    uint_t root {};
    uint_t size {};

    friend bool operator==(const TreeBitsetIndex&, const TreeBitsetIndex&) = default;
    friend auto operator<=>(const TreeBitsetIndex&, const TreeBitsetIndex&) = default;

    auto identifying_members() const noexcept { return std::tie(root, size); }
};

/// Toggles the bits in `mask` of the block at `position`.
template<std::unsigned_integral Block>
struct TreeBitsetFlip
{
    uint_t position {};
    Block mask {};

    friend bool operator==(const TreeBitsetFlip&, const TreeBitsetFlip&) = default;
};

/// Stores canonical bitsets, given as blocks with a nonzero last block, as balanced trees
/// of canonical block leaves and internal nodes, shaped like TreeVectorSet. Block i always
/// sits at position i, so changing a block only changes its path to the root. All-zero
/// subtrees are represented by the sentinel Index<>::MAX and are never stored.
/// ThreadSafe permits concurrent insertion, size queries, and reads of published indices.
/// Clear, memory inspection, move, and destruction require quiescence.
template<std::unsigned_integral Block, size_t FirstSegmentSize = 32, bool ThreadSafe = false>
class TreeBitsetSet
{
private:
    using Leaf = detail::TreeVectorLeaf<Block>;
    using Node = detail::TreeVectorNode<Block>;
    using LeafSet = IndexedHashSet<Leaf, Hash<Data<Leaf>>, EqualTo<Data<Leaf>>, FirstSegmentSize, ThreadSafe>;
    using NodeSet = IndexedHashSet<Node, Hash<Data<Node>>, EqualTo<Data<Node>>, FirstSegmentSize, ThreadSafe>;
    using Flip = TreeBitsetFlip<Block>;

    static_assert(Index<Leaf>::MAX == Index<Node>::MAX);
    static constexpr uint_t ZERO = Index<Leaf>::MAX;

    uint_t insert_leaf(Block value)
    {
        if (value == Block { 0 })
            return ZERO;
        return static_cast<uint_t>(m_leaves.insert(Data<Leaf> { value }).first);
    }

    uint_t insert_node(uint_t left, uint_t right)
    {
        if (left == ZERO && right == ZERO)
            return ZERO;
        return static_cast<uint_t>(m_nodes.insert(Data<Node> { left, right }).first);
    }

    uint_t insert_recursive(std::span<const Block> values)
    {
        assert(!values.empty());

        if (values.size() == 1)
            return insert_leaf(values.front());

        const auto middle = std::bit_floor(values.size() - 1);
        const auto left = insert_recursive(values.first(middle));
        const auto right = insert_recursive(values.subspan(middle));
        return insert_node(left, right);
    }

    /// Returns the root of the subtree of `base` that covers exactly [offset, offset + size),
    /// or nullopt if `base` has no such subtree. Zero subtrees cover any range inside them.
    std::optional<uint_t> find_subtree(TreeBitsetIndex<Block> base, uint_t offset, uint_t size) const
    {
        if (offset >= base.size)
            return ZERO;
        if (offset + size > base.size)
            return std::nullopt;

        auto root = base.root;
        auto root_offset = uint_t { 0 };
        auto root_size = base.size;
        while (root != ZERO && root_size != size)
        {
            if (root_size == 1)
                return std::nullopt;

            const auto& node = m_nodes[Index<Node>(root)];
            const auto middle = static_cast<uint_t>(std::bit_floor(root_size - 1));
            if (offset + size <= root_offset + middle)
            {
                root = node.left;
                root_size = middle;
            }
            else if (offset >= root_offset + middle)
            {
                root = node.right;
                root_offset += middle;
                root_size -= middle;
            }
            else
                return std::nullopt;
        }
        assert(root == ZERO || root_offset == offset);
        return root;
    }

    /// Builds the subtree for [offset, offset + size) of the new bitset. `base_root` covers the
    /// same range in the base bitset if `base_known`; otherwise it is looked up in `base`.
    uint_t insert_delta_recursive(TreeBitsetIndex<Block> base, bool base_known, uint_t base_root, uint_t offset, uint_t size, std::span<const Flip> flips)
    {
        assert(size > 0);

        if (!base_known)
        {
            if (const auto root = find_subtree(base, offset, size))
            {
                base_known = true;
                base_root = *root;
            }
        }

        if (flips.empty() && base_known)
            return base_root;

        if (size == 1)
        {
            assert(base_known);
            const auto value = (base_root == ZERO) ? Block { 0 } : m_leaves[Index<Leaf>(base_root)].value;
            return insert_leaf(flips.empty() ? value : Block(value ^ flips.front().mask));
        }

        const auto middle = static_cast<uint_t>(std::bit_floor(size - 1));
        const auto split = std::ranges::lower_bound(flips, offset + middle, {}, &Flip::position) - flips.begin();

        auto left_root = ZERO;
        auto right_root = ZERO;
        if (base_known && base_root != ZERO)
        {
            const auto& node = m_nodes[Index<Node>(base_root)];
            left_root = node.left;
            right_root = node.right;
        }

        const auto left = insert_delta_recursive(base, base_known, left_root, offset, middle, flips.first(split));
        const auto right = insert_delta_recursive(base, base_known, right_root, offset + middle, size - middle, flips.subspan(split));
        return insert_node(left, right);
    }

    void read_recursive(uint_t root, std::span<Block> values) const
    {
        assert(!values.empty());

        if (root == ZERO)
        {
            std::ranges::fill(values, Block { 0 });
            return;
        }

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

    template<typename Callback>
    void for_each_block_recursive(uint_t root, uint_t offset, uint_t size, Callback& callback) const
    {
        assert(size > 0);

        if (root == ZERO)
            return;

        if (size == 1)
        {
            callback(offset, m_leaves[Index<Leaf>(root)].value);
            return;
        }

        const auto& node = m_nodes[Index<Node>(root)];
        const auto middle = static_cast<uint_t>(std::bit_floor(size - 1));
        for_each_block_recursive(node.left, offset, middle, callback);
        for_each_block_recursive(node.right, offset + middle, size - middle, callback);
    }

public:
    using value_type = Block;
    using index_type = TreeBitsetIndex<Block>;
    using flip_type = Flip;

    static constexpr bool thread_safe = ThreadSafe;

    TreeBitsetSet() = default;
    TreeBitsetSet(const TreeBitsetSet&) = delete;
    TreeBitsetSet& operator=(const TreeBitsetSet&) = delete;
    TreeBitsetSet(TreeBitsetSet&&) = default;
    TreeBitsetSet& operator=(TreeBitsetSet&&) = default;

    index_type insert(std::span<const Block> blocks)
    {
        if (!blocks.empty() && blocks.back() == Block { 0 })
            throw std::invalid_argument("TreeBitsetSet::insert requires a nonzero last block.");

        const auto size = to_uint_t(blocks.size());
        return blocks.empty() ? index_type {} : index_type { insert_recursive(blocks), size };
    }

    /// Inserts the bitset obtained from `base` by applying `flips`, which must be sorted by
    /// unique position. The result must have exactly `size` blocks, i.e., its last block nonzero.
    /// Only the paths from the flipped blocks, and from blocks at changed sizes, to the root are rebuilt.
    index_type insert(index_type base, std::span<const flip_type> flips, uint_t size)
    {
        assert(std::ranges::is_sorted(flips, std::ranges::less {}, &Flip::position));
        assert(std::ranges::adjacent_find(flips, {}, &Flip::position) == flips.end());
        assert(flips.empty() || flips.back().position < std::max(size, base.size));

        if (size == 0)
            return index_type {};

        const auto root = insert_delta_recursive(base, false, ZERO, 0, size, flips.first(std::ranges::lower_bound(flips, size, {}, &Flip::position) - flips.begin()));
        assert(root != ZERO);
        return index_type { root, size };
    }

    void read(index_type index, std::span<Block> blocks) const
    {
        if (blocks.size() != index.size)
            throw std::invalid_argument("TreeBitsetSet::read requires an output span matching the stored block count.");
        if (!blocks.empty())
            read_recursive(index.root, blocks);
    }

    /// Calls `callback(position, block)` for each nonzero block in increasing position order.
    template<typename Callback>
    void for_each_block(index_type index, Callback&& callback) const
    {
        if (index.size != 0)
            for_each_block_recursive(index.root, 0, index.size, callback);
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
