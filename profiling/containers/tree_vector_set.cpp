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

#include <array>
#include <benchmark/benchmark.h>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <valla/valla.hpp>
#include <vector>
#include <yggdrasil/containers/tree_vector_set.hpp>

namespace ygg::profiling
{
namespace
{

constexpr auto kVectorSize = size_t { 29 };
constexpr auto kInsertIterations = benchmark::IterationCount { 1U << 14 };
constexpr auto kReadIterations = benchmark::IterationCount { 1U << 18 };
constexpr auto kMixedIterations = benchmark::IterationCount { 1U << 17 };
constexpr auto kReadCorpusPerThread = size_t { 1U << 14 };
constexpr auto kMixedPrefill = size_t { 1U << 14 };
constexpr auto kMixedInsertPeriod = size_t { 20 };
constexpr auto kMixedInsertsPerThread = (static_cast<size_t>(kMixedIterations) + kMixedInsertPeriod - 1) / kMixedInsertPeriod;

// Valla's heterogeneous overloads are ambiguous when its leaf and index types
// are identical, so use a different integral width for the scalar-leaf baseline.
using Value = uint64_t;
using Vector = std::array<Value, kVectorSize>;
using SequentialSet = TreeVectorSet<Value>;
using ConcurrentSet = TreeVectorSet<Value, 32, true>;

class VallaSet
{
public:
    using index_type = valla::Slot<uint_t>;
    static constexpr bool thread_safe = false;

    index_type insert(std::span<const Value> values)
    {
        assert(values.size() == kVectorSize);
        valla::encode_as_unsigned_integrals(values, m_leaves, m_scratch.begin());
        return valla::insert_sequence(m_scratch, m_nodes);
    }

    void read(index_type index, std::span<Value> values)
    {
        assert(values.size() == kVectorSize);
        valla::read_sequence(index, m_nodes, m_scratch.begin());
        valla::decode_from_unsigned_integrals(m_scratch, m_leaves, values.begin());
    }

    size_t memory_usage() const noexcept { return m_leaves.memory_usage() + m_nodes.memory_usage(); }
    size_t num_leaves() const noexcept { return m_leaves.size(); }
    size_t num_nodes() const noexcept { return m_nodes.size(); }

private:
    valla::IndexedHashSet<Value, uint_t> m_leaves;
    valla::IndexedHashSet<valla::Slot<uint_t>, uint_t> m_nodes;
    std::array<uint_t, kVectorSize> m_scratch {};
};

std::vector<Vector> corpus;

template<typename Set>
std::unique_ptr<Set> tree_set;

template<typename Set>
std::vector<typename Set::index_type> handles;

void make_corpus(size_t size)
{
    corpus.resize(size);
    auto current = Vector {};
    for (size_t position = 0; position < current.size(); ++position)
        current[position] = static_cast<Value>(position);

    for (size_t sequence = 0; sequence < size; ++sequence)
    {
        const auto mutations = size_t { 1 } + sequence % 5;
        for (size_t mutation = 0; mutation < mutations; ++mutation)
        {
            const auto position = (sequence * 17 + mutation * 7) % current.size();
            current[position] = static_cast<Value>((sequence * 31 + mutation * 13) & 1023U);
        }
        corpus[sequence] = current;
    }
}

template<typename Set>
void set_up_insert(const benchmark::State& state)
{
    tree_set<Set> = std::make_unique<Set>();
    make_corpus(static_cast<size_t>(state.threads()) * static_cast<size_t>(kInsertIterations));
}

template<typename Set>
void set_up_read(const benchmark::State& state)
{
    tree_set<Set> = std::make_unique<Set>();
    make_corpus(static_cast<size_t>(state.threads()) * kReadCorpusPerThread);
    handles<Set>.clear();
    handles<Set>.reserve(corpus.size());
    for (const auto& values : corpus)
        handles<Set>.push_back(tree_set<Set>->insert(values));
}

template<typename Set>
void set_up_mixed(const benchmark::State& state)
{
    tree_set<Set> = std::make_unique<Set>();
    make_corpus(kMixedPrefill + static_cast<size_t>(state.threads()) * kMixedInsertsPerThread);
    handles<Set>.clear();
    handles<Set>.reserve(kMixedPrefill);
    for (size_t i = 0; i < kMixedPrefill; ++i)
        handles<Set>.push_back(tree_set<Set>->insert(corpus[i]));
}

template<typename Set>
void tear_down(const benchmark::State&)
{
    tree_set<Set>.reset();
    handles<Set>.clear();
    corpus.clear();
}

template<typename Set>
void report(benchmark::State& state)
{
    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() * static_cast<benchmark::IterationCount>(sizeof(Vector)));
    if constexpr (!Set::thread_safe)
    {
        state.counters["bytes"] = static_cast<double>(tree_set<Set>->memory_usage());
        state.counters["leaves"] = static_cast<double>(tree_set<Set>->num_leaves());
        state.counters["nodes"] = static_cast<double>(tree_set<Set>->num_nodes());
    }
}

template<typename Set>
void benchmark_insert(benchmark::State& state)
{
    auto position = static_cast<size_t>(state.thread_index()) * static_cast<size_t>(kInsertIterations);
    for (auto _ : state)
    {
        auto index = tree_set<Set>->insert(corpus[position++]);
        benchmark::DoNotOptimize(index);
    }
    report<Set>(state);
}

template<typename Set>
void benchmark_read(benchmark::State& state)
{
    const auto first = static_cast<size_t>(state.thread_index()) * kReadCorpusPerThread;
    auto operation = uint64_t { 0 };
    auto output = Vector {};
    for (auto _ : state)
    {
        const auto offset = static_cast<size_t>((operation++ * 11400714819323198485ULL) & (kReadCorpusPerThread - 1));
        tree_set<Set>->read(handles<Set>[first + offset], output);
        benchmark::DoNotOptimize(output);
        benchmark::ClobberMemory();
    }
    report<Set>(state);
}

template<typename Set>
void benchmark_mixed(benchmark::State& state)
{
    const auto first_insert = kMixedPrefill + static_cast<size_t>(state.thread_index()) * kMixedInsertsPerThread;
    const auto insert_phase = static_cast<size_t>(state.thread_index()) * kMixedInsertPeriod / static_cast<size_t>(state.threads());
    auto operation = size_t { 0 };
    auto inserted = size_t { 0 };
    auto output = Vector {};
    for (auto _ : state)
    {
        if ((operation + insert_phase) % kMixedInsertPeriod == 0)
        {
            auto index = tree_set<Set>->insert(corpus[first_insert + inserted++]);
            benchmark::DoNotOptimize(index);
        }
        else
        {
            const auto offset = (operation * 11400714819323198485ULL + static_cast<size_t>(state.thread_index())) & (kMixedPrefill - 1);
            tree_set<Set>->read(handles<Set>[offset], output);
            benchmark::DoNotOptimize(output);
            benchmark::ClobberMemory();
        }
        ++operation;
    }
    report<Set>(state);
}

void configure(benchmark::internal::Benchmark* benchmark, benchmark::IterationCount iterations, bool concurrent)
{
    benchmark->Iterations(iterations)->UseRealTime();
    if (concurrent)
        benchmark->Threads(1)->Threads(2)->Threads(4)->Threads(8);
    else
        benchmark->Threads(1);
}

template<typename Set>
void register_benchmarks(const char* prefix, bool concurrent)
{
    configure(benchmark::RegisterBenchmark(std::string(prefix) + "/insert", benchmark_insert<Set>)->Setup(set_up_insert<Set>)->Teardown(tear_down<Set>),
              kInsertIterations,
              concurrent);
    configure(benchmark::RegisterBenchmark(std::string(prefix) + "/read", benchmark_read<Set>)->Setup(set_up_read<Set>)->Teardown(tear_down<Set>),
              kReadIterations,
              concurrent);
    configure(benchmark::RegisterBenchmark(std::string(prefix) + "/mixed_95_read", benchmark_mixed<Set>)->Setup(set_up_mixed<Set>)->Teardown(tear_down<Set>),
              kMixedIterations,
              concurrent);
}

[[maybe_unused]] const auto registered = []
{
    register_benchmarks<SequentialSet>("tree_vector_set/uint64/sequential", false);
    register_benchmarks<ConcurrentSet>("tree_vector_set/uint64/concurrent", true);
    register_benchmarks<VallaSet>("valla/uint64/sequential", false);
    return true;
}();

}  // namespace
}  // namespace ygg::profiling
