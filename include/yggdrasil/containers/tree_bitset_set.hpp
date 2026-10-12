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
#include <array>
#include <bit>
#include <boost/dynamic_bitset.hpp>
#include <boost/iterator/function_output_iterator.hpp>
#include <cassert>
#include <compare>
#include <concepts>
#include <cstddef>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

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

/// Stores canonical bitsets, given as blocks with a nonzero last block, as balanced trees shaped like
/// TreeVectorSet. As there, blocks with the width of a child index are stored inline in the child slots,
/// without a leaf table; wider blocks are stored in a leaf table. Block i always sits at position i, so
/// changing a block only changes its path to the root. All-zero subtrees are never stored: they are the
/// sentinel Index<>::MAX, except that an inline zero block is the value 0.
/// ThreadSafe permits concurrent insertion, size queries, and reads of published indices.
/// Clear, memory inspection, move, and destruction require quiescence.
template<std::unsigned_integral Block, size_t FirstSegmentSize = 32, bool ThreadSafe = false>
class TreeBitsetSet
{
private:
    static constexpr bool inline_leaves = sizeof(Block) == sizeof(uint_t);

    using Leaf = detail::TreeVectorLeaf<Block>;
    using Node = detail::TreeVectorNode<Block>;
    using LeafSet = std::conditional_t<inline_leaves, detail::NoLeafSet, IndexedHashSet<Leaf, Hash<Data<Leaf>>, EqualTo<Data<Leaf>>, FirstSegmentSize, ThreadSafe>>;
    using NodeSet = IndexedHashSet<Node, Hash<Data<Node>>, EqualTo<Data<Node>>, FirstSegmentSize, ThreadSafe>;
    using Flip = TreeBitsetFlip<Block>;

    static constexpr uint_t ZERO = Index<Node>::MAX;

    /// The slot value of an all-zero subtree covering `size` blocks.
    static constexpr uint_t zero(uint_t size) noexcept { return (inline_leaves && size == 1) ? uint_t { 0 } : ZERO; }

    uint_t insert_leaf(Block value)
    {
        if constexpr (inline_leaves)
            return std::bit_cast<uint_t>(value);
        else
            return value == Block { 0 } ? ZERO : static_cast<uint_t>(m_leaves.insert(Data<Leaf> { value }).first);
    }

    Block leaf_value(uint_t slot) const
    {
        if constexpr (inline_leaves)
            return std::bit_cast<Block>(slot);
        else
            return slot == ZERO ? Block { 0 } : m_leaves[Index<Leaf>(slot)].value;
    }

    static constexpr uint_t split(uint_t size) noexcept { return static_cast<uint_t>(std::bit_floor(size - 1)); }

    uint_t insert_node(uint_t left, uint_t right, uint_t size)
    {
        const auto middle = split(size);
        if (left == zero(middle) && right == zero(size - middle))
            return ZERO;
        return static_cast<uint_t>(m_nodes.insert(Data<Node> { left, right }).first);
    }

    uint_t insert_recursive(std::span<const Block> values)
    {
        assert(!values.empty());

        if (values.size() == 1)
            return insert_leaf(values.front());

        const auto size = to_uint_t(values.size());
        const auto middle = split(size);
        const auto left = insert_recursive(values.first(middle));
        const auto right = insert_recursive(values.subspan(middle));
        return insert_node(left, right, size);
    }

    /// Finds subtrees of a base bitset for the ranges that insert_delta_recursive visits. These ranges arrive in
    /// depth-first order, so the cursor keeps its last descent path and only climbs as far as the next range requires
    /// instead of descending from the root for every range.
    class BaseCursor
    {
    public:
        BaseCursor(const NodeSet& nodes, TreeBitsetIndex<Block> base) : m_nodes(nodes), m_base_size(base.size) { m_path[0] = Frame { base.root, 0, base.size }; }

        /// Returns the subtree of the base that covers exactly [offset, offset + size), or nullopt if the base has
        /// no such subtree. Ranges inside all-zero subtrees, or beyond the base, are zero.
        std::optional<uint_t> find(uint_t offset, uint_t size)
        {
            if (offset >= m_base_size)
                return zero(size);
            if (offset + size > m_base_size)
                return std::nullopt;

            while (!m_path[m_depth].contains(offset, size))  // The root frame contains every range inside the base.
                --m_depth;

            while (true)
            {
                const auto frame = m_path[m_depth];
                if (frame.size == size)
                    return frame.root;
                if (frame.root == ZERO)
                    return zero(size);

                const auto& node = m_nodes[Index<Node>(frame.root)];
                const auto middle = split(frame.size);
                if (offset + size <= frame.offset + middle)
                    m_path[++m_depth] = Frame { node.left, frame.offset, middle };
                else if (offset >= frame.offset + middle)
                    m_path[++m_depth] = Frame { node.right, frame.offset + middle, frame.size - middle };
                else
                    return std::nullopt;
            }
        }

    private:
        struct Frame
        {
            uint_t root;
            uint_t offset;
            uint_t size;

            bool contains(uint_t other_offset, uint_t other_size) const noexcept { return offset <= other_offset && other_offset + other_size <= offset + size; }
        };

        const NodeSet& m_nodes;
        uint_t m_base_size;
        std::array<Frame, std::numeric_limits<uint_t>::digits + 2> m_path {};  // Bounded by the tree height.
        size_t m_depth = 0;
    };

    /// Builds the subtree for [offset, offset + size) of the new bitset. `base_root` covers the
    /// same range in the base bitset if `base_known`; otherwise it is looked up with `base`.
    uint_t insert_delta_recursive(BaseCursor& base, bool base_known, uint_t base_root, uint_t offset, uint_t size, std::span<const Flip> flips)
    {
        assert(size > 0);

        if (!base_known)
        {
            if (const auto root = base.find(offset, size))
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
            return flips.empty() ? base_root : insert_leaf(leaf_value(base_root) ^ flips.front().mask);
        }

        const auto middle = split(size);
        const auto split_flips = std::ranges::lower_bound(flips, offset + middle, {}, &Flip::position) - flips.begin();

        auto left_root = zero(middle);
        auto right_root = zero(size - middle);
        if (base_known && base_root != ZERO)
        {
            const auto& node = m_nodes[Index<Node>(base_root)];
            left_root = node.left;
            right_root = node.right;
        }

        const auto left = insert_delta_recursive(base, base_known, left_root, offset, middle, flips.first(split_flips));
        const auto right = insert_delta_recursive(base, base_known, right_root, offset + middle, size - middle, flips.subspan(split_flips));
        return insert_node(left, right, size);
    }

    /// Returns the number of blocks up to and including the last nonzero block of the subtree.
    uint_t trimmed_size(uint_t root, uint_t size) const
    {
        auto offset = uint_t { 0 };
        while (root != zero(size))
        {
            if (size == 1)
                return offset + 1;

            const auto& node = m_nodes[Index<Node>(root)];
            const auto middle = split(size);
            if (node.right != zero(size - middle))
            {
                root = node.right;
                offset += middle;
                size -= middle;
            }
            else
            {
                root = node.left;  // Nonzero, since insert_node never stores two zero children.
                size = middle;
            }
        }
        return offset;
    }

    void read_recursive(uint_t root, std::span<Block> values) const
    {
        assert(!values.empty());

        if (values.size() == 1)
        {
            values.front() = leaf_value(root);
            return;
        }

        if (root == ZERO)
        {
            std::ranges::fill(values, Block { 0 });
            return;
        }

        const auto& node = m_nodes[Index<Node>(root)];
        const auto middle = split(to_uint_t(values.size()));
        read_recursive(node.left, values.first(middle));
        read_recursive(node.right, values.subspan(middle));
    }

    template<typename Callback>
    void for_each_block_recursive(uint_t root, uint_t offset, uint_t size, Callback& callback) const
    {
        assert(size > 0);

        if (root == zero(size))
            return;

        if (size == 1)
        {
            callback(offset, leaf_value(root));
            return;
        }

        const auto& node = m_nodes[Index<Node>(root)];
        const auto middle = split(size);
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
    /// unique position. Only the paths from the flipped blocks, and from blocks at changed sizes,
    /// to the root are rebuilt. The result is trimmed to its last nonzero block.
    index_type insert(index_type base, std::span<const flip_type> flips)
    {
        assert(std::ranges::is_sorted(flips, std::ranges::less {}, &Flip::position));
        assert(std::ranges::adjacent_find(flips, {}, &Flip::position) == flips.end());

        const auto upper = flips.empty() ? base.size : std::max(base.size, flips.back().position + 1);
        if (upper == 0)
            return index_type {};

        auto cursor = BaseCursor(m_nodes, base);
        const auto root = insert_delta_recursive(cursor, false, zero(upper), 0, upper, flips);
        const auto size = trimmed_size(root, upper);
        if (size == upper)
            return index_type { root, size };
        if (size == 0)
            return index_type {};

        // Cleared trailing blocks: rebuild the right spine for the shorter size, reusing all other subtrees.
        auto trimmed = BaseCursor(m_nodes, index_type { root, upper });
        return index_type { insert_delta_recursive(trimmed, false, zero(size), 0, size, {}), size };
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
    [[no_unique_address]] LeafSet m_leaves;
    NodeSet m_nodes;
};

/// Sorts `flips` by position and combines flips of the same block, dropping flips that cancel out.
/// The result satisfies the precondition of TreeBitsetSet::insert(base, flips).
template<std::unsigned_integral Block>
void normalize_flips(std::vector<TreeBitsetFlip<Block>>& flips)
{
    std::ranges::sort(flips, {}, &TreeBitsetFlip<Block>::position);

    auto last = size_t { 0 };
    for (size_t i = 0; i < flips.size(); ++i)
    {
        if (last > 0 && flips[last - 1].position == flips[i].position)
            flips[last - 1].mask ^= flips[i].mask;
        else
            flips[last++] = flips[i];
    }
    flips.resize(last);
    std::erase_if(flips, [](const TreeBitsetFlip<Block>& flip) { return flip.mask == Block { 0 }; });
}

/// Appends one flip per block in which `from` and `to` differ, in increasing position order.
template<std::unsigned_integral Block>
void append_diff_flips(std::span<const Block> from, std::span<const Block> to, std::vector<TreeBitsetFlip<Block>>& flips)
{
    for (size_t i = 0; i < std::max(from.size(), to.size()); ++i)
    {
        const auto mask = Block((i < from.size() ? from[i] : Block { 0 }) ^ (i < to.size() ? to[i] : Block { 0 }));
        if (mask != Block { 0 })
            flips.push_back(TreeBitsetFlip<Block> { to_uint_t(i), mask });
    }
}

/// Appends the bits of `bitset` as blocks of type Block, which may be narrower than the blocks of `bitset`.
template<std::unsigned_integral Block, std::unsigned_integral BitsetBlock, typename Allocator>
    requires(sizeof(BitsetBlock) % sizeof(Block) == 0)
void append_blocks(const boost::dynamic_bitset<BitsetBlock, Allocator>& bitset, std::vector<Block>& blocks)
{
    constexpr auto parts = sizeof(BitsetBlock) / sizeof(Block);
    boost::to_block_range(bitset,
                          boost::make_function_output_iterator(
                              [&](BitsetBlock block)
                              {
                                  for (size_t i = 0; i < parts; ++i)
                                      blocks.push_back(Block(block >> (i * std::numeric_limits<Block>::digits)));
                              }));
}

/// Removes trailing zero blocks, as TreeBitsetSet::insert(blocks) requires a nonzero last block.
template<std::unsigned_integral Block>
void trim_trailing_zero_blocks(std::vector<Block>& blocks)
{
    while (!blocks.empty() && blocks.back() == Block { 0 })
        blocks.pop_back();
}

/// Replaces the bits of `bitset` by those of `blocks`. The size becomes a multiple of the block width of `bitset`.
template<std::unsigned_integral Block, std::unsigned_integral BitsetBlock, typename Allocator>
    requires(sizeof(BitsetBlock) % sizeof(Block) == 0)
void assign_blocks(std::span<const Block> blocks, boost::dynamic_bitset<BitsetBlock, Allocator>& bitset)
{
    constexpr auto parts = sizeof(BitsetBlock) / sizeof(Block);
    bitset.clear();
    for (size_t i = 0; i < blocks.size(); i += parts)
    {
        auto block = BitsetBlock { 0 };
        for (size_t j = 0; j < parts && i + j < blocks.size(); ++j)
            block |= BitsetBlock(blocks[i + j]) << (j * std::numeric_limits<Block>::digits);
        bitset.append(block);
    }
}

}  // namespace ygg

#endif
