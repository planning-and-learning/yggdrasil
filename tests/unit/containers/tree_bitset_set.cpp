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

#include "yggdrasil/containers/tree_bitset_set.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <bit>
#include <boost/dynamic_bitset.hpp>
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <limits>
#include <map>
#include <random>
#include <span>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace ygg::tests
{
namespace
{

using Block = std::uint32_t;

static_assert(Identifiable<TreeBitsetIndex<Block>>);
static_assert(std::three_way_comparable<TreeBitsetIndex<Block>>);

std::vector<Block> to_blocks(const std::map<uint_t, Block>& words)
{
    auto blocks = std::vector<Block>(words.empty() ? 0 : words.rbegin()->first + 1);
    for (const auto& [position, block] : words)
        blocks[position] = block;
    return blocks;
}

template<typename Set>
std::vector<Block> read_all(const Set& set, typename Set::index_type index)
{
    auto blocks = std::vector<Block>(index.size);
    set.read(index, blocks);
    return blocks;
}

TEST(YggdrasilTests, CommonTreeBitsetSetHelpersPrepareFlipsAndBlocks)
{
    using Flip = TreeBitsetFlip<Block>;

    // Flips of the same block combine by XOR; flips that cancel out disappear.
    auto flips = std::vector<Flip> { Flip { 5, 1 }, Flip { 2, 4 }, Flip { 5, 2 }, Flip { 2, 4 }, Flip { 0, 8 } };
    normalize_flips(flips);
    EXPECT_EQ(flips, (std::vector<Flip> { Flip { 0, 8 }, Flip { 5, 3 } }));

    // A 6-bit integer at bit 30 of block 1 spans blocks 1 and 2.
    auto int_flips = std::vector<Flip> {};
    append_int_flips<Block>(1, 30, 6, Block { 0b101101 }, int_flips);
    EXPECT_EQ(int_flips, (std::vector<Flip> { Flip { 1, Block { 0b01 } << 30 }, Flip { 2, Block { 0b1011 } } }));

    auto diff = std::vector<Flip> {};
    append_diff_flips<Block>(std::vector<Block> { 1, 2, 3 }, std::vector<Block> { 1, 6 }, diff);
    EXPECT_EQ(diff, (std::vector<Flip> { Flip { 1, 4 }, Flip { 2, 3 } }));

    // 64-bit dynamic_bitset blocks split into 32-bit blocks and back.
    auto bits = boost::dynamic_bitset<std::uint64_t> {};
    bits.append(std::uint64_t { 0x0000000500000003 });
    bits.append(std::uint64_t { 0x0000000000000007 });
    auto blocks = std::vector<Block> {};
    append_blocks(bits, blocks);
    EXPECT_EQ(blocks, (std::vector<Block> { 3, 5, 7, 0 }));
    trim_trailing_zero_blocks(blocks);
    EXPECT_EQ(blocks, (std::vector<Block> { 3, 5, 7 }));

    auto restored = boost::dynamic_bitset<std::uint64_t> {};
    assign_blocks<Block>(blocks, restored);
    EXPECT_EQ(restored, bits);
}

TEST(YggdrasilTests, CommonTreeBitsetSetRoundTripsAndSkipsZeroBlocks)
{
    auto set = TreeBitsetSet<Block> {};

    const auto empty = set.insert(std::span<const Block> {});
    EXPECT_EQ(empty, TreeBitsetIndex<Block> {});
    EXPECT_TRUE(set.empty());
    set.read(empty, std::span<Block> {});

    constexpr auto single = std::array<Block, 1> { 0b1011 };
    const auto single_index = set.insert(single);
    EXPECT_EQ(single_index.size, 1);
    EXPECT_EQ(read_all(set, single_index), std::vector<Block>(single.begin(), single.end()));
    EXPECT_EQ(single_index.root, single.front());  // Blocks are stored inline.
    EXPECT_EQ(set.num_nodes(), 0);

    // Leading zero blocks and zero subtrees are not stored.
    constexpr auto sparse = std::array<Block, 8> { 0, 0, 0, 0, 0, 0, 0, 7 };
    const auto sparse_index = set.insert(sparse);
    EXPECT_EQ(read_all(set, sparse_index), std::vector<Block>(sparse.begin(), sparse.end()));
    EXPECT_EQ(set.num_nodes(), 3);

    auto far = std::vector<Block>(size_t { 1 } << 20);
    far.front() = 1;
    far.back() = Block { 1 } << 31;
    const auto far_index = set.insert(far);
    EXPECT_EQ(read_all(set, far_index), far);

    EXPECT_EQ(set.insert(single), single_index);
    EXPECT_EQ(set.insert(sparse), sparse_index);
    EXPECT_THROW(set.insert(std::array<Block, 2> { 1, 0 }), std::invalid_argument);
    EXPECT_THROW(set.read(sparse_index, std::span<Block>(far).first(3)), std::invalid_argument);

    set.clear();
    EXPECT_TRUE(set.empty());
}

TEST(YggdrasilTests, CommonTreeBitsetSetStoresWideBlocksInLeafTable)
{
    using WideBlock = std::uint64_t;
    using Flip = TreeBitsetFlip<WideBlock>;
    auto set = TreeBitsetSet<WideBlock> {};

    const auto blocks = std::vector<WideBlock> { 1, 0, ~WideBlock { 0 }, WideBlock { 1 } << 63 };
    const auto index = set.insert(blocks);
    auto output = std::vector<WideBlock>(blocks.size());
    set.read(index, output);
    EXPECT_EQ(output, blocks);
    EXPECT_EQ(set.num_leaves(), 3);  // Wide blocks do not fit into a child slot; the zero block is not stored.

    const auto changed = set.insert(index, std::array<Flip, 2> { Flip { 1, 5 }, Flip { 3, WideBlock { 1 } << 63 } });
    EXPECT_EQ(changed, set.insert(std::vector<WideBlock> { 1, 5, ~WideBlock { 0 } }));
}

TEST(YggdrasilTests, CommonTreeBitsetSetStoresSlotWideBlocksInline)
{
    auto set = TreeBitsetSet<Block> {};
    set.insert(std::vector<Block> { 1, 2, 3, 4, 5 });
    EXPECT_EQ(set.num_leaves(), 0);
}

TEST(YggdrasilTests, CommonTreeBitsetSetDistinguishesAllOnesBlocksFromZeroSubtrees)
{
    using Flip = TreeBitsetFlip<Block>;
    auto set = TreeBitsetSet<Block> {};

    // An all-ones block has the bit pattern of the zero-subtree sentinel, but leaves are read by position.
    constexpr auto ones = ~Block { 0 };
    for (const auto& blocks :
         { std::vector<Block> { ones }, std::vector<Block> { ones, ones }, std::vector<Block> { 0, ones }, std::vector<Block> { ones, 0, 0, ones } })
    {
        const auto index = set.insert(blocks);
        EXPECT_EQ(read_all(set, index), blocks);
    }

    const auto base = set.insert(std::vector<Block> { 1, 2, 3 });
    const auto flipped = set.insert(base, std::array<Flip, 2> { Flip { 0, ones ^ 1 }, Flip { 2, 3 } });
    EXPECT_EQ(flipped, set.insert(std::vector<Block> { ones, 2 }));
    EXPECT_EQ(set.insert(flipped, std::array<Flip, 1> { Flip { 0, ones } }), set.insert(std::vector<Block> { 0, 2 }));
}

TEST(YggdrasilTests, CommonTreeBitsetSetDeltaInsertMatchesFullInsert)
{
    using Flip = TreeBitsetFlip<Block>;
    auto set = TreeBitsetSet<Block> {};

    const auto base = set.insert(std::array<Block, 5> { 1, 2, 3, 4, 5 });

    // Same size: only the path to block 2 is new.
    const auto nodes_before = set.num_nodes();
    const auto changed = set.insert(base, std::array<Flip, 1> { Flip { 2, 0b11 } });
    EXPECT_EQ(changed, set.insert(std::array<Block, 5> { 1, 2, 0, 4, 5 }));
    EXPECT_LE(set.num_nodes() - nodes_before, 3);

    // Growing and shrinking.
    const auto grown = set.insert(base, std::array<Flip, 1> { Flip { 9, 1 } });
    EXPECT_EQ(grown, set.insert(std::array<Block, 10> { 1, 2, 3, 4, 5, 0, 0, 0, 0, 1 }));
    const auto shrunk = set.insert(grown, std::array<Flip, 2> { Flip { 4, 5 }, Flip { 9, 1 } });
    EXPECT_EQ(shrunk, set.insert(std::array<Block, 4> { 1, 2, 3, 4 }));
    EXPECT_EQ(set.insert(shrunk, std::array<Flip, 1> { Flip { 4, 5 } }), base);

    // No flips leaves the index unchanged; clearing everything gives the empty index.
    EXPECT_EQ(set.insert(base, std::span<const Flip> {}), base);
    EXPECT_EQ(set.insert(base, std::array<Flip, 5> { Flip { 0, 1 }, Flip { 1, 2 }, Flip { 2, 3 }, Flip { 3, 4 }, Flip { 4, 5 } }), TreeBitsetIndex<Block> {});
    EXPECT_EQ(set.insert(TreeBitsetIndex<Block> {}, std::array<Flip, 1> { Flip { 3, 8 } }), set.insert(std::array<Block, 4> { 0, 0, 0, 8 }));
}

TEST(YggdrasilTests, CommonTreeBitsetSetDeltaInsertAcrossPowerOfTwoSizes)
{
    using Flip = TreeBitsetFlip<Block>;
    auto set = TreeBitsetSet<Block> {};

    // Sizes just below, at, and above powers of two, where the shapes of base and result differ most.
    for (const uint_t from : { 1u, 2u, 7u, 64u, 65u, 511u, 512u, 513u, 1000u })
    {
        for (const uint_t to : { 1u, 3u, 63u, 64u, 65u, 257u, 512u, 513u, 1500u })
        {
            auto base_blocks = std::vector<Block>(from);
            for (uint_t i = 0; i < from; ++i)
                base_blocks[i] = static_cast<Block>(i % 3 == 0 ? i + 1 : 0);
            base_blocks.back() = 1;
            const auto base = set.insert(base_blocks);

            // Set the new last block, clear the old last block if it lies beyond, and change one block in between.
            auto blocks = base_blocks;
            blocks.resize(std::max(from, to), Block { 0 });
            auto flips = std::vector<Flip> {};
            const auto middle = std::min(from, to) / 2;
            flips.push_back(Flip { middle, Block { 0b100 } });
            if (to - 1 != middle && blocks[to - 1] == 0)
                flips.push_back(Flip { to - 1, Block { 1 } });
            for (uint_t i = to; i < from; ++i)
                if (blocks[i] != 0)
                    flips.push_back(Flip { i, blocks[i] });
            std::ranges::sort(flips, {}, &Flip::position);
            for (const auto& flip : flips)
                blocks[flip.position] ^= flip.mask;
            while (!blocks.empty() && blocks.back() == 0)
                blocks.pop_back();

            EXPECT_EQ(set.insert(base, flips), set.insert(blocks)) << "from " << from << " to " << to;
        }
    }
}

TEST(YggdrasilTests, CommonTreeBitsetSetRandomDeltaChainsMatchFullInsert)
{
    using Flip = TreeBitsetFlip<Block>;

    auto rng = std::mt19937_64 { 42 };
    auto set = TreeBitsetSet<Block> {};
    auto reference = TreeBitsetSet<Block> {};

    for (const uint_t universe : { 1u, 3u, 64u, 200u, 5000u })
    {
        for (size_t chain = 0; chain < 20; ++chain)
        {
            auto words = std::map<uint_t, Block> {};
            auto index = TreeBitsetIndex<Block> {};
            auto position = std::uniform_int_distribution<uint_t>(0, universe - 1);
            auto num_flips = std::uniform_int_distribution<size_t>(0, 6);

            for (size_t step = 0; step < 50; ++step)
            {
                auto flips = std::map<uint_t, Block> {};
                for (size_t k = num_flips(rng); k > 0; --k)
                    flips[position(rng)] ^= Block { 1 } << (rng() % 32);

                auto flip_list = std::vector<Flip> {};
                for (const auto& [p, mask] : flips)
                {
                    if (mask == 0)
                        continue;
                    flip_list.push_back(Flip { p, mask });
                    words[p] ^= mask;
                    if (words[p] == 0)
                        words.erase(p);
                }

                const auto blocks = to_blocks(words);
                const auto nodes_before = set.num_nodes();
                const auto height = blocks.empty() ? 0 : std::bit_width(blocks.size());
                index = set.insert(index, flip_list);

                // Changing a block and growing or shrinking each rebuild at most one path.
                EXPECT_LE(set.num_nodes() - nodes_before, (flip_list.size() + 2) * (height + 1));
                EXPECT_EQ(read_all(set, index), blocks);
                EXPECT_EQ(index, set.insert(blocks));
                EXPECT_EQ(read_all(reference, reference.insert(blocks)), blocks);
            }
        }
    }
}

TEST(YggdrasilTests, CommonTreeBitsetSetSupportsConcurrentDeltaInsertionAndReads)
{
    using Set = TreeBitsetSet<Block, 1, true>;
    using Flip = TreeBitsetFlip<Block>;
    static_assert(Set::thread_safe);

    constexpr auto thread_count = size_t { 8 };
    constexpr auto step_count = size_t { 256 };
    constexpr auto block_count = uint_t { 32 };

    auto set = Set {};
    auto published_blocks = std::vector<Block>(block_count, 1);
    const auto published = set.insert(published_blocks);
    auto start = std::barrier<>(static_cast<std::ptrdiff_t>(thread_count));
    auto handles = std::vector(thread_count, std::vector<typename Set::index_type>(step_count));
    auto errors = std::atomic_size_t { 0 };
    auto threads = std::vector<std::jthread> {};
    threads.reserve(thread_count);

    for (size_t thread = 0; thread < thread_count; ++thread)
    {
        threads.emplace_back(
            [&, thread]
            {
                start.arrive_and_wait();
                auto blocks = published_blocks;
                auto index = published;
                for (size_t step = 0; step < step_count; ++step)
                {
                    const auto flip = Flip { static_cast<uint_t>(step % (block_count - 1)), Block { 1 } << (step % 31 + 1) };
                    blocks[flip.position] ^= flip.mask;
                    index = set.insert(index, std::span<const Flip>(&flip, 1));
                    handles[thread][step] = index;

                    auto output = std::vector<Block>(block_count);
                    set.read(index, output);
                    if (output != blocks)
                        errors.fetch_add(1, std::memory_order_relaxed);
                }
            });
    }

    threads.clear();
    EXPECT_EQ(errors.load(std::memory_order_relaxed), 0);
    for (size_t step = 0; step < step_count; ++step)
        for (size_t thread = 1; thread < thread_count; ++thread)
            EXPECT_EQ(handles[thread][step], handles[0][step]);
}

/// Sizes 1, Arity +- 1, Arity^2 +- 1 and Arity^3 +- 1 have unused child slots on the right edge.
template<size_t Arity>
std::vector<uint_t> arity_sizes()
{
    auto sizes = std::vector<uint_t> { 1, 2 };
    for (uint_t power = Arity; power <= Arity * Arity * Arity; power *= Arity)
        for (const auto size : { power - 1, power, power + 1 })
            sizes.push_back(size);
    return sizes;
}

template<std::unsigned_integral TestBlock, size_t Arity>
void expect_arity_round_trips_and_canonical_trees()
{
    SCOPED_TRACE(testing::Message() << "Arity " << Arity << ", " << sizeof(TestBlock) << "-byte blocks");
    auto rng = std::mt19937_64 { Arity };
    auto set = TreeBitsetSet<TestBlock, 32, false, Arity> {};
    for (const auto size : arity_sizes<Arity>())
    {
        // Sparse blocks, so that zero subtrees occur at every level.
        auto blocks = std::vector<TestBlock>(size);
        for (auto& block : blocks)
            block = (rng() % 3 == 0) ? TestBlock(rng() | 1) : TestBlock { 0 };
        blocks.back() = TestBlock { 1 };

        const auto index = set.insert(blocks);
        auto output = std::vector<TestBlock>(size);
        set.read(index, output);
        EXPECT_EQ(output, blocks);
        EXPECT_EQ(set.insert(blocks), index);

        // Building the same bitset from the empty bitset by flips gives the same tree.
        auto flips = std::vector<TreeBitsetFlip<TestBlock>> {};
        for (uint_t i = 0; i < size; ++i)
            if (blocks[i] != TestBlock { 0 })
                flips.push_back(TreeBitsetFlip<TestBlock> { i, blocks[i] });
        EXPECT_EQ(set.insert(TreeBitsetIndex<TestBlock> {}, flips), index);
    }
}

TEST(YggdrasilTests, CommonTreeBitsetSetRoundTripsAndCanonicalTreesForArities)
{
    expect_arity_round_trips_and_canonical_trees<Block, 2>();
    expect_arity_round_trips_and_canonical_trees<Block, 4>();
    expect_arity_round_trips_and_canonical_trees<Block, 8>();
    expect_arity_round_trips_and_canonical_trees<std::uint64_t, 4>();  // Leaf table blocks.
}

template<std::unsigned_integral TestBlock, size_t Arity>
void expect_random_delta_chains_match_full_insert()
{
    SCOPED_TRACE(testing::Message() << "Arity " << Arity << ", " << sizeof(TestBlock) << "-byte blocks");
    using Flip = TreeBitsetFlip<TestBlock>;

    auto rng = std::mt19937_64 { 7 * Arity };
    auto set = TreeBitsetSet<TestBlock, 32, false, Arity> {};

    for (const uint_t universe : { 1u, 3u, static_cast<uint_t>(Arity + 1), 200u, 5000u })
    {
        for (size_t chain = 0; chain < 10; ++chain)
        {
            auto words = std::map<uint_t, TestBlock> {};
            auto index = TreeBitsetIndex<TestBlock> {};
            auto position = std::uniform_int_distribution<uint_t>(0, universe - 1);
            auto num_flips = std::uniform_int_distribution<size_t>(0, 6);

            for (size_t step = 0; step < 40; ++step)
            {
                auto flips = std::map<uint_t, TestBlock> {};
                for (size_t k = num_flips(rng); k > 0; --k)
                    flips[position(rng)] ^= TestBlock { 1 } << (rng() % std::numeric_limits<TestBlock>::digits);

                auto flip_list = std::vector<Flip> {};
                for (const auto& [p, mask] : flips)
                {
                    if (mask == 0)
                        continue;
                    flip_list.push_back(Flip { p, mask });
                    words[p] ^= mask;
                    if (words[p] == 0)
                        words.erase(p);
                }

                auto blocks = std::vector<TestBlock>(words.empty() ? 0 : words.rbegin()->first + 1);
                for (const auto& [p, block] : words)
                    blocks[p] = block;

                const auto nodes_before = set.num_nodes();
                const auto levels = blocks.empty() ? 0 : (std::bit_width(blocks.size()) + std::countr_zero(Arity) - 1) / std::countr_zero(Arity) + 1;
                index = set.insert(index, flip_list);

                // Changing a block and growing or shrinking each rebuild at most one path.
                EXPECT_LE(set.num_nodes() - nodes_before, (flip_list.size() + 2) * (levels + 1));
                auto output = std::vector<TestBlock>(index.size);
                set.read(index, output);
                EXPECT_EQ(output, blocks);
                EXPECT_EQ(index, set.insert(blocks));
            }
        }
    }
}

TEST(YggdrasilTests, CommonTreeBitsetSetRandomDeltaChainsMatchFullInsertForArities)
{
    expect_random_delta_chains_match_full_insert<Block, 2>();
    expect_random_delta_chains_match_full_insert<Block, 4>();
    expect_random_delta_chains_match_full_insert<Block, 8>();
    expect_random_delta_chains_match_full_insert<std::uint64_t, 4>();  // Leaf table blocks.
}

TEST(YggdrasilTests, CommonTreeBitsetSetSupportsConcurrentDeltaInsertionForArity4)
{
    using Set = TreeBitsetSet<Block, 1, true, 4>;
    using Flip = TreeBitsetFlip<Block>;

    constexpr auto thread_count = size_t { 8 };
    constexpr auto step_count = size_t { 128 };
    constexpr auto block_count = uint_t { 37 };

    auto set = Set {};
    const auto published_blocks = std::vector<Block>(block_count, 1);
    const auto published = set.insert(published_blocks);
    auto start = std::barrier<>(static_cast<std::ptrdiff_t>(thread_count));
    auto handles = std::vector(thread_count, std::vector<typename Set::index_type>(step_count));
    auto errors = std::atomic_size_t { 0 };
    auto threads = std::vector<std::jthread> {};
    threads.reserve(thread_count);

    for (size_t thread = 0; thread < thread_count; ++thread)
    {
        threads.emplace_back(
            [&, thread]
            {
                start.arrive_and_wait();
                auto blocks = published_blocks;
                auto index = published;
                for (size_t step = 0; step < step_count; ++step)
                {
                    const auto flip = Flip { static_cast<uint_t>(step % (block_count - 1)), Block { 1 } << (step % 31 + 1) };
                    blocks[flip.position] ^= flip.mask;
                    index = set.insert(index, std::span<const Flip>(&flip, 1));
                    handles[thread][step] = index;

                    auto output = std::vector<Block>(block_count);
                    set.read(index, output);
                    if (output != blocks)
                        errors.fetch_add(1, std::memory_order_relaxed);
                }
            });
    }

    threads.clear();
    EXPECT_EQ(errors.load(std::memory_order_relaxed), 0);
    for (size_t step = 0; step < step_count; ++step)
        for (size_t thread = 1; thread < thread_count; ++thread)
            EXPECT_EQ(handles[thread][step], handles[0][step]);
}

/// A random bitset of `num_bits` bits whose last bit is set, i.e., that ends at its last set bit.
template<std::unsigned_integral BitsetBlock>
boost::dynamic_bitset<BitsetBlock> random_trimmed_bitset(std::mt19937_64& rng, size_t num_bits)
{
    auto bits = boost::dynamic_bitset<BitsetBlock>(num_bits);
    for (size_t i = 0; i < num_bits; ++i)
        bits[i] = rng() % 4 == 0;
    if (num_bits > 0)
        bits[num_bits - 1] = true;
    return bits;
}

template<std::unsigned_integral BitsetBlock, size_t Arity>
void expect_dynamic_bitset_adapters()
{
    SCOPED_TRACE(testing::Message() << "Arity " << Arity << ", " << sizeof(BitsetBlock) << "-byte bitset blocks");
    auto rng = std::mt19937_64 { 11 * Arity };
    auto set = TreeBitsetSet<Block, 32, false, Arity> {};
    auto blocks = std::vector<Block> {};
    auto flips = std::vector<TreeBitsetFlip<Block>> {};

    for (const size_t num_bits : { 0, 1, 31, 32, 33, 64, 65, 100, 1000, 5000 })
    {
        const auto bits = random_trimmed_bitset<BitsetBlock>(rng, num_bits);
        const auto index = insert_bitset(set, bits, blocks);
        auto read = boost::dynamic_bitset<BitsetBlock> {};
        read_bitset(set, index, read, blocks);
        EXPECT_EQ(read, bits);

        // A delta to any stored parent gives the same tree as a full insert.
        for (const size_t parent_bits : { 0, 7, 64, 3000 })
        {
            const auto parent = random_trimmed_bitset<BitsetBlock>(rng, parent_bits);
            const auto parent_index = insert_bitset(set, parent, blocks);
            EXPECT_EQ(insert_bitset(set, parent_index, parent, bits, blocks, flips), index);
        }
    }
}

TEST(YggdrasilTests, CommonTreeBitsetSetDynamicBitsetAdaptersRoundTrip)
{
    expect_dynamic_bitset_adapters<unsigned long, 2>();
    expect_dynamic_bitset_adapters<unsigned long, 4>();
    expect_dynamic_bitset_adapters<std::uint32_t, 4>();
}

}  // namespace
}  // namespace ygg::tests
