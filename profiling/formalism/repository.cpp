/*
 * Copyright (C) 2026 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "yggdrasil/formalism/repository.hpp"

#include "yggdrasil/core/config.hpp"
#include "yggdrasil/formalism/declarations.hpp"
#include "yggdrasil/formalism/relation_repository.hpp"
#include "yggdrasil/formalism/row_data.hpp"
#include "yggdrasil/formalism/symbol_repository.hpp"
#include "yggdrasil/formalism/values.hpp"

#include <array>
#include <benchmark/benchmark.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <urd/elements/flat.hpp>
#include <urd/tags.hpp>
#include <utility>

namespace ygg::profiling
{
struct TrivialSymbol;
struct SerializedSymbol;
struct Relation;
struct BlockObjectTag;
struct PackedObjectTag;
}

namespace urd
{

template<>
struct data<ygg::profiling::TrivialSymbol>
{
    urd::index<ygg::profiling::TrivialSymbol> index;
    std::uint64_t value = 0;

    static constexpr auto cista_members() noexcept { return urd::Members<&data::index, &data::value> {}; }
    static constexpr auto identifying_members() noexcept { return urd::Members<&data::value> {}; }
};

template<>
struct data<ygg::profiling::SerializedSymbol>
{
    urd::index<ygg::profiling::SerializedSymbol> index;
    urd::flat_vector<urd::index<ygg::profiling::TrivialSymbol>> values;

    data() = default;
    data(const data&) = delete;
    data& operator=(const data&) = delete;
    data(data&&) = default;
    data& operator=(data&&) = default;
    ~data() = default;

    static constexpr auto cista_members() noexcept { return urd::Members<&data::index, &data::values> {}; }
    static constexpr auto identifying_members() noexcept { return urd::Members<&data::values> {}; }
};

}

namespace ygg::formalism
{
template<>
struct RelationRepositoryTraits<profiling::PackedObjectTag>
{
    using width = urd::bit;
};
}

namespace ygg::profiling
{
namespace
{
constexpr std::uint64_t kPrefill = 1U << 16;
constexpr benchmark::IterationCount kLookupIterations = 1U << 20;
constexpr benchmark::IterationCount kInsertIterations = 1U << 18;

using SequentialSymbols = formalism::SymbolRepository<TrivialSymbol, SerializedSymbol>;
using ConcurrentSymbols = formalism::ConcurrentSymbolRepository<TrivialSymbol, SerializedSymbol>;
using SequentialBlockRelations = formalism::RelationRepository<BlockObjectTag, Relation>;
using ConcurrentBlockRelations = formalism::ConcurrentRelationRepository<BlockObjectTag, Relation>;
using SequentialPackedRelations = formalism::RelationRepository<PackedObjectTag, Relation>;
using ConcurrentPackedRelations = formalism::ConcurrentRelationRepository<PackedObjectTag, Relation>;

using SequentialBlockRepository = formalism::Repository<SequentialSymbols, SequentialBlockRelations>;
using ConcurrentBlockRepository = formalism::Repository<ConcurrentSymbols, ConcurrentBlockRelations>;
using SequentialPackedRepository = formalism::Repository<SequentialSymbols, SequentialPackedRelations>;
using ConcurrentPackedRepository = formalism::Repository<ConcurrentSymbols, ConcurrentPackedRelations>;
using Binding = Row<Relation>;

/// The bits of an object of the benchmarks' rows, and the objects a repository is built for.
constexpr std::uint64_t kObjectBits = 12;
constexpr std::size_t kObjects = std::size_t { 1 } << kObjectBits;
/// The column types of the benchmarks' rows: four objects.
constexpr auto kObjectColumns = std::array { type_id<urd::index<formalism::Object<void>>>,
                                             type_id<urd::index<formalism::Object<void>>>,
                                             type_id<urd::index<formalism::Object<void>>>,
                                             type_id<urd::index<formalism::Object<void>>> };

template<typename Repository>
std::unique_ptr<Repository>& repository()
{
    static std::unique_ptr<Repository> value;
    return value;
}

void set_key(urd::data<TrivialSymbol>& data, std::uint64_t key) noexcept { data.value = key; }

void set_key(urd::data<SerializedSymbol>& data, std::uint64_t key)
{
    if (data.values.empty())
        data.values.resize(4);
    for (std::size_t i = 0; i < data.values.size(); ++i)
        data.values[i] = urd::index<TrivialSymbol>(static_cast<uint_t>((key >> (i * 16U)) & 0xFFFFU));
}

urd::data<Binding> make_binding(urd::index<Relation> relation = urd::index<Relation>(0))
{
    auto objects = urd::flat_vector<Value> {};
    objects.resize(kObjectColumns.size());
    return urd::data<Binding>(relation, std::move(objects));
}

void set_key(urd::data<Binding>& data, std::uint64_t key) noexcept
{
    for (std::size_t i = 0; i < data.values.size(); ++i)
        data.values[i] = (key >> (i * kObjectBits)) & (kObjects - 1);
}

template<typename Repository>
void set_up_empty(const benchmark::State&)
{ repository<Repository>() = std::make_unique<Repository>(kObjects); }

/// An empty repository with the pool of each thread's lane declared (a raw key: its pool is declared here).
template<typename Repository>
void set_up_pools(const benchmark::State& state)
{
    set_up_empty<Repository>(state);
    for (int lane = 0; lane < state.threads(); ++lane)
        repository<Repository>()->declare(urd::index<Relation>(static_cast<uint_t>(lane)), kObjectColumns);
}

template<typename Repository, typename Symbol>
void set_up_symbol(const benchmark::State& state)
{
    set_up_empty<Repository>(state);
    auto data = urd::data<Symbol> {};
    for (std::uint64_t key = 0; key < kPrefill; ++key)
    {
        set_key(data, key);
        repository<Repository>()->insert(data);
    }
}

template<typename Repository, bool PerThreadLane>
void set_up_relation(const benchmark::State& state)
{
    set_up_pools<Repository>(state);
    const auto lanes = PerThreadLane ? state.threads() : 1;
    for (int lane = 0; lane < lanes; ++lane)
    {
        auto data = make_binding(urd::index<Relation>(static_cast<uint_t>(lane)));
        for (std::uint64_t key = 0; key < kPrefill; ++key)
        {
            set_key(data, key);
            repository<Repository>()->insert(data);
        }
    }
}

template<typename Repository>
void tear_down(const benchmark::State&)
{ repository<Repository>().reset(); }

template<typename Repository, typename Symbol>
void report_symbol(benchmark::State& state, std::uint64_t created)
{
    const auto entries = repository<Repository>()->symbols().template size<Symbol>();
    const auto bytes = repository<Repository>()->symbols().template memory_usage<Symbol>();
    state.SetItemsProcessed(state.iterations());
    state.counters["created/s"] = benchmark::Counter(static_cast<double>(created), benchmark::Counter::kIsRate);
    state.counters["entries"] = benchmark::Counter(static_cast<double>(entries), benchmark::Counter::kAvgThreads);
    state.counters["bytes/entry"] =
        benchmark::Counter(entries ? static_cast<double>(bytes) / static_cast<double>(entries) : 0.0, benchmark::Counter::kAvgThreads);
}

template<typename Repository>
void report_relation(benchmark::State& state, std::uint64_t created, bool per_thread_lane)
{
    std::size_t entries = 0;
    const auto lanes = per_thread_lane ? state.threads() : 1;
    for (int lane = 0; lane < lanes; ++lane)
        entries += repository<Repository>()->relations().size(urd::index<Relation>(static_cast<uint_t>(lane)));
    const auto bytes = repository<Repository>()->relations().template memory_usage<Relation>();
    state.SetItemsProcessed(state.iterations());
    state.counters["created/s"] = benchmark::Counter(static_cast<double>(created), benchmark::Counter::kIsRate);
    state.counters["entries"] = benchmark::Counter(static_cast<double>(entries), benchmark::Counter::kAvgThreads);
    state.counters["bytes/entry"] =
        benchmark::Counter(entries ? static_cast<double>(bytes) / static_cast<double>(entries) : 0.0, benchmark::Counter::kAvgThreads);
}

template<typename Repository, typename Symbol>
void benchmark_symbol_find(benchmark::State& state)
{
    auto data = urd::data<Symbol> {};
    std::uint64_t checksum = 0;
    std::uint64_t operation = 0;
    for (auto _ : state)
    {
        set_key(data, (operation++ * 11400714819323198485ULL + static_cast<std::uint64_t>(state.thread_index())) & (kPrefill - 1));
        const auto found = repository<Repository>()->find(data);
        if (!found)
        {
            state.SkipWithError("prefilled symbol not found");
            break;
        }
        checksum += found->index().value;
    }
    benchmark::DoNotOptimize(checksum);
    report_symbol<Repository, Symbol>(state, 0);
}

template<typename Repository, typename Symbol>
void benchmark_symbol_mixed(benchmark::State& state)
{
    auto data = urd::data<Symbol> {};
    std::uint64_t checksum = 0;
    std::uint64_t created = 0;
    std::uint64_t operation = 0;
    for (auto _ : state)
    {
        const auto miss = operation % 20U == 0;
        const auto key = miss ? kPrefill + static_cast<std::uint64_t>(state.thread_index()) + static_cast<std::uint64_t>(state.threads()) * created :
                                (operation * 11400714819323198485ULL + static_cast<std::uint64_t>(state.thread_index())) & (kPrefill - 1);
        ++operation;
        set_key(data, key);
        const auto [view, was_created] = repository<Repository>()->insert(data);
        created += was_created;
        checksum += view.index().value;
    }
    if (created != (static_cast<std::uint64_t>(state.iterations()) + 19U) / 20U)
        state.SkipWithError("mixed symbol miss was not created");
    benchmark::DoNotOptimize(checksum);
    report_symbol<Repository, Symbol>(state, created);
}

template<typename Repository, typename Symbol>
void benchmark_symbol_insert(benchmark::State& state)
{
    auto data = urd::data<Symbol> {};
    std::uint64_t checksum = 0;
    std::uint64_t created = 0;
    std::uint64_t operation = 0;
    for (auto _ : state)
    {
        const auto key = static_cast<std::uint64_t>(state.thread_index()) + static_cast<std::uint64_t>(state.threads()) * operation++;
        set_key(data, key);
        const auto [view, was_created] = repository<Repository>()->insert(data);
        created += was_created;
        checksum += view.index().value;
    }
    if (created != static_cast<std::uint64_t>(state.iterations()))
        state.SkipWithError("unique symbol insertion was not created");
    benchmark::DoNotOptimize(checksum);
    report_symbol<Repository, Symbol>(state, created);
}

template<typename Repository, bool PerThreadLane>
void benchmark_relation_find(benchmark::State& state)
{
    const auto lane = PerThreadLane ? state.thread_index() : 0;
    auto data = make_binding(urd::index<Relation>(static_cast<uint_t>(lane)));
    std::uint64_t checksum = 0;
    std::uint64_t operation = 0;
    for (auto _ : state)
    {
        set_key(data, (operation++ * 11400714819323198485ULL + static_cast<std::uint64_t>(state.thread_index())) & (kPrefill - 1));
        const auto found = repository<Repository>()->find(data);
        if (!found)
        {
            state.SkipWithError("prefilled row not found");
            break;
        }
        checksum += found->data().row.value;
    }
    benchmark::DoNotOptimize(checksum);
    report_relation<Repository>(state, 0, PerThreadLane);
}

template<typename Repository>
void benchmark_relation_mixed(benchmark::State& state)
{
    auto data = make_binding();
    std::uint64_t checksum = 0;
    std::uint64_t created = 0;
    std::uint64_t operation = 0;
    for (auto _ : state)
    {
        const auto miss = operation % 20U == 0;
        const auto key = miss ? kPrefill + static_cast<std::uint64_t>(state.thread_index()) + static_cast<std::uint64_t>(state.threads()) * created :
                                (operation * 11400714819323198485ULL + static_cast<std::uint64_t>(state.thread_index())) & (kPrefill - 1);
        ++operation;
        set_key(data, key);
        const auto [view, was_created] = repository<Repository>()->insert(data);
        created += was_created;
        checksum += view.data().row.value;
    }
    if (created != (static_cast<std::uint64_t>(state.iterations()) + 19U) / 20U)
        state.SkipWithError("mixed relation miss was not created");
    benchmark::DoNotOptimize(checksum);
    report_relation<Repository>(state, created, false);
}

template<typename Repository, bool PerThreadLane>
void benchmark_relation_insert(benchmark::State& state)
{
    const auto lane = PerThreadLane ? state.thread_index() : 0;
    auto data = make_binding(urd::index<Relation>(static_cast<uint_t>(lane)));
    std::uint64_t checksum = 0;
    std::uint64_t created = 0;
    std::uint64_t operation = 0;
    for (auto _ : state)
    {
        const auto key =
            PerThreadLane ? operation++ : static_cast<std::uint64_t>(state.thread_index()) + static_cast<std::uint64_t>(state.threads()) * operation++;
        set_key(data, key);
        const auto [view, was_created] = repository<Repository>()->insert(data);
        created += was_created;
        checksum += view.data().row.value;
    }
    if (created != static_cast<std::uint64_t>(state.iterations()))
        state.SkipWithError("unique relation insertion was not created");
    benchmark::DoNotOptimize(checksum);
    report_relation<Repository>(state, created, PerThreadLane);
}

template<typename Repository>
void configure_sequential(benchmark::Benchmark* benchmark, benchmark::IterationCount iterations)
{ benchmark->Iterations(iterations)->Threads(1)->UseRealTime(); }

template<typename Repository>
void configure_concurrent(benchmark::Benchmark* benchmark, benchmark::IterationCount iterations)
{ benchmark->Iterations(iterations)->ThreadRange(1, 8)->UseRealTime(); }

template<typename Repository, typename Symbol>
void register_symbol_benchmarks(const char* prefix, bool concurrent)
{
    auto configure = concurrent ? configure_concurrent<Repository> : configure_sequential<Repository>;
    configure(benchmark::RegisterBenchmark(std::string(prefix) + "/find", benchmark_symbol_find<Repository, Symbol>)
                  ->Setup(set_up_symbol<Repository, Symbol>)
                  ->Teardown(tear_down<Repository>),
              kLookupIterations);
    configure(benchmark::RegisterBenchmark(std::string(prefix) + "/mixed_95_hit", benchmark_symbol_mixed<Repository, Symbol>)
                  ->Setup(set_up_symbol<Repository, Symbol>)
                  ->Teardown(tear_down<Repository>),
              kLookupIterations);
    configure(benchmark::RegisterBenchmark(std::string(prefix) + "/insert", benchmark_symbol_insert<Repository, Symbol>)
                  ->Setup(set_up_empty<Repository>)
                  ->Teardown(tear_down<Repository>),
              kInsertIterations);
}

template<typename Repository>
void register_relation_benchmarks(const char* prefix, bool concurrent)
{
    auto configure = concurrent ? configure_concurrent<Repository> : configure_sequential<Repository>;
    configure(benchmark::RegisterBenchmark(std::string(prefix) + "/shared/find", benchmark_relation_find<Repository, false>)
                  ->Setup(set_up_relation<Repository, false>)
                  ->Teardown(tear_down<Repository>),
              kLookupIterations);
    configure(benchmark::RegisterBenchmark(std::string(prefix) + "/shared/mixed_95_hit", benchmark_relation_mixed<Repository>)
                  ->Setup(set_up_relation<Repository, false>)
                  ->Teardown(tear_down<Repository>),
              kLookupIterations);
    configure(benchmark::RegisterBenchmark(std::string(prefix) + "/shared/insert", benchmark_relation_insert<Repository, false>)
                  ->Setup(set_up_pools<Repository>)
                  ->Teardown(tear_down<Repository>),
              kInsertIterations);
    configure(benchmark::RegisterBenchmark(std::string(prefix) + "/lanes/find", benchmark_relation_find<Repository, true>)
                  ->Setup(set_up_relation<Repository, true>)
                  ->Teardown(tear_down<Repository>),
              kLookupIterations);
    configure(benchmark::RegisterBenchmark(std::string(prefix) + "/lanes/insert", benchmark_relation_insert<Repository, true>)
                  ->Setup(set_up_pools<Repository>)
                  ->Teardown(tear_down<Repository>),
              kInsertIterations);
}

[[maybe_unused]] const auto registered = []
{
    register_symbol_benchmarks<SequentialBlockRepository, TrivialSymbol>("symbol/trivial/sequential", false);
    register_symbol_benchmarks<ConcurrentBlockRepository, TrivialSymbol>("symbol/trivial/concurrent", true);
    register_symbol_benchmarks<SequentialBlockRepository, SerializedSymbol>("symbol/serialized/sequential", false);
    register_symbol_benchmarks<ConcurrentBlockRepository, SerializedSymbol>("symbol/serialized/concurrent", true);
    register_relation_benchmarks<SequentialBlockRepository>("relation/block/sequential", false);
    register_relation_benchmarks<ConcurrentBlockRepository>("relation/block/concurrent", true);
    register_relation_benchmarks<SequentialPackedRepository>("relation/packed/sequential", false);
    register_relation_benchmarks<ConcurrentPackedRepository>("relation/packed/concurrent", true);
    return true;
}();
}
}  // namespace ygg::profiling
