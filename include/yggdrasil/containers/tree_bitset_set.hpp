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

#include "yggdrasil/containers/detail/indexed_hash_set_impl.hpp"
#include "yggdrasil/containers/tree_vector_set.hpp"
#include "yggdrasil/core/bit.hpp"
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
#include <cstdint>
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

    static constexpr auto identifying_members() noexcept { return ygg::Members<&TreeBitsetIndex::root, &TreeBitsetIndex::size> {}; }
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
/// TreeVectorSet, with Arity children per internal node. As there, blocks with the width of a child index are
/// stored inline in the child slots,
/// without a leaf table; wider blocks are stored in a leaf table. Block i always sits at position i, so
/// changing a block only changes its path to the root. All-zero subtrees are never stored: they are the
/// sentinel Index<>::MAX, except that an inline zero block is the value 0.
/// ThreadSafe permits concurrent insertion, size queries, and reads of published indices.
/// Clear, memory inspection, move, and destruction require quiescence.
template<std::unsigned_integral Block, size_t FirstSegmentSize = 32, bool ThreadSafe = false, size_t Arity = 2>
    requires(std::has_single_bit(Arity) && Arity >= 2)
class TreeBitsetSet
{
private:
    static constexpr bool inline_leaves = sizeof(Block) == sizeof(uint_t);

    using Leaf = detail::TreeVectorLeaf<Block>;
    using Node = detail::TreeVectorNode<Block, Arity>;
    using Children = std::array<uint_t, Arity>;
    using LeafSet =
        std::conditional_t<inline_leaves, detail::NoLeafSet, IndexedHashSet<Leaf, Hash<Data<Leaf>>, EqualTo<Data<Leaf>>, FirstSegmentSize, ThreadSafe>>;
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

    static constexpr uint_t chunk(uint_t size) noexcept { return detail::tree_chunk<Arity>(size); }

    /// The number of blocks covered by child `k` of a subtree covering `size` blocks.
    static constexpr uint_t child_size(uint_t size, uint_t k) noexcept { return std::min(chunk(size), size - k * chunk(size)); }

    /// Stores the node of a subtree covering `size` blocks, whose unused trailing children are tree_unused_child.
    uint_t insert_node(const Children& children, uint_t size)
    {
        const auto num_children = detail::tree_num_children<Arity>(size);
        for (uint_t k = 0; k < num_children; ++k)
            if (children[k] != zero(child_size(size, k)))
                return static_cast<uint_t>(m_nodes.insert(Data<Node> { children }).first);
        return ZERO;
    }

    static Children unused_children() noexcept
    {
        auto children = Children {};
        children.fill(detail::tree_unused_child);
        return children;
    }

    uint_t insert_recursive(std::span<const Block> values)
    {
        assert(!values.empty());

        if (values.size() == 1)
            return insert_leaf(values.front());

        const auto size = to_uint_t(values.size());
        auto children = unused_children();
        for (uint_t k = 0, offset = 0; offset < size; ++k, offset += chunk(size))
            children[k] = insert_recursive(values.subspan(offset, child_size(size, k)));
        return insert_node(children, size);
    }

    /// Finds subtrees of a base bitset for the ranges that insert_delta_recursive visits. These ranges arrive in
    /// depth-first order, so the cursor keeps its last descent path and only climbs as far as the next range requires
    /// instead of descending from the root for every range.
    class BaseCursor
    {
    public:
        BaseCursor(const NodeSet& nodes, TreeBitsetIndex<Block> base) : m_nodes(&nodes), m_base_size(base.size)
        { m_path[0] = Frame { base.root, 0, base.size }; }

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

                const auto& node = (*m_nodes)[Index<Node>(frame.root)];
                const auto k = (offset - frame.offset) / chunk(frame.size);
                const auto child_offset = frame.offset + k * chunk(frame.size);
                const auto child_blocks = child_size(frame.size, k);
                if (offset + size > child_offset + child_blocks)
                    return std::nullopt;
                m_path[++m_depth] = Frame { node.children[k], child_offset, child_blocks };
            }
        }

    private:
        struct Frame
        {
            uint_t root;
            uint_t offset;
            uint_t size;

            bool contains(uint_t other_offset, uint_t other_size) const noexcept
            { return offset <= other_offset && other_offset + other_size <= offset + size; }
        };

        const NodeSet* m_nodes;
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

        const auto num_children = detail::tree_num_children<Arity>(size);
        auto children = unused_children();
        if (base_known && base_root != ZERO)
            children = m_nodes[Index<Node>(base_root)].children;
        else
            for (uint_t k = 0; k < num_children; ++k)
                children[k] = zero(child_size(size, k));

        for (uint_t k = 0, child_offset = offset; k < num_children; ++k, child_offset += chunk(size))
        {
            const auto child_blocks = child_size(size, k);
            const auto split_flips = std::ranges::lower_bound(flips, child_offset + child_blocks, {}, &Flip::position) - flips.begin();
            children[k] = insert_delta_recursive(base, base_known, children[k], child_offset, child_blocks, flips.first(split_flips));
            flips = flips.subspan(split_flips);
        }
        return insert_node(children, size);
    }

    /// Returns the number of blocks up to and including the last nonzero block of the subtree.
    uint_t trimmed_size(uint_t root, uint_t size) const
    {
        auto offset = uint_t { 0 };
        while (root != zero(size))
        {
            if (size == 1)
                return offset + 1;

            // Descend into the last nonzero child, which exists since insert_node never stores all-zero children.
            const auto& node = m_nodes[Index<Node>(root)];
            auto k = detail::tree_num_children<Arity>(size) - 1;
            while (node.children[k] == zero(child_size(size, k)))
                --k;
            root = node.children[k];
            offset += k * chunk(size);
            size = child_size(size, k);
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
        const auto size = to_uint_t(values.size());
        for (uint_t k = 0, offset = 0; offset < size; ++k, offset += chunk(size))
            read_recursive(node.children[k], values.subspan(offset, child_size(size, k)));
    }

public:
    using value_type = Block;
    using index_type = TreeBitsetIndex<Block>;
    using flip_type = Flip;

    static constexpr bool thread_safe = ThreadSafe;
    static constexpr size_t arity = Arity;

    TreeBitsetSet() = default;
    ~TreeBitsetSet() = default;
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

/// Appends the flips that XOR the `length`-bit integer `value` into the bits starting at bit `offset` of block
/// `position`, as used for bit-packed integers. The integer may span two blocks.
template<std::unsigned_integral Block>
void append_int_flips(uint_t position, uint8_t offset, uint8_t length, Block value, std::vector<TreeBitsetFlip<Block>>& flips)
{
    auto masks = std::array<Block, 2> {};
    bit::write_int(masks.data(), value, offset, length);
    for (uint_t i = 0; i < masks.size(); ++i)
        if (masks[i] != Block { 0 })
            flips.push_back(TreeBitsetFlip<Block> { position + i, masks[i] });
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
    constexpr auto parts = std::numeric_limits<BitsetBlock>::digits / std::numeric_limits<Block>::digits;
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
    constexpr auto parts = std::numeric_limits<BitsetBlock>::digits / std::numeric_limits<Block>::digits;
    bitset.clear();
    for (size_t i = 0; i < blocks.size(); i += parts)
    {
        auto block = BitsetBlock { 0 };
        for (size_t j = 0; j < parts && i + j < blocks.size(); ++j)
            block |= BitsetBlock(blocks[i + j]) << (j * std::numeric_limits<Block>::digits);
        bitset.append(block);
    }
}

/// Inserts `bits` into `set`. `blocks` is scratch space.
template<std::unsigned_integral Block, size_t FirstSegmentSize, bool ThreadSafe, size_t Arity, std::unsigned_integral BitsetBlock, typename Allocator>
    requires(sizeof(BitsetBlock) % sizeof(Block) == 0)
TreeBitsetIndex<Block> insert_bitset(TreeBitsetSet<Block, FirstSegmentSize, ThreadSafe, Arity>& set,
                                     const boost::dynamic_bitset<BitsetBlock, Allocator>& bits,
                                     std::vector<Block>& blocks)
{
    blocks.clear();
    append_blocks(bits, blocks);
    trim_trailing_zero_blocks(blocks);
    return set.insert(blocks);
}

/// Inserts `bits` into `set` as a delta to the stored `parent`, whose bits are `parent_bits`. Linear in the bitset
/// size; when the changed bits are known, inserting their flips directly is cheaper. `blocks` and `flips` are scratch space.
template<std::unsigned_integral Block, size_t FirstSegmentSize, bool ThreadSafe, size_t Arity, std::unsigned_integral BitsetBlock, typename Allocator>
    requires(sizeof(BitsetBlock) % sizeof(Block) == 0)
TreeBitsetIndex<Block> insert_bitset(TreeBitsetSet<Block, FirstSegmentSize, ThreadSafe, Arity>& set,
                                     TreeBitsetIndex<Block> parent,
                                     const boost::dynamic_bitset<BitsetBlock, Allocator>& parent_bits,
                                     const boost::dynamic_bitset<BitsetBlock, Allocator>& bits,
                                     std::vector<Block>& blocks,
                                     std::vector<TreeBitsetFlip<Block>>& flips)
{
    blocks.clear();
    append_blocks(parent_bits, blocks);
    const auto num_parent_blocks = blocks.size();
    append_blocks(bits, blocks);

    flips.clear();
    append_diff_flips<Block>(std::span(blocks).first(num_parent_blocks), std::span(blocks).subspan(num_parent_blocks), flips);
    return set.insert(parent, flips);
}

/// Reads `index` from `set` into `bits`, which then ends at its last set bit. `blocks` is scratch space.
template<std::unsigned_integral Block, size_t FirstSegmentSize, bool ThreadSafe, size_t Arity, std::unsigned_integral BitsetBlock, typename Allocator>
    requires(sizeof(BitsetBlock) % sizeof(Block) == 0)
void read_bitset(const TreeBitsetSet<Block, FirstSegmentSize, ThreadSafe, Arity>& set,
                 TreeBitsetIndex<Block> index,
                 boost::dynamic_bitset<BitsetBlock, Allocator>& bits,
                 std::vector<Block>& blocks)
{
    blocks.resize(index.size);
    set.read(index, blocks);
    assign_blocks<Block>(blocks, bits);
    if (!blocks.empty())
        bits.resize((blocks.size() - 1) * std::numeric_limits<Block>::digits + std::bit_width(blocks.back()));
}

}  // namespace ygg

#endif
