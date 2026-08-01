/*
 * Copyright (C) 2026 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <benchmark/benchmark.h>
#include <cstdint>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <yggdrasil/containers/vector.hpp>
#include <yggdrasil/formalism/binding_data.hpp>
#include <yggdrasil/formalism/repository.hpp>
#include <yggdrasil/ids/index_mixins.hpp>

namespace ygg::profiling
{
struct TrivialSymbol;
struct SerializedSymbol;
struct Relation;
struct BlockObjectTag;
struct PackedObjectTag;
}

namespace ygg
{
template<>
struct Index<profiling::TrivialSymbol> : IndexMixin<Index<profiling::TrivialSymbol>>
{
    using Base = IndexMixin<Index<profiling::TrivialSymbol>>;
    using Base::Base;
};

template<>
struct Data<profiling::TrivialSymbol>
{
    Index<profiling::TrivialSymbol> index;
    std::uint64_t value = 0;

    auto identifying_members() const noexcept { return std::tie(value); }
};

template<>
struct Index<profiling::SerializedSymbol> : IndexMixin<Index<profiling::SerializedSymbol>>
{
    using Base = IndexMixin<Index<profiling::SerializedSymbol>>;
    using Base::Base;
};

template<>
struct Data<profiling::SerializedSymbol>
{
    Index<profiling::SerializedSymbol> index;
    IndexList<profiling::TrivialSymbol> values;

    Data() = default;
    Data(const Data&) = delete;
    Data& operator=(const Data&) = delete;
    Data(Data&&) = default;
    Data& operator=(Data&&) = default;

    auto cista_members() const noexcept { return std::tie(index, values); }
    auto identifying_members() const noexcept { return std::tie(values); }
};

template<>
struct Index<profiling::Relation> : IndexMixin<Index<profiling::Relation>>
{
    using Base = IndexMixin<Index<profiling::Relation>>;
    using Base::Base;
};
}

namespace ygg::formalism
{
template<>
struct RelationRepositoryTraits<profiling::PackedObjectTag>
{
    using storage_type = BitPackedArraySetStorage;
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
using BlockBinding = formalism::RelationBinding<Relation, BlockObjectTag>;
using PackedBinding = formalism::RelationBinding<Relation, PackedObjectTag>;

template<typename Repository>
std::unique_ptr<Repository> repository;

void set_key(Data<TrivialSymbol>& data, std::uint64_t key) noexcept { data.value = key; }

void set_key(Data<SerializedSymbol>& data, std::uint64_t key)
{
    if (data.values.empty())
        data.values.resize(4);
    for (std::size_t i = 0; i < data.values.size(); ++i)
        data.values[i] = Index<TrivialSymbol>(static_cast<uint_t>((key >> (i * 16U)) & 0xFFFFU));
}

template<typename Binding>
Data<Binding> make_binding(Index<Relation> relation = Index<Relation>(0))
{
    using Object = formalism::Object<typename Binding::object_tag>;
    auto objects = IndexList<Object> {};
    objects.resize(4);
    return Data<Binding>(relation, 4, std::move(objects));
}

template<typename Binding>
void set_key(Data<Binding>& data, std::uint64_t key) noexcept
{
    for (std::size_t i = 0; i < data.objects.size(); ++i)
        data.objects[i] = Index<formalism::Object<typename Binding::object_tag>>(static_cast<uint_t>((key >> (i * 12U)) & 0xFFFU));
}

template<typename Repository>
void set_up_empty(const benchmark::State&)
{
    repository<Repository> = std::make_unique<Repository>(0, nullptr, formalism::RelationRepositoryConfig(12));
}

template<typename Repository, typename Symbol>
void set_up_symbol(const benchmark::State& state)
{
    set_up_empty<Repository>(state);
    auto data = Data<Symbol> {};
    for (std::uint64_t key = 0; key < kPrefill; ++key)
    {
        set_key(data, key);
        repository<Repository>->get_or_create(data);
    }
}

template<typename Repository, typename Binding, bool PerThreadLane>
void set_up_relation(const benchmark::State& state)
{
    set_up_empty<Repository>(state);
    const auto lanes = PerThreadLane ? state.threads() : 1;
    for (int lane = 0; lane < lanes; ++lane)
    {
        auto data = make_binding<Binding>(Index<Relation>(static_cast<uint_t>(lane)));
        for (std::uint64_t key = 0; key < kPrefill; ++key)
        {
            set_key(data, key);
            repository<Repository>->get_or_create(data);
        }
    }
}

template<typename Repository>
void tear_down(const benchmark::State&)
{
    repository<Repository>.reset();
}

template<typename Repository, typename Symbol>
void report_symbol(benchmark::State& state, std::uint64_t created)
{
    const auto entries = repository<Repository>->template size<Symbol>();
    const auto bytes = repository<Repository>->template memory_usage<Symbol>();
    state.SetItemsProcessed(state.iterations());
    state.counters["created/s"] = benchmark::Counter(static_cast<double>(created), benchmark::Counter::kIsRate);
    state.counters["entries"] = benchmark::Counter(static_cast<double>(entries), benchmark::Counter::kAvgThreads);
    state.counters["bytes/entry"] = benchmark::Counter(entries ? static_cast<double>(bytes) / entries : 0.0, benchmark::Counter::kAvgThreads);
}

template<typename Repository, typename Binding>
void report_relation(benchmark::State& state, std::uint64_t created, bool per_thread_lane)
{
    std::size_t entries = 0;
    const auto lanes = per_thread_lane ? state.threads() : 1;
    for (int lane = 0; lane < lanes; ++lane)
        entries += repository<Repository>->size(Index<Relation>(static_cast<uint_t>(lane)));
    const auto bytes = repository<Repository>->template memory_usage<Binding>();
    state.SetItemsProcessed(state.iterations());
    state.counters["created/s"] = benchmark::Counter(static_cast<double>(created), benchmark::Counter::kIsRate);
    state.counters["entries"] = benchmark::Counter(static_cast<double>(entries), benchmark::Counter::kAvgThreads);
    state.counters["bytes/entry"] = benchmark::Counter(entries ? static_cast<double>(bytes) / entries : 0.0, benchmark::Counter::kAvgThreads);
}

template<typename Repository, typename Symbol>
void benchmark_symbol_find(benchmark::State& state)
{
    auto data = Data<Symbol> {};
    std::uint64_t checksum = 0;
    std::uint64_t operation = 0;
    for (auto _ : state)
    {
        set_key(data, (operation++ * 11400714819323198485ULL + static_cast<std::uint64_t>(state.thread_index())) & (kPrefill - 1));
        const auto found = repository<Repository>->find(data);
        if (!found)
        {
            state.SkipWithError("prefilled symbol not found");
            break;
        }
        checksum += found->get_index().get_value();
    }
    benchmark::DoNotOptimize(checksum);
    report_symbol<Repository, Symbol>(state, 0);
}

template<typename Repository, typename Symbol>
void benchmark_symbol_mixed(benchmark::State& state)
{
    auto data = Data<Symbol> {};
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
        const auto [view, was_created] = repository<Repository>->get_or_create(data);
        created += was_created;
        checksum += view.get_index().get_value();
    }
    if (created != (static_cast<std::uint64_t>(state.iterations()) + 19U) / 20U)
        state.SkipWithError("mixed symbol miss was not created");
    benchmark::DoNotOptimize(checksum);
    report_symbol<Repository, Symbol>(state, created);
}

template<typename Repository, typename Symbol>
void benchmark_symbol_insert(benchmark::State& state)
{
    auto data = Data<Symbol> {};
    std::uint64_t checksum = 0;
    std::uint64_t created = 0;
    std::uint64_t operation = 0;
    for (auto _ : state)
    {
        const auto key = static_cast<std::uint64_t>(state.thread_index()) + static_cast<std::uint64_t>(state.threads()) * operation++;
        set_key(data, key);
        const auto [view, was_created] = repository<Repository>->get_or_create(data);
        created += was_created;
        checksum += view.get_index().get_value();
    }
    if (created != static_cast<std::uint64_t>(state.iterations()))
        state.SkipWithError("unique symbol insertion was not created");
    benchmark::DoNotOptimize(checksum);
    report_symbol<Repository, Symbol>(state, created);
}

template<typename Repository, typename Binding, bool PerThreadLane>
void benchmark_relation_find(benchmark::State& state)
{
    const auto lane = PerThreadLane ? state.thread_index() : 0;
    auto data = make_binding<Binding>(Index<Relation>(static_cast<uint_t>(lane)));
    std::uint64_t checksum = 0;
    std::uint64_t operation = 0;
    for (auto _ : state)
    {
        set_key(data, (operation++ * 11400714819323198485ULL + static_cast<std::uint64_t>(state.thread_index())) & (kPrefill - 1));
        const auto found = repository<Repository>->find(data);
        if (!found)
        {
            state.SkipWithError("prefilled relation binding not found");
            break;
        }
        checksum += found->get_index().row.get_value();
    }
    benchmark::DoNotOptimize(checksum);
    report_relation<Repository, Binding>(state, 0, PerThreadLane);
}

template<typename Repository, typename Binding>
void benchmark_relation_mixed(benchmark::State& state)
{
    auto data = make_binding<Binding>();
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
        const auto [view, was_created] = repository<Repository>->get_or_create(data);
        created += was_created;
        checksum += view.get_index().row.get_value();
    }
    if (created != (static_cast<std::uint64_t>(state.iterations()) + 19U) / 20U)
        state.SkipWithError("mixed relation miss was not created");
    benchmark::DoNotOptimize(checksum);
    report_relation<Repository, Binding>(state, created, false);
}

template<typename Repository, typename Binding, bool PerThreadLane>
void benchmark_relation_insert(benchmark::State& state)
{
    const auto lane = PerThreadLane ? state.thread_index() : 0;
    auto data = make_binding<Binding>(Index<Relation>(static_cast<uint_t>(lane)));
    std::uint64_t checksum = 0;
    std::uint64_t created = 0;
    std::uint64_t operation = 0;
    for (auto _ : state)
    {
        const auto key =
            PerThreadLane ? operation++ : static_cast<std::uint64_t>(state.thread_index()) + static_cast<std::uint64_t>(state.threads()) * operation++;
        set_key(data, key);
        const auto [view, was_created] = repository<Repository>->get_or_create(data);
        created += was_created;
        checksum += view.get_index().row.get_value();
    }
    if (created != static_cast<std::uint64_t>(state.iterations()))
        state.SkipWithError("unique relation insertion was not created");
    benchmark::DoNotOptimize(checksum);
    report_relation<Repository, Binding>(state, created, PerThreadLane);
}

template<typename Repository>
void configure_sequential(benchmark::internal::Benchmark* benchmark, benchmark::IterationCount iterations)
{
    benchmark->Iterations(iterations)->Threads(1)->UseRealTime();
}

template<typename Repository>
void configure_concurrent(benchmark::internal::Benchmark* benchmark, benchmark::IterationCount iterations)
{
    benchmark->Iterations(iterations)->ThreadRange(1, 8)->UseRealTime();
}

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

template<typename Repository, typename Binding>
void register_relation_benchmarks(const char* prefix, bool concurrent)
{
    auto configure = concurrent ? configure_concurrent<Repository> : configure_sequential<Repository>;
    configure(benchmark::RegisterBenchmark(std::string(prefix) + "/shared/find", benchmark_relation_find<Repository, Binding, false>)
                  ->Setup(set_up_relation<Repository, Binding, false>)
                  ->Teardown(tear_down<Repository>),
              kLookupIterations);
    configure(benchmark::RegisterBenchmark(std::string(prefix) + "/shared/mixed_95_hit", benchmark_relation_mixed<Repository, Binding>)
                  ->Setup(set_up_relation<Repository, Binding, false>)
                  ->Teardown(tear_down<Repository>),
              kLookupIterations);
    configure(benchmark::RegisterBenchmark(std::string(prefix) + "/shared/insert", benchmark_relation_insert<Repository, Binding, false>)
                  ->Setup(set_up_empty<Repository>)
                  ->Teardown(tear_down<Repository>),
              kInsertIterations);
    configure(benchmark::RegisterBenchmark(std::string(prefix) + "/lanes/find", benchmark_relation_find<Repository, Binding, true>)
                  ->Setup(set_up_relation<Repository, Binding, true>)
                  ->Teardown(tear_down<Repository>),
              kLookupIterations);
    configure(benchmark::RegisterBenchmark(std::string(prefix) + "/lanes/insert", benchmark_relation_insert<Repository, Binding, true>)
                  ->Setup(set_up_empty<Repository>)
                  ->Teardown(tear_down<Repository>),
              kInsertIterations);
}

[[maybe_unused]] const auto registered = []
{
    register_symbol_benchmarks<SequentialBlockRepository, TrivialSymbol>("symbol/trivial/sequential", false);
    register_symbol_benchmarks<ConcurrentBlockRepository, TrivialSymbol>("symbol/trivial/concurrent", true);
    register_symbol_benchmarks<SequentialBlockRepository, SerializedSymbol>("symbol/serialized/sequential", false);
    register_symbol_benchmarks<ConcurrentBlockRepository, SerializedSymbol>("symbol/serialized/concurrent", true);
    register_relation_benchmarks<SequentialBlockRepository, BlockBinding>("relation/block/sequential", false);
    register_relation_benchmarks<ConcurrentBlockRepository, BlockBinding>("relation/block/concurrent", true);
    register_relation_benchmarks<SequentialPackedRepository, PackedBinding>("relation/packed/sequential", false);
    register_relation_benchmarks<ConcurrentPackedRepository, PackedBinding>("relation/packed/concurrent", true);
    return true;
}();
}
}  // namespace ygg::profiling
