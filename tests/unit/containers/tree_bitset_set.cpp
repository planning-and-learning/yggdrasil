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

#include <array>
#include <atomic>
#include <barrier>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <map>
#include <optional>
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

using Block = std::uint64_t;

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

template<typename Set>
std::map<uint_t, Block> visit_all(const Set& set, typename Set::index_type index)
{
    auto words = std::map<uint_t, Block> {};
    auto last = std::optional<uint_t> {};
    set.for_each_block(index,
                       [&](uint_t position, Block block)
                       {
                           EXPECT_NE(block, Block { 0 });
                           EXPECT_TRUE(!last || *last < position);
                           last = position;
                           words.emplace(position, block);
                       });
    return words;
}

TEST(YggdrasilTests, CommonTreeBitsetSetRoundTripsAndSkipsZeroBlocks)
{
    auto set = TreeBitsetSet<Block> {};

    const auto empty = set.insert(std::span<const Block> {});
    EXPECT_EQ(empty, TreeBitsetIndex<Block> {});
    EXPECT_TRUE(set.empty());
    set.read(empty, std::span<Block> {});
    EXPECT_TRUE(visit_all(set, empty).empty());

    constexpr auto single = std::array<Block, 1> { 0b1011 };
    const auto single_index = set.insert(single);
    EXPECT_EQ(single_index.size, 1);
    EXPECT_EQ(read_all(set, single_index), std::vector<Block>(single.begin(), single.end()));
    EXPECT_EQ(set.num_leaves(), 1);
    EXPECT_EQ(set.num_nodes(), 0);

    // Leading zero blocks and zero subtrees are not stored.
    constexpr auto sparse = std::array<Block, 8> { 0, 0, 0, 0, 0, 0, 0, 7 };
    const auto sparse_index = set.insert(sparse);
    EXPECT_EQ(read_all(set, sparse_index), std::vector<Block>(sparse.begin(), sparse.end()));
    EXPECT_EQ(visit_all(set, sparse_index), (std::map<uint_t, Block> { { 7, 7 } }));
    EXPECT_EQ(set.num_leaves(), 2);
    // Like in TreeVectorSet, node records are shared across heights: (Z, leaf 1) at height 1 equals (Z, node 1) at height 3.
    EXPECT_EQ(set.num_nodes(), 2);

    auto far = std::vector<Block>(size_t { 1 } << 20);
    far.front() = 1;
    far.back() = Block { 1 } << 63;
    const auto far_index = set.insert(far);
    EXPECT_EQ(read_all(set, far_index), far);
    EXPECT_EQ(visit_all(set, far_index), (std::map<uint_t, Block> { { 0, 1 }, { (1u << 20) - 1, Block { 1 } << 63 } }));

    EXPECT_EQ(set.insert(single), single_index);
    EXPECT_EQ(set.insert(sparse), sparse_index);
    EXPECT_THROW(set.insert(std::array<Block, 2> { 1, 0 }), std::invalid_argument);
    EXPECT_THROW(set.read(sparse_index, std::span<Block>(far).first(3)), std::invalid_argument);

    set.clear();
    EXPECT_TRUE(set.empty());
}

TEST(YggdrasilTests, CommonTreeBitsetSetDeltaInsertMatchesFullInsert)
{
    using Flip = TreeBitsetFlip<Block>;
    auto set = TreeBitsetSet<Block> {};

    const auto base = set.insert(std::array<Block, 5> { 1, 2, 3, 4, 5 });

    // Same size: only the path to block 2 is new.
    const auto nodes_before = set.num_nodes();
    const auto changed = set.insert(base, std::array<Flip, 1> { Flip { 2, 0b11 } }, 5);
    EXPECT_EQ(changed, set.insert(std::array<Block, 5> { 1, 2, 0, 4, 5 }));
    EXPECT_LE(set.num_nodes() - nodes_before, 3);

    // Growing and shrinking.
    const auto grown = set.insert(base, std::array<Flip, 1> { Flip { 9, 1 } }, 10);
    EXPECT_EQ(grown, set.insert(std::array<Block, 10> { 1, 2, 3, 4, 5, 0, 0, 0, 0, 1 }));
    const auto shrunk = set.insert(grown, std::array<Flip, 2> { Flip { 4, 5 }, Flip { 9, 1 } }, 4);
    EXPECT_EQ(shrunk, set.insert(std::array<Block, 4> { 1, 2, 3, 4 }));
    EXPECT_EQ(set.insert(shrunk, std::array<Flip, 1> { Flip { 4, 5 } }, 5), base);

    // No flips leaves the index unchanged; clearing everything gives the empty index.
    EXPECT_EQ(set.insert(base, std::span<const Flip> {}, 5), base);
    EXPECT_EQ(set.insert(base, std::array<Flip, 5> { Flip { 0, 1 }, Flip { 1, 2 }, Flip { 2, 3 }, Flip { 3, 4 }, Flip { 4, 5 } }, 0),
              TreeBitsetIndex<Block> {});
    EXPECT_EQ(set.insert(TreeBitsetIndex<Block> {}, std::array<Flip, 1> { Flip { 3, 8 } }, 4), set.insert(std::array<Block, 4> { 0, 0, 0, 8 }));
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
                    flips[position(rng)] ^= Block { 1 } << (rng() % 64);

                auto flip_list = std::vector<Flip> {};
                for (const auto& [p, mask] : flips)
                {
                    if (mask == 0)
                        continue;
                    flip_list.push_back(Flip { p, mask });
                    if ((words[p] ^= mask) == 0)
                        words.erase(p);
                }

                const auto blocks = to_blocks(words);
                const auto nodes_before = set.num_nodes();
                const auto height = blocks.empty() ? 0 : std::bit_width(blocks.size());
                index = set.insert(index, flip_list, to_uint_t(blocks.size()));

                // Changing a block and growing or shrinking each rebuild at most one path.
                EXPECT_LE(set.num_nodes() - nodes_before, (flip_list.size() + 2) * (height + 1));
                EXPECT_EQ(read_all(set, index), blocks);
                EXPECT_EQ(visit_all(set, index), words);
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
                    const auto flip = Flip { static_cast<uint_t>(step % (block_count - 1)), Block { 1 } << (step % 63 + 1) };
                    blocks[flip.position] ^= flip.mask;
                    index = set.insert(index, std::span<const Flip>(&flip, 1), block_count);
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

}  // namespace
}  // namespace ygg::tests
