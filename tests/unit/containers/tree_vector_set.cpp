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

#include "yggdrasil/containers/tree_vector_set.hpp"

#include <array>
#include <atomic>
#include <barrier>
#include <bit>
#include <cstddef>
#include <gtest/gtest.h>
#include <limits>
#include <span>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <vector>

namespace ygg::tests
{
namespace
{

struct CustomLeaf
{
    uint_t first;
    uint_t second;

    friend bool operator==(const CustomLeaf&, const CustomLeaf&) = default;
    auto identifying_members() const noexcept { return std::tie(first, second); }
};

static_assert(Identifiable<TreeVectorIndex<uint_t>>);
static_assert(std::three_way_comparable<TreeVectorIndex<uint_t>>);

TEST(YggdrasilTests, CommonTreeVectorSetRoundTripsAndSharesTrees)
{
    {
        auto singleton_set = TreeVectorSet<uint_t> {};
        constexpr auto singleton = std::array<uint_t, 1> { 7 };
        const auto singleton_index = singleton_set.insert(singleton);
        auto singleton_output = std::array<uint_t, 1> {};
        singleton_set.read(singleton_index, singleton_output);
        EXPECT_EQ(singleton_output, singleton);
        EXPECT_EQ(singleton_index.root, singleton.front());
        EXPECT_EQ(singleton_set.num_leaves(), 0);
        EXPECT_EQ(singleton_set.num_nodes(), 0);
    }

    auto set = TreeVectorSet<uint_t> {};

    const auto empty = set.insert(std::span<const uint_t> {});
    EXPECT_EQ(empty, TreeVectorIndex<uint_t> {});
    EXPECT_TRUE(set.empty());
    set.read(empty, std::span<uint_t> {});

    constexpr auto repeated = std::array<uint_t, 4> { 1, 2, 1, 2 };
    const auto repeated_index = set.insert(repeated);
    EXPECT_EQ(repeated_index.size, repeated.size());
    EXPECT_EQ(set.num_leaves(), 0);
    EXPECT_EQ(set.num_nodes(), 2);

    auto output = std::array<uint_t, repeated.size()> {};
    set.read(repeated_index, output);
    EXPECT_EQ(output, repeated);

    constexpr auto shared_subtree = std::array<uint_t, 2> { 1, 2 };
    const auto shared_subtree_index = set.insert(shared_subtree);
    auto shared_subtree_output = std::array<uint_t, shared_subtree.size()> {};
    set.read(shared_subtree_index, shared_subtree_output);
    EXPECT_EQ(shared_subtree_output, shared_subtree);
    EXPECT_EQ(set.num_leaves(), 0);
    EXPECT_EQ(set.num_nodes(), 2);

    EXPECT_EQ(set.insert(repeated), repeated_index);
    EXPECT_EQ(set.insert(shared_subtree), shared_subtree_index);
    EXPECT_THROW(set.read(repeated_index, std::span<uint_t>(output).first(3)), std::invalid_argument);

    set.clear();
    EXPECT_TRUE(set.empty());
    EXPECT_EQ(set.num_leaves(), 0);
    EXPECT_EQ(set.num_nodes(), 0);
}

TEST(YggdrasilTests, CommonTreeVectorSetSupportsDifferentLeafTypesAndLengths)
{
    auto float_set = TreeVectorSet<double> {};
    constexpr auto floats = std::array<double, 5> { 1.5, 2.5, 3.5, 4.5, 5.5 };
    const auto float_index = float_set.insert(floats);
    auto float_output = std::array<double, floats.size()> {};
    float_set.read(float_index, float_output);
    EXPECT_EQ(float_output, floats);
    EXPECT_EQ(float_set.num_leaves(), floats.size());

    auto custom_set = TreeVectorSet<CustomLeaf> {};
    constexpr auto custom = std::array<CustomLeaf, 3> { CustomLeaf { 1, 2 }, CustomLeaf { 3, 4 }, CustomLeaf { 5, 6 } };
    const auto custom_index = custom_set.insert(custom);
    auto custom_output = std::array<CustomLeaf, custom.size()> {};
    custom_set.read(custom_index, custom_output);
    EXPECT_EQ(custom_output, custom);
    EXPECT_EQ(custom_set.num_leaves(), custom.size());
}

TEST(YggdrasilTests, CommonTreeVectorSetStoresUintLeavesInline)
{
    auto set = TreeVectorSet<uint_t> {};

    // Values coinciding with node indices and the full value range must round-trip.
    constexpr auto max = std::numeric_limits<uint_t>::max();
    constexpr auto vectors = std::array {
        std::array<uint_t, 5> { 0, 1, 0, 1, 2 },
        std::array<uint_t, 5> { 0, max, 0, max, 0 },
        std::array<uint_t, 5> { 1, 0, 2, 1, 0 },
    };
    auto indices = std::array<TreeVectorIndex<uint_t>, vectors.size()> {};
    for (size_t i = 0; i < vectors.size(); ++i)
    {
        indices[i] = set.insert(vectors[i]);
        auto output = std::array<uint_t, 5> {};
        set.read(indices[i], output);
        EXPECT_EQ(output, vectors[i]);
    }
    for (size_t i = 0; i < vectors.size(); ++i)
    {
        EXPECT_EQ(set.insert(vectors[i]), indices[i]);
        for (size_t j = i + 1; j < vectors.size(); ++j)
            EXPECT_NE(indices[i], indices[j]);
    }

    for (const auto value : { uint_t { 0 }, uint_t { 1 }, max })
    {
        const auto single = std::array<uint_t, 1> { value };
        const auto index = set.insert(single);
        EXPECT_EQ(index, (TreeVectorIndex<uint_t> { value, 1 }));
        auto output = std::array<uint_t, 1> {};
        set.read(index, output);
        EXPECT_EQ(output, single);
    }

    EXPECT_EQ(set.num_leaves(), 0);
}

TEST(YggdrasilTests, CommonTreeVectorSetWritesMatchFullInsert)
{
    const auto expect_writes = [](auto set, auto values, auto writes)
    {
        using Value = typename decltype(values)::value_type;
        const auto base = set.insert(values);
        const auto nodes_before = set.num_nodes();
        const auto written = set.insert(base, std::span(writes));
        for (const auto& write : writes)
            values[write.position] = write.value;
        auto output = std::vector<Value>(values.size());
        set.read(written, output);
        EXPECT_EQ(output, values);
        EXPECT_EQ(written, set.insert(values));
        EXPECT_LE(set.num_nodes() - nodes_before, writes.size() * std::bit_width(values.size()));
    };

    // Inline leaves, a value equal to its position's old value, and the last position.
    expect_writes(TreeVectorSet<uint_t> {},
                  std::vector<uint_t> { 1, 2, 3, 4, 5, 6, 7 },
                  std::vector<TreeVectorWrite<uint_t>> { { 0, 9 }, { 3, 4 }, { 6, 0 } });
    // Leaf table leaves.
    expect_writes(TreeVectorSet<double> {},
                  std::vector<double> { 1.5, 2.5, 3.5, 4.5, 5.5 },
                  std::vector<TreeVectorWrite<double>> { { 1, -2.0 }, { 4, 8.25 } });
    // No writes and single-value vectors.
    expect_writes(TreeVectorSet<uint_t> {}, std::vector<uint_t> { 1, 2, 3 }, std::vector<TreeVectorWrite<uint_t>> {});
    expect_writes(TreeVectorSet<double> {}, std::vector<double> { 1.0 }, std::vector<TreeVectorWrite<double>> { { 0, 3.0 } });
}

TEST(YggdrasilTests, CommonTreeVectorSetSupportsConcurrentInsertionAndReads)
{
    using Set = TreeVectorSet<uint_t, 1, true>;
    static_assert(Set::thread_safe);

    constexpr auto thread_count = size_t { 8 };
    constexpr auto sequence_count = size_t { 64 };
    constexpr auto sequence_size = size_t { 8 };
    constexpr auto published = std::array<uint_t, sequence_size> { 10000, 10001, 10002, 10003, 10004, 10005, 10006, 10007 };

    auto set = Set {};
    const auto published_handle = set.insert(published);
    auto start = std::barrier<>(static_cast<std::ptrdiff_t>(thread_count));
    auto handles = std::vector(thread_count, std::vector<typename Set::index_type>(sequence_count));
    auto errors = std::atomic_size_t { 0 };
    auto threads = std::vector<std::jthread> {};
    threads.reserve(thread_count);

    for (size_t thread = 0; thread < thread_count; ++thread)
    {
        threads.emplace_back(
            [&, thread]
            {
                start.arrive_and_wait();
                for (size_t sequence = 0; sequence < sequence_count; ++sequence)
                {
                    auto input = std::array<uint_t, sequence_size> {};
                    for (size_t offset = 0; offset < sequence_size; ++offset)
                        input[offset] = static_cast<uint_t>(sequence * sequence_size + offset);

                    const auto handle = set.insert(input);
                    handles[thread][sequence] = handle;

                    auto output = std::array<uint_t, sequence_size> {};
                    set.read(handle, output);
                    if (output != input)
                        errors.fetch_add(1, std::memory_order_relaxed);

                    set.read(published_handle, output);
                    if (output != published)
                        errors.fetch_add(1, std::memory_order_relaxed);
                }
            });
    }

    threads.clear();
    EXPECT_EQ(errors.load(std::memory_order_relaxed), 0);
    EXPECT_EQ(set.num_leaves(), 0);
    for (size_t sequence = 0; sequence < sequence_count; ++sequence)
        for (size_t thread = 1; thread < thread_count; ++thread)
            EXPECT_EQ(handles[thread][sequence], handles[0][sequence]);
}

}  // namespace
}  // namespace ygg::tests
