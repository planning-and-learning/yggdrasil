/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "yggdrasil/containers/bit_packed_array_pool.hpp"
#include "yggdrasil/database/incremental/join.hpp"
#include "yggdrasil/database/incremental/projection.hpp"
#include "yggdrasil/database/operations.hpp"
#include "yggdrasil/database/relation_pool.hpp"
#include "yggdrasil/database/relation_repository.hpp"
#include "yggdrasil/ids/index_coder.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <gtest/gtest.h>
#include <limits>
#include <new>
#include <ranges>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

#if defined(_MSC_VER)
#include <malloc.h>
#endif

// This executable replaces C++ allocation functions only. That covers the
// default std/GTL allocators and RawArraySet's geometric byte storage, including
// aligned allocations. It does not intercept direct malloc/free calls, including
// Cista column/plan storage. Tracking is restricted to the evaluating thread and excludes GTest,
// setup, warmup, and destruction of the retained evaluation objects.
namespace allocation_tracking
{
struct Counts
{
    size_t allocated = 0;
    size_t deallocated = 0;
};

thread_local bool enabled = false;
thread_local Counts counts;

void* allocate(size_t bytes)
{
    auto* result = std::malloc(bytes == 0 ? 1 : bytes);
    if (!result)
        throw std::bad_alloc();
    if (enabled)
        ++counts.allocated;
    return result;
}

void deallocate(void* pointer) noexcept
{
    if (pointer && enabled)
        ++counts.deallocated;
    std::free(pointer);
}

void* allocate_aligned(size_t bytes, size_t alignment)
{
    void* result = nullptr;
#if defined(_MSC_VER)
    result = _aligned_malloc(bytes == 0 ? 1 : bytes, alignment);
#else
    if (alignment < sizeof(void*))
        alignment = sizeof(void*);
    if (posix_memalign(&result, alignment, bytes == 0 ? 1 : bytes) != 0)
        result = nullptr;
#endif
    if (!result)
        throw std::bad_alloc();
    if (enabled)
        ++counts.allocated;
    return result;
}

void deallocate_aligned(void* pointer) noexcept
{
    if (pointer && enabled)
        ++counts.deallocated;
#if defined(_MSC_VER)
    _aligned_free(pointer);
#else
    std::free(pointer);
#endif
}

class Scope
{
public:
    Scope()
    {
        counts = {};
        enabled = true;
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    ~Scope() { enabled = false; }

    Counts finish() noexcept
    {
        enabled = false;
        return counts;
    }
};
}  // namespace allocation_tracking

void* operator new(size_t bytes) { return allocation_tracking::allocate(bytes); }
void* operator new[](size_t bytes) { return allocation_tracking::allocate(bytes); }
void operator delete(void* pointer) noexcept { allocation_tracking::deallocate(pointer); }
void operator delete[](void* pointer) noexcept { allocation_tracking::deallocate(pointer); }
void operator delete(void* pointer, size_t) noexcept { allocation_tracking::deallocate(pointer); }
void operator delete[](void* pointer, size_t) noexcept { allocation_tracking::deallocate(pointer); }
void* operator new(size_t bytes, std::align_val_t alignment) { return allocation_tracking::allocate_aligned(bytes, static_cast<size_t>(alignment)); }
void* operator new[](size_t bytes, std::align_val_t alignment) { return allocation_tracking::allocate_aligned(bytes, static_cast<size_t>(alignment)); }
void operator delete(void* pointer, std::align_val_t) noexcept { allocation_tracking::deallocate_aligned(pointer); }
void operator delete[](void* pointer, std::align_val_t) noexcept { allocation_tracking::deallocate_aligned(pointer); }
void operator delete(void* pointer, size_t, std::align_val_t) noexcept { allocation_tracking::deallocate_aligned(pointer); }
void operator delete[](void* pointer, size_t, std::align_val_t) noexcept { allocation_tracking::deallocate_aligned(pointer); }
void* operator new(size_t bytes, const std::nothrow_t&) noexcept
{
    try
    {
        return ::operator new(bytes);
    }
    catch (...)
    {
        return nullptr;
    }
}
void* operator new[](size_t bytes, const std::nothrow_t&) noexcept
{
    try
    {
        return ::operator new[](bytes);
    }
    catch (...)
    {
        return nullptr;
    }
}
void* operator new(size_t bytes, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    try
    {
        return ::operator new(bytes, alignment);
    }
    catch (...)
    {
        return nullptr;
    }
}
void* operator new[](size_t bytes, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    try
    {
        return ::operator new[](bytes, alignment);
    }
    catch (...)
    {
        return nullptr;
    }
}
void operator delete(void* pointer, const std::nothrow_t&) noexcept { allocation_tracking::deallocate(pointer); }
void operator delete[](void* pointer, const std::nothrow_t&) noexcept { allocation_tracking::deallocate(pointer); }
void operator delete(void* pointer, std::align_val_t, const std::nothrow_t&) noexcept { allocation_tracking::deallocate_aligned(pointer); }
void operator delete[](void* pointer, std::align_val_t, const std::nothrow_t&) noexcept { allocation_tracking::deallocate_aligned(pointer); }

namespace ygg::tests
{
using namespace database;
using Values = TypeList<uint_t>;
using ColumnIndex = Index<database::Column>;

namespace
{
template<typename T = uint_t, typename... Args>
auto cells(Args... values)
{
    return std::tuple { T(values)... };
}

template<typename T, size_t N>
auto packed(const std::array<T, N>& values)
{
    std::array<std::byte, N * ColumnCodec<T>::size> result;
    for (size_t i = 0; i < N; ++i)
        ColumnCodec<T>::encode(values[i], std::span(result).subspan(i * ColumnCodec<T>::size, ColumnCodec<T>::size));
    return result;
}

constexpr size_t key_count = 16;
constexpr std::array<std::pair<size_t, size_t>, 12> state_sizes = { {
    { 1025, 128 },
    { 128, 1025 },
    { 257, 257 },
    { 127, 128 },
    { 128, 127 },
    { 0, 128 },
    { 128, 0 },
    { 0, 0 },
    { 1, 7 },
    { 7, 1 },
    { 1024, 1 },
    { 1, 1024 },
} };

size_t frequency(size_t rows, size_t key) { return rows / key_count + static_cast<size_t>(key < rows % key_count); }
size_t distinct_keys(size_t rows) { return rows < key_count ? rows : key_count; }

size_t joined_size(size_t left_size, size_t right_size, size_t keys = key_count)
{
    size_t result = 0;
    for (size_t key = 0; key < keys; ++key)
        result += frequency(left_size, key) * frequency(right_size, key);
    return result;
}

size_t projected_right_size(size_t left_size, size_t right_size)
{
    size_t result = 0;
    for (size_t key = 0; key < key_count; ++key)
        if (frequency(left_size, key) != 0)
            result += frequency(right_size, key);
    return result;
}

void fill(Builder<Relation<Values>>& relation, size_t rows, uint_t offset)
{
    relation.clear();
    for (size_t i = 0; i < rows; ++i)
        relation.insert(cells(static_cast<uint_t>(i % key_count), offset + static_cast<uint_t>(i)));
}

struct Evaluation
{
    Builder<Relation<Values>> left { { ColumnIndex(1), ColumnIndex(2) } };
    Builder<Relation<Values>> right { { ColumnIndex(1), ColumnIndex(3) } };
    Builder<Relation<Values>> joined { { ColumnIndex(1), ColumnIndex(2), ColumnIndex(3) } };
    Builder<Relation<Values>> selected { { ColumnIndex(1), ColumnIndex(2), ColumnIndex(3) } };
    Builder<Relation<Values>> projected { { ColumnIndex(3) } };
    Builder<Relation<Values>> renamed_projection { { ColumnIndex(6) } };
    Builder<Relation<Values>> left_keys { { ColumnIndex(1) } };
    Builder<Relation<Values>> right_keys { { ColumnIndex(1) } };
    Builder<Relation<Values>> either_keys { { ColumnIndex(1) } };
    Builder<Relation<Values>> only_left_keys { { ColumnIndex(1) } };
    Builder<Relation<Values>> exists;
    Workspace<Values> workspace;
    const Builder<Columns<Values>> joined_columns { ColumnIndex(1), ColumnIndex(2), ColumnIndex(3) };
    const std::array<ColumnIndex, 3> renamed_columns { ColumnIndex(4), ColumnIndex(5), ColumnIndex(6) };
    const std::array<ColumnIndex, 1> projected_columns { ColumnIndex(6) };
    const std::span<const ColumnIndex> renamed_schema { std::span<const ColumnIndex>(renamed_columns) };
    const std::span<const ColumnIndex> projected_schema { std::span<const ColumnIndex>(projected_columns) };
    Builder<Columns<Values>> retained_columns { ColumnIndex(4), ColumnIndex(5), ColumnIndex(6) };

    bool evaluate(size_t left_size, size_t right_size, uint_t offset)
    {
        const auto schema_memory = retained_columns.memory_usage();
        retained_columns.assign(projected_schema);
        bool valid = std::ranges::equal(retained_columns.span(), projected_schema, {}, &ColumnLayout::label);
        retained_columns.assign(std::span<const ColumnIndex>());
        valid &= retained_columns.empty();
        retained_columns.assign(renamed_schema);
        valid &= std::ranges::equal(retained_columns.span(), renamed_schema, {}, &ColumnLayout::label);
        valid &= retained_columns.memory_usage() == schema_memory;
        fill(left, left_size, offset);
        fill(right, right_size, offset + 10000);
        join(left, right, joined, workspace);
        select(joined, [](Row<Values> row) { return row.get<uint_t>(size_t { 0 }) < key_count / 2; }, selected);
        project(joined, { ColumnIndex(3) }, projected, workspace);
        // Relabel and restore the reusable builder within the measured path.
        const auto* schema_storage = joined.columns().data();
        joined.rename(renamed_schema);
        project(joined, std::span<const ColumnIndex>(projected_columns), renamed_projection, workspace);
        valid &= joined.columns().data() == schema_storage;
        valid &= std::ranges::equal(joined.columns(), retained_columns.span());
        joined.rename(joined_columns.span());
        project(left, { ColumnIndex(1) }, left_keys, workspace);
        project(right, { ColumnIndex(1) }, right_keys, workspace);
        union_(left_keys, right_keys, either_keys);
        difference(left_keys, right_keys, only_left_keys);
        project(joined, {}, exists, workspace);
        const auto left_count = distinct_keys(left_size);
        const auto right_count = distinct_keys(right_size);
        valid &= joined.size() == joined_size(left_size, right_size);
        valid &= selected.size() == joined_size(left_size, right_size, key_count / 2);
        valid &= projected.size() == projected_right_size(left_size, right_size);
        valid &= renamed_projection.size() == projected.size();
        valid &= either_keys.size() == (left_count > right_count ? left_count : right_count);
        valid &= only_left_keys.size() == (left_count > right_count ? left_count - right_count : 0);
        valid &= exists.size() == static_cast<size_t>(left_size != 0 && right_size != 0);
        if (left_size != 0 && right_size != 0)
        {
            valid &= joined.contains(cells(0, offset, offset + 10000));
            valid &= projected.contains(cells(offset + 10000));
            valid &= renamed_projection.contains(cells(offset + 10000));
        }
        return valid;
    }
};

struct CachedJoinEvaluation
{
    RelationPool<Values> pool;
    UniqueObjectPoolPtr<Builder<Relation<Values>>> build = pool.get_or_allocate({ ColumnIndex(1), ColumnIndex(2) });
    Builder<Relation<Values>> probe { { ColumnIndex(1), ColumnIndex(3) } };
    JoinPlan<Values> forward { build->columns(), probe.columns() };
    JoinPlan<Values> reverse { probe.columns(), build->columns() };
    Builder<Relation<Values>> forward_result { forward.output_columns() };
    Builder<Relation<Values>> reverse_result { reverse.output_columns() };
    JoinIndexCache<Values> cache;
    Workspace<Values> workspace;

    CachedJoinEvaluation() { fill(*build, 128, 1000); }

    bool evaluate(size_t left_size, size_t right_size, uint_t offset)
    {
        bool valid = true;
        for (const auto probe_size : { left_size, right_size })
        {
            fill(probe, probe_size, offset);
            join(*build, probe, forward, cache, JoinReuse { true, false }, forward_result, workspace);
            join(probe, *build, reverse, cache, JoinReuse { false, true }, reverse_result, workspace);
            valid &= forward_result.size() == joined_size(build->size(), probe_size);
            valid &= reverse_result.size() == forward_result.size();
            valid &= cache.size() == 1;
            if (probe_size != 0)
            {
                valid &= forward_result.contains(cells(0, 1000, offset));
                valid &= reverse_result.contains(cells(0, offset, 1000));
            }
        }
        return valid;
    }
};

struct PooledEvaluation
{
    Builder<Relation<Values>> left { { ColumnIndex(1), ColumnIndex(2) } };
    Builder<Relation<Values>> right { { ColumnIndex(1), ColumnIndex(3) } };
    RelationPool<Values> pool;
    Workspace<Values> workspace;

    bool evaluate(size_t left_size, size_t right_size, uint_t offset)
    {
        fill(left, left_size, offset);
        fill(right, right_size, offset + 10000);
        auto joined = pool.get_or_allocate({ ColumnIndex(1), ColumnIndex(2), ColumnIndex(3) });
        auto left_keys = pool.get_or_allocate({ ColumnIndex(1) });
        auto right_keys = pool.get_or_allocate({ ColumnIndex(1) });
        auto either = pool.get_or_allocate({ ColumnIndex(1) });
        auto exists = pool.get_or_allocate({});
        join(left, right, *joined, workspace);
        project(left, { ColumnIndex(1) }, *left_keys, workspace);
        project(right, { ColumnIndex(1) }, *right_keys, workspace);
        union_(*left_keys, *right_keys, *either);
        project(*joined, {}, *exists, workspace);
        bool valid = joined->size() == joined_size(left_size, right_size);
        const auto left_count = distinct_keys(left_size);
        const auto right_count = distinct_keys(right_size);
        valid &= either->size() == (left_count > right_count ? left_count : right_count);
        valid &= exists->size() == static_cast<size_t>(left_size != 0 && right_size != 0);
        if (left_size != 0 && right_size != 0)
            valid &= joined->contains(cells(0, offset, offset + 10000));
        // Returning these handles is also inside the measurement. In particular,
        // all three unary intermediates must coexist without new pool entries.
        return valid;
    }
};

struct PreparedPooledEvaluation
{
    // Sparse labels must not be interpreted as positions or dense-map sizes.
    static constexpr ColumnIndex key_column = ColumnIndex(900000001);
    static constexpr ColumnIndex left_column = ColumnIndex(3);
    static constexpr ColumnIndex right_column = ColumnIndex(800000001);
    Builder<Relation<Values>> left { { left_column, key_column } };
    Builder<Relation<Values>> right { { key_column, right_column } };
    JoinPlan<Values> joining { left.columns(), right.columns() };
    ProjectionPlan<Values> projecting { joining.output_columns(), { right_column, left_column } };
    ProjectionPlan<Values> projecting_left_keys { left.columns(), { key_column } };
    ProjectionPlan<Values> projecting_right_keys { right.columns(), { key_column } };
    ProjectionPlan<Values> testing_existence { joining.output_columns(), {} };
    const Builder<Columns<Values>> alternate_join_columns { ColumnIndex(7), ColumnIndex(8), ColumnIndex(9) };
    RelationPool<Values> pool;
    Workspace<Values> workspace;

    bool evaluate(size_t left_size, size_t right_size, uint_t offset)
    {
        bool valid = true;
        // Alternate matching and nonempty disjoint key sets to detect stale
        // indexes, with changed keys and tuple values on every measured state.
        for (const bool disjoint : { false, true })
        {
            left.clear();
            right.clear();
            for (size_t i = 0; i < left_size; ++i)
                left.insert(cells(offset + static_cast<uint_t>(i), offset + static_cast<uint_t>(i % key_count)));
            const auto right_key_offset = offset + static_cast<uint_t>(disjoint ? key_count : 0);
            for (size_t i = 0; i < right_size; ++i)
                right.insert(cells(right_key_offset + static_cast<uint_t>(i % key_count), offset + 10000 + static_cast<uint_t>(i)));

            // Relabel the same pooled relation before each prepared checkout.
            // Schema changes and read-only access stay in the measurement.
            {
                auto relabeled = pool.get_or_allocate(alternate_join_columns.span());
                valid &= relabeled->empty();
                relabeled->insert(cells(offset, offset + 1, offset + 2));
                const auto& borrowed = *relabeled;
                valid &= borrowed.size() == 1;
                valid &= std::ranges::equal(borrowed.columns(), alternate_join_columns.span());
            }
            auto joined = pool.get_or_allocate(joining.output_columns());
            valid &= joined->empty();
            valid &= std::ranges::equal(joined->columns(), joining.output_columns());
            auto projected = pool.get_or_allocate(projecting.output_columns());
            auto left_keys = pool.get_or_allocate(projecting_left_keys.output_columns());
            auto right_keys = pool.get_or_allocate(projecting_right_keys.output_columns());
            auto either = pool.get_or_allocate(projecting_left_keys.output_columns());
            auto exists = pool.get_or_allocate(testing_existence.output_columns());
            join(left, right, joining, *joined, workspace);
            project(*joined, projecting, *projected, workspace);
            project(left, projecting_left_keys, *left_keys, workspace);
            project(right, projecting_right_keys, *right_keys, workspace);
            union_(*left_keys, *right_keys, *either);
            project(*joined, testing_existence, *exists, workspace);

            const auto expected_size = disjoint ? 0 : joined_size(left_size, right_size);
            const auto left_count = distinct_keys(left_size);
            const auto right_count = distinct_keys(right_size);
            valid &= joined->size() == expected_size;
            valid &= projected->size() == expected_size;
            valid &= left_keys->size() == left_count;
            valid &= right_keys->size() == right_count;
            valid &= either->size() == (disjoint ? left_count + right_count : (left_count > right_count ? left_count : right_count));
            valid &= exists->size() == static_cast<size_t>(expected_size != 0);
            if (expected_size != 0)
            {
                valid &= joined->contains(cells(offset, offset, offset + 10000));
                valid &= projected->contains(cells(offset + 10000, offset));
            }
            // Pool checkout and return both stay inside the measurement.
        }
        return valid;
    }
};

struct InterningEvaluation
{
    RelationPoolFactory<Values> row_factory;
    RelationPool<Values> pool = row_factory.create_pool();
    RelationRepositoryFactory<Values> factory { row_factory };
    RelationRepository<Values> repository = factory.create();
    RelationRepository<Values> copied_repository = factory.create();
    Builder<Columns<Values>> schema_builder;
    Data<Columns<Values>> schema_data;
    const std::array<ColumnIndex, 3> columns { ColumnIndex(11), ColumnIndex(12), ColumnIndex(13) };
    const std::array<ColumnIndex, 3> renamed_columns { ColumnIndex(21), ColumnIndex(22), ColumnIndex(23) };

    bool evaluate(bool reverse, uint_t generation)
    {
        repository.clear();
        copied_repository.clear();
        bool valid = true;
        constexpr size_t count = 512;
        for (size_t i = 0; i < count; ++i)
        {
            // Vary row lengths and registration order across repository clears.
            const auto logical_index = reverse ? count - i - 1 : i;
            const auto arity = 1 + logical_index % 3;
            const auto schema = std::span<const ColumnIndex>(columns.data(), arity);
            schema_builder.assign(schema);
            const auto interned_schema = insert(repository, schema_builder).first;
            assign(schema_data, schema_builder);
            const auto [duplicate_schema, schema_created] = repository.insert(schema_data);
            valid &= !schema_created && interned_schema.get_index() == duplicate_schema.get_index();
            valid &= std::ranges::equal(interned_schema.span(), schema, {}, &ColumnLayout::label);
            auto builder = pool.get_or_allocate(schema);
            const auto seed = generation * 8192 + static_cast<uint_t>(logical_index) * 4;
            const std::array<uint_t, 3> first_row { seed, seed + 1, seed + 2 };
            const std::array<uint_t, 3> second_row { seed + 1, seed + 2, seed + 3 };
            builder->insert(std::span<const std::byte>(packed(first_row)).first(arity * ColumnCodec<uint_t>::size));
            builder->insert(std::span<const std::byte>(packed(second_row)).first(arity * ColumnCodec<uint_t>::size));
            const auto [original, created] = insert(repository, *builder, generation);
            const auto [copied, copied_created] = database::copy(original, copied_repository);
            valid &= copied_created && copied.get_data().schema_namespace == generation;
            valid &= !database::copy(original, copied_repository).second;
            auto extracted = pool.get_or_allocate(std::span<const ColumnIndex>(renamed_columns.data(), arity));
            const auto storage_index = extracted->get_storage_index();
            const auto* schema_storage = extracted->columns().data();
            assign(*extracted, copied);
            assign(*extracted, *extracted);
            assign(*extracted, make_view(*extracted, repository));
            valid &= extracted->get_index().is_max() && extracted->size() == original.size();
            valid &= extracted->get_storage_index() == storage_index && extracted->columns().data() == schema_storage;
            valid &= std::ranges::equal(extracted->columns(), schema, {}, &ColumnLayout::label);
            valid &= extracted->contains(std::span<const std::byte>(packed(first_row)).first(arity * ColumnCodec<uint_t>::size));
            builder->clear();
            builder->insert(std::span<const std::byte>(packed(second_row)).first(arity * ColumnCodec<uint_t>::size));
            builder->insert(std::span<const std::byte>(packed(first_row)).first(arity * ColumnCodec<uint_t>::size));
            const auto [duplicate, duplicate_created] = insert(repository, *builder, generation);
            const auto renamed = repository.rename(original, std::span<const ColumnIndex>(renamed_columns.data(), arity), generation);
            valid &= created && !duplicate_created;
            valid &= original.get_index() == duplicate.get_index();
            valid &= original.get_index() != renamed.get_index();
            valid &= original.get_storage_index() == renamed.get_storage_index();
            valid &= original.at(0).bytes().data() == renamed.at(0).bytes().data();
            valid &= original.size() == 2 && renamed.arity() == arity;
            valid &= renamed.contains(std::span<const std::byte>(packed(first_row)).first(arity * ColumnCodec<uint_t>::size));
        }
        auto nullary = pool.get_or_allocate({});
        const auto false_view = insert(repository, *nullary, generation).first;
        nullary->insert(cells());
        const auto true_view = insert(repository, *nullary, generation).first;
        valid &= false_view.empty() && true_view.size() == 1;
        valid &= repository.size() == 2 * count + 2;
        return valid;
    }
};

template<typename EvaluationType>
void expect_no_allocations_after_warmup(EvaluationType& evaluation)
{
    bool valid = true;
    for (const auto& [left_size, right_size] : state_sizes)
        valid &= evaluation.evaluate(left_size, right_size, 1000);
    ASSERT_TRUE(valid);

    allocation_tracking::Scope measured;
    uint_t generation = 1;
    for (size_t repeat = 0; repeat < 4; ++repeat)
        for (const auto& [left_size, right_size] : state_sizes)
            valid &= evaluation.evaluate(left_size, right_size, 1000 + 20000 * generation++);
    const auto counts = measured.finish();

    EXPECT_TRUE(valid);
    EXPECT_EQ(counts.allocated, 0);
    EXPECT_EQ(counts.deallocated, 0);
}
}  // namespace

TEST(YggdrasilTests, DatabaseAllocationTrackingCountsOrdinaryAndAlignedStorage)
{
    allocation_tracking::Scope measured;
    // Explicit allocation-function calls avoid new-expression allocation elision.
    auto* scalar = ::operator new(32);
    auto* array = ::operator new[](32);
    auto* aligned = ::operator new(64, std::align_val_t(64));
    auto* aligned_array = ::operator new[](64, std::align_val_t(64));
    ::operator delete(scalar);
    ::operator delete[](array);
    ::operator delete(aligned, std::align_val_t(64));
    ::operator delete[](aligned_array, std::align_val_t(64));
    const auto counts = measured.finish();
    EXPECT_EQ(counts.allocated, 4);
    EXPECT_EQ(counts.deallocated, 4);
}

TEST(YggdrasilTests, UnorderedMultiMapWarmedRebuildsAllocateAndFreeNothing)
{
    UnorderedMultiMap<size_t, size_t> map;
    map.reserve(1024);
    bool valid = true;
    allocation_tracking::Scope measured;
    for (size_t repeat = 0; repeat < 4; ++repeat)
        for (const auto count : { size_t(0), size_t(1), size_t(7), size_t(511), size_t(1024) })
            for (const bool duplicates : { false, true })
            {
                map.clear();
                map.reserve(count);  // Includes reserve(0) on retained storage.
                const auto key_count = duplicates ? std::min(count, size_t(17)) : count;
                const auto offset = repeat * 2000;
                for (size_t i = 0; i < count; ++i)
                    map.insert(offset + i % key_count, i);
                size_t seen = 0;
                size_t sum = 0;
                for (size_t key = 0; key < key_count; ++key)
                    for (const auto value : map.values(offset + key))
                    {
                        valid &= value % key_count == key;
                        ++seen;
                        sum += value;
                    }
                valid &= seen == count && map.size() == count;
                valid &= sum == (count == 0 ? 0 : count * (count - 1) / 2);
            }
    map.clear();
    const auto counts = measured.finish();
    EXPECT_TRUE(valid);
    EXPECT_EQ(counts.allocated, 0);
    EXPECT_EQ(counts.deallocated, 0);
}

TEST(YggdrasilTests, UnorderedMultiMapWarmedErasureReusesSlotsAcrossNovelKeys)
{
    UnorderedMultiMap<size_t, size_t> map;
    constexpr size_t count = 1024;
    map.reserve(count);
    for (size_t value = 0; value < count; ++value)
        map.insert(value, value);
    bool valid = true;
    const auto advance = [&](size_t generation)
    {
        for (size_t i = 0; i < count; ++i)
        {
            const auto old_value = generation * count + i;
            valid &= map.erase(old_value, old_value);
            map.insert(old_value + count, old_value + count);
        }
        valid &= map.size() == count;
    };
    for (size_t generation = 0; generation < 16; ++generation)
        advance(generation);
    const auto retained = map.memory_usage();

    allocation_tracking::Scope measured;
    for (size_t generation = 16; generation < 272; ++generation)
        advance(generation);
    const auto counts = measured.finish();
    EXPECT_TRUE(valid);
    EXPECT_EQ(counts.allocated, 0);
    EXPECT_EQ(counts.deallocated, 0);
    EXPECT_EQ(map.memory_usage(), retained);
}

TEST(YggdrasilTests, DatabaseWarmedEvaluationReusesAllStorageAcrossChangingStates)
{
    Evaluation evaluation;
    expect_no_allocations_after_warmup(evaluation);
}

TEST(YggdrasilTests, DatabaseWarmedPooledIntermediatesAllocateAndFreeNothing)
{
    PooledEvaluation evaluation;
    expect_no_allocations_after_warmup(evaluation);
}

TEST(YggdrasilTests, DatabaseWarmedCachedJoinsAllocateAndFreeNothing)
{
    CachedJoinEvaluation evaluation;
    expect_no_allocations_after_warmup(evaluation);
}

TEST(YggdrasilTests, DatabaseWarmedPreparedPooledEvaluationAllocatesAndFreesNothing)
{
    PreparedPooledEvaluation evaluation;
    expect_no_allocations_after_warmup(evaluation);
}

TEST(YggdrasilTests, DatabaseWarmedRelationInterningRetainsStorageAcrossReorderedArities)
{
    InterningEvaluation evaluation;
    bool valid = true;
    for (uint_t generation = 0; generation < 8; ++generation)
        valid &= evaluation.evaluate(generation % 2, generation);
    ASSERT_TRUE(valid);

    const auto* schema_builder_storage = evaluation.schema_builder.span().data();
    const auto* schema_data_storage = evaluation.schema_data.values.data();
    const auto schema_data_capacity = evaluation.schema_data.values.allocated_size_;

    allocation_tracking::Scope measured;
    for (uint_t generation = 8; generation < 108; ++generation)
        valid &= evaluation.evaluate(generation % 2, generation);
    const auto counts = measured.finish();
    EXPECT_TRUE(valid);
    EXPECT_EQ(counts.allocated, 0);
    EXPECT_EQ(counts.deallocated, 0);
    EXPECT_EQ(evaluation.schema_builder.span().data(), schema_builder_storage);
    EXPECT_EQ(evaluation.schema_data.values.data(), schema_data_storage);
    EXPECT_EQ(evaluation.schema_data.values.allocated_size_, schema_data_capacity);
}

TEST(YggdrasilTests, DatabaseWarmedTypedTupleInsertionAllocatesAndFreesNothing)
{
    auto relation = Builder<Relation<TypeList<ColumnIndex>>>({ ColumnIndex(0), ColumnIndex(1) });
    const auto row = std::tuple { ColumnIndex(2), ColumnIndex(5) };
    for (size_t i = 0; i < 8; ++i)
    {
        relation.clear();
        ASSERT_EQ(relation.insert(row), 0);
    }
    auto valid = true;
    allocation_tracking::Scope measured;
    for (size_t i = 0; i < 1000; ++i)
    {
        relation.clear();
        valid &= relation.insert(row) == 0;
        valid &= relation.insert(row) == 0;
        valid &= relation.contains(row);
        valid &= relation.size() == 1;
    }
    const auto counts = measured.finish();
    EXPECT_TRUE(valid);
    EXPECT_EQ(counts.allocated, 0);
    EXPECT_EQ(counts.deallocated, 0);
}

TEST(YggdrasilTests, DatabaseTypedTupleMembershipAcrossViewsAllocatesAndFreesNothing)
{
    auto repository = RelationRepositoryFactory<TypeList<ColumnIndex>>().create();
    auto relation = Builder<Relation<TypeList<ColumnIndex>>>({ ColumnIndex(0), ColumnIndex(1) });
    const auto row = std::tuple { ColumnIndex(2), ColumnIndex(5) };
    relation.insert(row);
    const auto indexed = insert(repository, relation).first;
    const auto data_view = make_view(indexed.get_data(), repository);
    const auto builder_view = make_view(relation, repository);
    auto other = Builder<Relation<TypeList<ColumnIndex>>>({ ColumnIndex(0), ColumnIndex(1) });
    const auto other_values = std::tuple { ColumnIndex(2), ColumnIndex(6) };
    other.insert(other_values);
    const auto other_indexed = insert(repository, other).first;
    ASSERT_TRUE(other_indexed.contains(other_values));
    const auto missing = other_values;
    auto valid = true;
    allocation_tracking::Scope measured;
    for (size_t i = 0; i < 1000; ++i)
    {
        valid &= relation.contains(row) && builder_view.contains(row) && data_view.contains(row) && indexed.contains(row);
        valid &= !relation.contains(missing) && !builder_view.contains(missing) && !data_view.contains(missing) && !indexed.contains(missing);
    }
    const auto counts = measured.finish();
    EXPECT_TRUE(valid);
    EXPECT_EQ(counts.allocated, 0);
    EXPECT_EQ(counts.deallocated, 0);
}

namespace
{
struct IncrementalEvaluation
{
    static constexpr size_t rows = 128;
    static constexpr size_t changed_rows = 32;
    static constexpr size_t fanout = 8;
    incremental::JoinEvaluator<Values> joining { JoinPlan<Values>({ ColumnIndex(1), ColumnIndex(2) }, { ColumnIndex(1), ColumnIndex(3) }) };
    const Builder<Relation<Values>> unchanged { ColumnIndex(1), ColumnIndex(3) };
    // Every output has eight join witnesses. Replacing a source row exercises
    // both support counts and mutable result storage without retaining history.
    incremental::ProjectionEvaluator<Values> projecting { ProjectionPlan<Values>({ ColumnIndex(1), ColumnIndex(2), ColumnIndex(3) }, { ColumnIndex(2) }) };
    incremental::Delta<Values> delta { ColumnIndex(1), ColumnIndex(2) };
    std::array<uint_t, rows> values {};
    Workspace<Values> workspace;

    IncrementalEvaluation()
    {
        Builder<Relation<Values>> initial { ColumnIndex(1), ColumnIndex(2) };
        for (size_t i = 0; i < rows; ++i)
        {
            values[i] = static_cast<uint_t>(i);
            initial.insert(cells(static_cast<uint_t>(i % key_count), values[i]));
        }
        Builder<Relation<Values>> fixed { ColumnIndex(1), ColumnIndex(3) };
        for (uint_t key = 0; key < key_count; ++key)
            for (uint_t match = 0; match < fanout; ++match)
                fixed.insert(cells(key, match));
        joining.initialize(initial, fixed, workspace);
        projecting.initialize(joining.get_result(), workspace);
    }

    bool evaluate(uint_t generation)
    {
        auto valid = true;
        const auto replace = [&](size_t begin, uint_t phase)
        {
            delta.clear();
            for (size_t i = begin; i < begin + changed_rows; ++i)
            {
                const auto key = static_cast<uint_t>(i % key_count);
                delta.removed.insert(cells(key, values[i]));
                values[i] = static_cast<uint_t>(i + (phase == 0 ? 0 : (generation * 3 + phase) * rows));
                delta.added.insert(cells(key, values[i]));
            }
            joining.update(delta.added, delta.removed, unchanged, unchanged, workspace);
            const auto& joined_delta = joining.get_delta();
            projecting.update(joined_delta.added, joined_delta.removed, workspace);
            valid &= joining.get_result().size() == rows * fanout;
            valid &= projecting.get_result().size() == rows;
            valid &= joined_delta.added.size() == changed_rows * fanout;
            valid &= joined_delta.removed.size() == changed_rows * fanout;
            valid &= projecting.get_delta().added.size() == changed_rows;
            valid &= projecting.get_delta().removed.size() == changed_rows;
            for (size_t i = begin; i < begin + changed_rows; ++i)
                valid &= projecting.get_result().contains(cells(values[i]));
        };
        replace(0, 1);
        replace(changed_rows, 2);
        replace(changed_rows, 0);
        replace(0, 0);
        replace(2 * changed_rows, 3);
        replace(2 * changed_rows, 0);
        for (size_t i = 0; i < rows; ++i)
            valid &= values[i] == i && projecting.get_result().contains(cells(static_cast<uint_t>(i)));
        return valid;
    }

    size_t memory_usage() const
    {
        return joining.memory_usage() + projecting.memory_usage() + delta.memory_usage() + unchanged.memory_usage() + workspace.row.capacity() * sizeof(uint_t);
    }
};
}  // namespace

TEST(YggdrasilTests, DatabaseWarmedIncrementalForwardUndoAndSiblingUpdatesRetainStorage)
{
    IncrementalEvaluation evaluation;
    auto valid = true;
    for (uint_t generation = 0; generation < 16; ++generation)
        valid &= evaluation.evaluate(generation);
    ASSERT_TRUE(valid);
    const auto retained_bytes = evaluation.memory_usage();

    allocation_tracking::Scope measured;
    // Novel tuples on every cycle detect storage growing with historical values,
    // even though the live input/result cardinalities remain bounded throughout.
    for (uint_t generation = 16; generation < 272; ++generation)
    {
        valid &= evaluation.evaluate(generation);
        valid &= evaluation.memory_usage() == retained_bytes;
    }
    const auto counts = measured.finish();
    EXPECT_TRUE(valid);
    EXPECT_EQ(counts.allocated, 0);
    EXPECT_EQ(counts.deallocated, 0);
    EXPECT_EQ(evaluation.memory_usage(), retained_bytes);
}

namespace
{
struct ChangingJoinEvaluation
{
    static constexpr size_t rows = 128;
    static constexpr size_t witnesses = 8;
    // Partial key groups force mutable-index bucket updates as well as complete
    // key disappearance; moved last rows must remain reachable after erasure.
    static constexpr size_t changed_rows = 17;

    incremental::JoinEvaluator<Values> joining { JoinPlan<Values>({ ColumnIndex(1), ColumnIndex(2) }, { ColumnIndex(1), ColumnIndex(3) }) };
    incremental::Delta<Values> lhs_delta { ColumnIndex(1), ColumnIndex(2) };
    incremental::Delta<Values> rhs_delta { ColumnIndex(1), ColumnIndex(3) };
    std::array<std::array<uint_t, 2>, rows> values {};
    Workspace<Values> workspace;

    ChangingJoinEvaluation()
    {
        Builder<Relation<Values>> lhs { ColumnIndex(1), ColumnIndex(2) };
        Builder<Relation<Values>> rhs { ColumnIndex(1), ColumnIndex(3) };
        for (size_t i = 0; i < rows; ++i)
        {
            values[i] = { static_cast<uint_t>(i / witnesses), static_cast<uint_t>(i) };
            lhs.insert(packed(values[i]));
            rhs.insert(packed(values[i]));
        }
        joining.initialize(lhs, rhs, workspace);
    }

    bool evaluate(uint_t generation)
    {
        auto valid = true;
        const auto replace = [&](size_t begin, uint_t phase)
        {
            lhs_delta.clear();
            rhs_delta.clear();
            const auto offset = phase == 0 ? uint_t { 0 } : static_cast<uint_t>((generation * 3 + phase) * rows);
            for (size_t i = begin; i < begin + changed_rows; ++i)
            {
                lhs_delta.removed.insert(packed(values[i]));
                rhs_delta.removed.insert(packed(values[i]));
                values[i] = { static_cast<uint_t>(i / witnesses + offset), static_cast<uint_t>(i + offset) };
                lhs_delta.added.insert(packed(values[i]));
                rhs_delta.added.insert(packed(values[i]));
            }
            // Overlapping changes exercise old/old, old/new, and new/new join
            // matches without double-reporting the tuples affected on both sides.
            joining.update(lhs_delta.added, lhs_delta.removed, rhs_delta.added, rhs_delta.removed, workspace);
            size_t expected = 0;
            for (const auto& lhs : values)
                for (const auto& rhs : values)
                    if (lhs[0] == rhs[0])
                    {
                        ++expected;
                        valid &= joining.get_result().contains(cells(lhs[0], lhs[1], rhs[1]));
                    }
            valid &= joining.get_result().size() == expected;
        };
        replace(0, 1);
        replace(changed_rows, 2);
        replace(changed_rows, 0);
        replace(0, 0);
        replace(2 * changed_rows, 3);
        replace(2 * changed_rows, 0);
        for (size_t i = 0; i < rows; ++i)
            valid &= values[i][0] == i / witnesses && values[i][1] == i;
        return valid;
    }

    size_t memory_usage() const
    {
        return joining.memory_usage() + lhs_delta.memory_usage() + rhs_delta.memory_usage() + workspace.row.capacity() * sizeof(uint_t)
               + workspace.join_index.memory_usage();
    }
};
}  // namespace

TEST(YggdrasilTests, DatabaseWarmedChangingJoinReusesStorageAcrossNovelKeysAndCompaction)
{
    ChangingJoinEvaluation evaluation;
    auto valid = true;
    for (uint_t generation = 0; generation < 16; ++generation)
        valid &= evaluation.evaluate(generation);
    ASSERT_TRUE(valid);
    const auto retained_bytes = evaluation.memory_usage();

    allocation_tracking::Scope measured;
    for (uint_t generation = 16; generation < 272; ++generation)
    {
        valid &= evaluation.evaluate(generation);
        valid &= evaluation.memory_usage() == retained_bytes;
    }
    const auto counts = measured.finish();
    EXPECT_TRUE(valid);
    EXPECT_EQ(counts.allocated, 0);
    EXPECT_EQ(counts.deallocated, 0);
    EXPECT_EQ(evaluation.memory_usage(), retained_bytes);
}

struct AllocationRangeFillFailure
{
};

template<bool ThreadSafe>
void test_warmed_failed_range_insertion()
{
    auto set = RawArraySet<int, 1, ThreadSafe>(3);
    const auto values = std::array { 11, 12, 13 };
    auto reads = size_t { 0 };
    auto throw_at = size_t { 4 };
    const auto row = values
                     | std::views::transform(
                         [&](int value)
                         {
                             if (reads++ == throw_at)
                                 throw AllocationRangeFillFailure {};
                             return value;
                         });
    auto valid = true;
    const auto cycle = [&]
    {
        set.clear();
        reads = 0;
        throw_at = 4;
        try
        {
            set.insert(row);
            valid = false;
        }
        catch (const AllocationRangeFillFailure&)
        {
            valid &= set.empty();
        }
        throw_at = std::numeric_limits<size_t>::max();
        valid &= set.insert(row) == 0;
        valid &= set.insert(row) == 0;
        valid &= set.contains(row);
        valid &= set.size() == 1;
    };
    cycle();
    ASSERT_TRUE(valid);
    allocation_tracking::Scope measured;
    for (size_t i = 0; i < 100; ++i)
        cycle();
    const auto counts = measured.finish();
    EXPECT_TRUE(valid);
    EXPECT_EQ(counts.allocated, 0);
    EXPECT_EQ(counts.deallocated, 0);
}

TEST(YggdrasilTests, DatabaseWarmedFailedRangeInsertionRetainsAllocatedStorage)
{
    // C++ exception-runtime allocation is outside the operator-new tracking above.
    test_warmed_failed_range_insertion<false>();
    test_warmed_failed_range_insertion<true>();
}

TEST(YggdrasilTests, DatabaseWarmedMixedWidthTuplesAndOperationsRetainStorage)
{
    using MixedValues = TypeList<uint_t, double, bool>;
    Builder<Columns<MixedValues>> left_columns;
    left_columns.push_back<uint_t>(ColumnIndex(1));
    left_columns.push_back<double>(ColumnIndex(2));
    left_columns.push_back<bool>(ColumnIndex(3));
    Builder<Columns<MixedValues>> right_columns;
    right_columns.push_back<double>(ColumnIndex(2));
    right_columns.push_back<uint_t>(ColumnIndex(4));
    Builder<Relation<MixedValues>> left(left_columns), right(right_columns);
    const JoinPlan<MixedValues> joining(left.columns(), right.columns());
    const ProjectionPlan<MixedValues> projecting(joining.output_columns(), { ColumnIndex(2), ColumnIndex(3) });
    Builder<Relation<MixedValues>> joined(joining.output_columns()), projected(projecting.output_columns());
    Workspace<MixedValues> workspace;
    const auto cycle = [&](uint_t generation)
    {
        left.clear();
        right.clear();
        for (uint_t i = 0; i < 12; ++i)
        {
            left.insert(std::tuple { generation + i, double(i % 4), i % 2 != 0 });
            right.insert(std::tuple { double(i % 4), generation + i });
        }
        join(left, right, joining, joined, workspace);
        project(joined, projecting, projected, workspace);
        return joined.size() == 36 && projected.size() == 4 && projected.contains(std::tuple { 1.0, true }) && projected.contains(std::tuple { 2.0, false });
    };
    for (uint_t generation = 0; generation < 8; ++generation)
        ASSERT_TRUE(cycle(generation));
    bool valid = true;
    allocation_tracking::Scope measured;
    for (uint_t generation = 0; generation < 100; ++generation)
        valid &= cycle(generation);
    const auto counts = measured.finish();
    EXPECT_TRUE(valid);
    EXPECT_EQ(counts.allocated, 0);
    EXPECT_EQ(counts.deallocated, 0);
}

}  // namespace ygg::tests
