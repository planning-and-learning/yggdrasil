/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <gtest/gtest.h>
#include <new>
#include <span>
#include <utility>
#include <yggdrasil/database/operations.hpp>
#include <yggdrasil/database/relation_pool.hpp>

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

namespace
{
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

void fill(Relation<>& relation, size_t rows, uint_t offset)
{
    relation.clear();
    for (size_t i = 0; i < rows; ++i)
        relation.insert({ static_cast<uint_t>(i % key_count), offset + static_cast<uint_t>(i) });
}

struct Evaluation
{
    Relation<> left { { 1, 2 } };
    Relation<> right { { 1, 3 } };
    Relation<> joined { { 1, 2, 3 } };
    Relation<> selected { { 1, 2, 3 } };
    Relation<> projected { { 3 } };
    Relation<> renamed_projection { { 6 } };
    Relation<> left_keys { { 1 } };
    Relation<> right_keys { { 1 } };
    Relation<> either_keys { { 1 } };
    Relation<> only_left_keys { { 1 } };
    Relation<> exists;
    Workspace<> workspace;
    const std::array<Column, 3> renamed_columns { 4, 5, 6 };
    const std::array<Column, 1> projected_columns { 6 };
    const ColumnsView renamed_schema { std::span<const Column>(renamed_columns) };
    const ColumnsView projected_schema { std::span<const Column>(projected_columns) };
    Columns retained_columns { 4, 5, 6 };

    bool evaluate(size_t left_size, size_t right_size, uint_t offset)
    {
        const auto schema_memory = retained_columns.memory_usage();
        retained_columns.assign(projected_schema);
        bool valid = std::ranges::equal(retained_columns.view(), projected_schema);
        retained_columns.assign(ColumnsView {});
        valid &= retained_columns.empty();
        retained_columns.assign(renamed_schema);
        valid &= std::ranges::equal(retained_columns.view(), renamed_schema);
        valid &= retained_columns.memory_usage() == schema_memory;
        fill(left, left_size, offset);
        fill(right, right_size, offset + 10000);
        join(left.view(), right.view(), joined, workspace);
        select(joined.view(), [](std::span<const uint_t> row) { return row[0] < key_count / 2; }, selected);
        project(joined.view(), { 3 }, projected, workspace);
        // Borrowed views and borrowed rename schemas are deliberately constructed
        // inside the measured path, so accidental schema copies are detected.
        auto renamed = rename(joined.view(), std::span<const Column>(renamed_columns));
        project(renamed, std::span<const Column>(projected_columns), renamed_projection, workspace);
        auto typed_renamed = rename(joined.view(), renamed_schema);
        valid &= typed_renamed.columns().data() == renamed_schema.data();
        valid &= &typed_renamed.storage() == &joined.storage();
        valid &= std::ranges::equal(typed_renamed.columns(), retained_columns.view());
        project(left.view(), { 1 }, left_keys, workspace);
        project(right.view(), { 1 }, right_keys, workspace);
        union_(left_keys.view(), right_keys.view(), either_keys);
        difference(left_keys.view(), right_keys.view(), only_left_keys);
        project(joined.view(), {}, exists, workspace);
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
            valid &= joined.contains({ 0, offset, offset + 10000 });
            valid &= projected.contains({ offset + 10000 });
            valid &= renamed_projection.contains({ offset + 10000 });
        }
        return valid;
    }
};

struct CachedJoinEvaluation
{
    RelationPool<> pool;
    UniqueObjectPoolPtr<Relation<>> build = pool.get_or_allocate({ 1, 2 });
    Relation<> probe { { 1, 3 } };
    JoinPlan forward { build->columns(), probe.columns() };
    JoinPlan reverse { probe.columns(), build->columns() };
    Relation<> forward_result { forward.output_columns() };
    Relation<> reverse_result { reverse.output_columns() };
    JoinIndexCache<> cache;
    Workspace<> workspace;

    CachedJoinEvaluation() { fill(*build, 128, 1000); }

    bool evaluate(size_t left_size, size_t right_size, uint_t offset)
    {
        bool valid = true;
        for (const auto probe_size : { left_size, right_size })
        {
            fill(probe, probe_size, offset);
            join(build->view(), probe.view(), forward, cache, JoinReuse { true, false }, forward_result, workspace);
            join(probe.view(), build->view(), reverse, cache, JoinReuse { false, true }, reverse_result, workspace);
            valid &= forward_result.size() == joined_size(build->size(), probe_size);
            valid &= reverse_result.size() == forward_result.size();
            valid &= cache.size() == 1;
            if (probe_size != 0)
            {
                valid &= forward_result.contains({ 0, 1000, offset });
                valid &= reverse_result.contains({ 0, offset, 1000 });
            }
        }
        return valid;
    }
};

struct PooledEvaluation
{
    Relation<> left { { 1, 2 } };
    Relation<> right { { 1, 3 } };
    RelationPool<> pool;
    Workspace<> workspace;

    bool evaluate(size_t left_size, size_t right_size, uint_t offset)
    {
        fill(left, left_size, offset);
        fill(right, right_size, offset + 10000);
        auto joined = pool.get_or_allocate({ 1, 2, 3 });
        auto left_keys = pool.get_or_allocate({ 1 });
        auto right_keys = pool.get_or_allocate({ 1 });
        auto either = pool.get_or_allocate({ 1 });
        auto exists = pool.get_or_allocate({});
        join(left.view(), right.view(), *joined, workspace);
        project(left.view(), { 1 }, *left_keys, workspace);
        project(right.view(), { 1 }, *right_keys, workspace);
        union_(left_keys->view(), right_keys->view(), *either);
        project(joined->view(), {}, *exists, workspace);
        bool valid = joined->size() == joined_size(left_size, right_size);
        const auto left_count = distinct_keys(left_size);
        const auto right_count = distinct_keys(right_size);
        valid &= either->size() == (left_count > right_count ? left_count : right_count);
        valid &= exists->size() == static_cast<size_t>(left_size != 0 && right_size != 0);
        if (left_size != 0 && right_size != 0)
            valid &= joined->contains({ 0, offset, offset + 10000 });
        // Returning these handles is also inside the measurement. In particular,
        // all three unary intermediates must coexist without new pool entries.
        return valid;
    }
};

struct PreparedPooledEvaluation
{
    // Sparse labels must not be interpreted as positions or dense-map sizes.
    static constexpr Column key_column = 900000001;
    static constexpr Column left_column = 3;
    static constexpr Column right_column = 800000001;
    Relation<> left { { left_column, key_column } };
    Relation<> right { { key_column, right_column } };
    JoinPlan joining { left.columns(), right.columns() };
    ProjectionPlan projecting { joining.output_columns(), { right_column, left_column } };
    ProjectionPlan projecting_left_keys { left.columns(), { key_column } };
    ProjectionPlan projecting_right_keys { right.columns(), { key_column } };
    ProjectionPlan testing_existence { joining.output_columns(), {} };
    const Columns alternate_join_columns { 7, 8, 9 };
    RelationPool<> pool;
    Workspace<> workspace;

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
                left.insert({ offset + static_cast<uint_t>(i), offset + static_cast<uint_t>(i % key_count) });
            const auto right_key_offset = offset + static_cast<uint_t>(disjoint ? key_count : 0);
            for (size_t i = 0; i < right_size; ++i)
                right.insert({ right_key_offset + static_cast<uint_t>(i % key_count), offset + 10000 + static_cast<uint_t>(i) });

            // Relabel the same pooled relation before each prepared checkout.
            // Both schema changes and view construction stay in the measurement.
            {
                auto relabeled = pool.get_or_allocate(alternate_join_columns.view());
                valid &= relabeled->empty();
                relabeled->insert({ offset, offset + 1, offset + 2 });
                const auto borrowed = relabeled->view();
                valid &= borrowed.size() == 1;
                valid &= std::ranges::equal(borrowed.columns(), alternate_join_columns.view());
            }
            auto joined = pool.get_or_allocate(joining.output_columns());
            valid &= joined->empty();
            valid &= std::ranges::equal(joined->columns(), joining.output_columns());
            auto projected = pool.get_or_allocate(projecting.output_columns());
            auto left_keys = pool.get_or_allocate(projecting_left_keys.output_columns());
            auto right_keys = pool.get_or_allocate(projecting_right_keys.output_columns());
            auto either = pool.get_or_allocate(projecting_left_keys.output_columns());
            auto exists = pool.get_or_allocate(testing_existence.output_columns());
            join(left.view(), right.view(), joining, *joined, workspace);
            project(joined->view(), projecting, *projected, workspace);
            project(left.view(), projecting_left_keys, *left_keys, workspace);
            project(right.view(), projecting_right_keys, *right_keys, workspace);
            union_(left_keys->view(), right_keys->view(), *either);
            project(joined->view(), testing_existence, *exists, workspace);

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
                valid &= joined->contains({ offset, offset, offset + 10000 });
                valid &= projected->contains({ offset + 10000, offset });
            }
            // Pool checkout and return both stay inside the measurement.
        }
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

}  // namespace ygg::tests
