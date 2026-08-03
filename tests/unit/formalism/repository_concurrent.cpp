/*
 * Copyright (C) 2026 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "gtest/gtest.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <cstdint>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>
#include <yggdrasil/buffer/buffer.hpp>
#include <yggdrasil/containers/bit_packed_array_set.hpp>
#include <yggdrasil/containers/block_array_set.hpp>
#include <yggdrasil/containers/indexed_hash_set.hpp>
#include <yggdrasil/containers/vector.hpp>
#include <yggdrasil/formalism/binding_data.hpp>
#include <yggdrasil/formalism/binding_view.hpp>
#include <yggdrasil/formalism/repository.hpp>
#include <yggdrasil/ids/index_mixins.hpp>

namespace ygg::tests
{
struct ConcurrentElement;
struct ConcurrentSerializedElement;
struct ConcurrentRelation;
struct ConcurrentObjectTag;
struct ConcurrentPackedObjectTag;
}

namespace ygg
{
template<>
struct Index<tests::ConcurrentElement> : IndexMixin<Index<tests::ConcurrentElement>>
{
    using Base = IndexMixin<Index<tests::ConcurrentElement>>;
    using Base::Base;
};

template<>
struct Data<tests::ConcurrentElement>
{
    Index<tests::ConcurrentElement> index;
    std::uint64_t value = 0;

    auto identifying_members() const noexcept { return std::tie(value); }
};

template<>
struct Index<tests::ConcurrentSerializedElement> : IndexMixin<Index<tests::ConcurrentSerializedElement>>
{
    using Base = IndexMixin<Index<tests::ConcurrentSerializedElement>>;
    using Base::Base;
};

template<>
struct Data<tests::ConcurrentSerializedElement>
{
    Index<tests::ConcurrentSerializedElement> index;
    IndexList<tests::ConcurrentElement> values;

    Data() = default;
    Data(const Data&) = delete;
    Data& operator=(const Data&) = delete;
    Data(Data&&) = default;
    Data& operator=(Data&&) = default;

    auto cista_members() const noexcept { return std::tie(index, values); }
    auto identifying_members() const noexcept { return std::tie(values); }
};

template<>
struct Index<tests::ConcurrentRelation> : IndexMixin<Index<tests::ConcurrentRelation>>
{
    using Base = IndexMixin<Index<tests::ConcurrentRelation>>;
    using Base::Base;
};
}

namespace ygg::formalism
{
template<>
struct RelationRepositoryTraits<tests::ConcurrentPackedObjectTag>
{
    using storage_type = BitPackedArraySetStorage;
};
}

namespace ygg::tests
{
namespace
{
constexpr std::size_t kThreads = 8;

template<typename Function>
void run_threads(std::size_t count, Function&& function)
{
    auto start = std::barrier<>(static_cast<std::ptrdiff_t>(count));
    auto threads = std::vector<std::jthread> {};
    threads.reserve(count);

    for (std::size_t thread = 0; thread < count; ++thread)
    {
        threads.emplace_back(
            [&, thread]
            {
                start.arrive_and_wait();
                function(thread);
            });
    }

    for (auto& thread : threads)
        thread.join();
}

template<typename ObjectTag>
using Binding = formalism::RelationBinding<ConcurrentRelation, ObjectTag>;

template<typename ObjectTag>
Data<Binding<ObjectTag>> make_binding(Index<ConcurrentRelation> relation, uint_t key)
{
    using Object = formalism::Object<ObjectTag>;

    auto objects = IndexList<Object> {};
    for (uint_t i = 0; i < 4; ++i)
        objects.push_back(Index<Object>((key + i * 997U) & 4095U));
    return Data<Binding<ObjectTag>>(relation, 4, std::move(objects));
}

template<typename ObjectTag>
using ConcurrentRepository = formalism::Repository<formalism::ConcurrentSymbolRepository<ConcurrentElement, ConcurrentSerializedElement>,
                                                   formalism::ConcurrentRelationRepository<ObjectTag, ConcurrentRelation>>;

using SequentialRepository = formalism::Repository<formalism::SymbolRepository<ConcurrentElement, ConcurrentSerializedElement>,
                                                   formalism::RelationRepository<ConcurrentObjectTag, ConcurrentRelation>>;
using MixedRepository = formalism::Repository<formalism::ConcurrentSymbolRepository<ConcurrentElement, ConcurrentSerializedElement>,
                                              formalism::RelationRepository<ConcurrentObjectTag, ConcurrentRelation>>;

static_assert(ConcurrentRepository<ConcurrentObjectTag>::thread_safe);
static_assert(!SequentialRepository::thread_safe);
static_assert(!MixedRepository::thread_safe);

template<typename Set, typename Element, typename IndexType>
void expect_concurrent_complete_miss_canonicalizes(Set& set, const Element& element, IndexType expected)
{
    const auto hash = Set::hash(element);
    auto created = std::atomic_size_t { 0 };
    auto errors = std::atomic_size_t { 0 };

    run_threads(kThreads,
                [&](std::size_t)
                {
                    for (size_t attempt = 0; attempt < 256; ++attempt)
                    {
                        const auto [index, was_created] = set.complete_miss_with_hash(hash, element);
                        created.fetch_add(was_created, std::memory_order_relaxed);
                        if (index != expected)
                            errors.fetch_add(1, std::memory_order_relaxed);
                    }
                });

    EXPECT_EQ(errors.load(), 0);
    EXPECT_EQ(created.load(), 1);
    EXPECT_EQ(set.size(), 1);
    const auto found = set.find_with_hash(element, hash);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(*found, expected);
    const auto unsafe_found = set.find_unsafe_with_hash(element, hash);
    ASSERT_TRUE(unsafe_found.has_value());
    EXPECT_EQ(*unsafe_found, expected);
    EXPECT_THROW(set.insert_new_with_hash(hash, element), std::logic_error);
    EXPECT_EQ(set.size(), 1);
}

TEST(YggdrasilTests, ConcurrentSetCompletesObservedMisses)
{
    {
        using Set = IndexedHashSet<ConcurrentElement, Hash<Data<ConcurrentElement>>, EqualTo<Data<ConcurrentElement>>, 32, true>;
        auto set = Set {};
        const auto element = Data<ConcurrentElement> { Index<ConcurrentElement>(0), 7 };
        expect_concurrent_complete_miss_canonicalizes(set, element, Index<ConcurrentElement>(0));
    }

    {
        using Set =
            buffer::IndexedHashSet<ConcurrentSerializedElement, Hash<Data<ConcurrentSerializedElement>>, EqualTo<Data<ConcurrentSerializedElement>>, 32, true>;
        auto arena = buffer::SegmentedBuffer {};
        auto bytes = buffer::Buffer {};
        auto set = Set(bytes, arena);
        auto element = Data<ConcurrentSerializedElement> {};
        element.index = Index<ConcurrentSerializedElement>(0);
        element.values.push_back(Index<ConcurrentElement>(7));
        expect_concurrent_complete_miss_canonicalizes(set, element, Index<ConcurrentSerializedElement>(0));
    }

    {
        using Set = BlockArraySet<uint_t, bit::ForwardingBlockCoder<uint_t>, 16, true>;
        auto set = Set(4);
        const auto element = std::array<uint_t, 4> { 1, 2, 3, 4 };
        expect_concurrent_complete_miss_canonicalizes(set, element, uint_t { 0 });
    }

    {
        using Set = BitPackedArraySet<uint_t, bit::ForwardingBlockCoder<uint_t>, 16, true>;
        auto set = Set(4, 12);
        const auto element = std::array<uint_t, 4> { 1, 2, 3, 4 };
        expect_concurrent_complete_miss_canonicalizes(set, element, uint_t { 0 });
    }
}

template<typename ObjectTag>
void expect_duplicate_same_relation_insertions()
{
    auto repository = ConcurrentRepository<ObjectTag>(0, nullptr, formalism::RelationRepositoryConfig(12));
    const auto relation = Index<ConcurrentRelation>(0);
    constexpr uint_t rows = 512;
    constexpr auto invalid_row = std::numeric_limits<uint_t>::max();
    auto canonical_rows = std::vector<std::atomic<uint_t>>(rows);
    for (auto& row : canonical_rows)
        row.store(invalid_row, std::memory_order_relaxed);

    auto created = std::atomic_size_t { 0 };
    auto errors = std::atomic_size_t { 0 };
    run_threads(kThreads,
                [&](std::size_t thread)
                {
                    for (uint_t offset = 0; offset < rows; ++offset)
                    {
                        const auto key = (offset + static_cast<uint_t>(thread)) % rows;
                        const auto data = make_binding<ObjectTag>(relation, key);
                        const auto [view, was_created] = repository.get_or_create(data);
                        created.fetch_add(was_created, std::memory_order_relaxed);

                        const auto returned_row = view.get_index().row.get_value();
                        auto expected = invalid_row;
                        if (!canonical_rows[key].compare_exchange_strong(expected, returned_row, std::memory_order_relaxed) && expected != returned_row)
                            errors.fetch_add(1, std::memory_order_relaxed);
                    }
                });

    EXPECT_EQ(errors.load(), 0);
    EXPECT_EQ(created.load(), rows);
    EXPECT_EQ(repository.size(relation), rows);
    for (uint_t key = 0; key < rows; ++key)
    {
        const auto data = make_binding<ObjectTag>(relation, key);
        const auto found = repository.find(data);
        ASSERT_TRUE(found.has_value());
        EXPECT_EQ(found->get_index().row.get_value(), canonical_rows[key].load(std::memory_order_relaxed));
        EXPECT_TRUE(std::ranges::equal(found->get_data(), data.objects));
    }
}

template<typename ObjectTag>
void expect_unique_same_relation_insertions()
{
    auto repository = ConcurrentRepository<ObjectTag>(0, nullptr, formalism::RelationRepositoryConfig(12));
    const auto relation = Index<ConcurrentRelation>(0);
    constexpr uint_t rows_per_thread = 256;
    constexpr size_t expected_rows = kThreads * rows_per_thread;
    auto created = std::atomic_size_t { 0 };
    auto errors = std::atomic_size_t { 0 };

    run_threads(kThreads,
                [&](std::size_t thread)
                {
                    for (uint_t row = 0; row < rows_per_thread; ++row)
                    {
                        const auto key = static_cast<uint_t>(thread) * rows_per_thread + row;
                        const auto data = make_binding<ObjectTag>(relation, key);
                        const auto [view, was_created] = repository.get_or_create(data);
                        created.fetch_add(was_created, std::memory_order_relaxed);
                        if (!std::ranges::equal(view.get_data(), data.objects))
                            errors.fetch_add(1, std::memory_order_relaxed);
                    }
                });

    EXPECT_EQ(errors.load(), 0);
    EXPECT_EQ(created.load(), expected_rows);
    EXPECT_EQ(repository.size(relation), expected_rows);

    auto seen = std::vector<bool>(expected_rows, false);
    for (uint_t key = 0; key < expected_rows; ++key)
    {
        const auto data = make_binding<ObjectTag>(relation, key);
        const auto found = repository.find(data);
        ASSERT_TRUE(found.has_value());
        const auto row = found->get_index().row.get_value();
        ASSERT_LT(row, expected_rows);
        EXPECT_FALSE(seen[row]);
        seen[row] = true;
        EXPECT_TRUE(std::ranges::equal(found->get_data(), data.objects));
    }
}

Data<Binding<ConcurrentPackedObjectTag>> make_packed_binding(Index<ConcurrentRelation> relation, uint_t key, bool valid)
{
    using Object = formalism::Object<ConcurrentPackedObjectTag>;
    auto objects = IndexList<Object> {};
    for (uint_t i = 0; i < 4; ++i)
        objects.push_back(Index<Object>((key >> (i * 3U)) & 7U));
    if (!valid)
        objects.back() = Index<Object>(8);
    return Data<Binding<ConcurrentPackedObjectTag>>(relation, 4, std::move(objects));
}

TEST(YggdrasilTests, FormalismConcurrentRepositoryCanonicalizesTrivialAndSerializedSymbols)
{
    auto repository = ConcurrentRepository<ConcurrentObjectTag>(0, nullptr, formalism::RelationRepositoryConfig(12));
    auto errors = std::atomic_size_t { 0 };
    auto trivial_created = std::atomic_size_t { 0 };
    constexpr uint_t num_trivial = 1024;

    run_threads(kThreads,
                [&](std::size_t thread)
                {
                    for (uint_t offset = 0; offset < num_trivial; ++offset)
                    {
                        const auto key = (offset + static_cast<uint_t>(thread)) % num_trivial;
                        auto data = Data<ConcurrentElement> {};
                        data.value = key;
                        const auto [view, created] = repository.get_or_create(data);
                        trivial_created.fetch_add(created, std::memory_order_relaxed);
                        if (view.get_data().value != key || view.get_data().index != view.get_index())
                            errors.fetch_add(1, std::memory_order_relaxed);
                    }
                });

    EXPECT_EQ(errors.load(), 0);
    EXPECT_EQ(trivial_created.load(), num_trivial);
    EXPECT_EQ(repository.size<ConcurrentElement>(), num_trivial);

    auto seen = std::vector<bool>(num_trivial, false);
    for (uint_t key = 0; key < num_trivial; ++key)
    {
        auto data = Data<ConcurrentElement> {};
        data.value = key;
        const auto found = repository.find(data);
        ASSERT_TRUE(found.has_value());
        const auto index = found->get_index().get_value();
        ASSERT_LT(index, num_trivial);
        EXPECT_FALSE(seen[index]);
        seen[index] = true;
    }

    auto serialized_created = std::atomic_size_t { 0 };
    constexpr uint_t num_serialized = 256;
    run_threads(kThreads,
                [&](std::size_t thread)
                {
                    for (uint_t offset = 0; offset < num_serialized; ++offset)
                    {
                        const auto key = (offset + static_cast<uint_t>(thread)) % num_serialized;
                        auto data = Data<ConcurrentSerializedElement> {};
                        data.values.push_back(Index<ConcurrentElement>(key));
                        const auto [view, created] = repository.get_or_create(data);
                        serialized_created.fetch_add(created, std::memory_order_relaxed);
                        if (view.get_data().values.size() != 1 || view.get_data().values.front().get_value() != key
                            || view.get_data().index != view.get_index())
                            errors.fetch_add(1, std::memory_order_relaxed);
                    }
                });

    EXPECT_EQ(errors.load(), 0);
    EXPECT_EQ(serialized_created.load(), num_serialized);
    EXPECT_EQ(repository.size<ConcurrentSerializedElement>(), num_serialized);
}

TEST(YggdrasilTests, FormalismConcurrentRepositoryUsesIndependentRelationGroupLanes)
{
    using ObjectTag = ConcurrentObjectTag;
    auto lane_repository = ConcurrentRepository<ObjectTag>(1, nullptr, formalism::RelationRepositoryConfig(12));
    constexpr uint_t lane_rows = 256;
    run_threads(kThreads,
                [&](std::size_t thread)
                {
                    const auto lane = Index<ConcurrentRelation>(static_cast<uint_t>(thread));
                    for (uint_t key = 0; key < lane_rows; ++key)
                        lane_repository.get_or_create(make_binding<ObjectTag>(lane, key));
                });

    for (uint_t lane = 0; lane < kThreads; ++lane)
        EXPECT_EQ(lane_repository.size(Index<ConcurrentRelation>(lane)), lane_rows);
}

TEST(YggdrasilTests, FormalismConcurrentRepositorySupportsHighestRelationLane)
{
    using ObjectTag = ConcurrentObjectTag;
    auto repository = ConcurrentRepository<ObjectTag>(0, nullptr, formalism::RelationRepositoryConfig(12));
    const auto relation = Index<ConcurrentRelation>(std::numeric_limits<uint_t>::max() - 1);

    const auto [view, created] = repository.get_or_create(make_binding<ObjectTag>(relation, 7));

    EXPECT_TRUE(created);
    EXPECT_EQ(view.get_index().relation, relation);
    EXPECT_EQ(repository.size(relation), 1);
}

TEST(YggdrasilTests, FormalismRepositoryRejectsReservedRelationIndex)
{
    using ObjectTag = ConcurrentObjectTag;
    const auto relation = Index<ConcurrentRelation>::max();
    auto sequential = SequentialRepository(0, nullptr, formalism::RelationRepositoryConfig(12));
    auto concurrent = ConcurrentRepository<ObjectTag>(0, nullptr, formalism::RelationRepositoryConfig(12));

    EXPECT_THROW(sequential.get_or_create(make_binding<ObjectTag>(relation, 7)), std::invalid_argument);
    EXPECT_THROW(concurrent.get_or_create(make_binding<ObjectTag>(relation, 7)), std::invalid_argument);
}

TEST(YggdrasilTests, FormalismConcurrentPackedRepositoryRejectsWrongArityWithoutMutation)
{
    using ObjectTag = ConcurrentPackedObjectTag;
    using Object = formalism::Object<ObjectTag>;
    auto repository = ConcurrentRepository<ObjectTag>(0, nullptr, formalism::RelationRepositoryConfig(12));
    const auto relation = Index<ConcurrentRelation>(0);
    const auto [view, created] = repository.get_or_create(make_binding<ObjectTag>(relation, 7));
    ASSERT_TRUE(created);

    auto objects = IndexList<Object> {};
    objects.push_back(Index<Object>(0));
    objects.push_back(Index<Object>(1));
    objects.push_back(Index<Object>(2));
    const auto wrong = Data<Binding<ObjectTag>>(relation, 3, std::move(objects));
    static_assert(!noexcept(repository.find(wrong)));

    EXPECT_THROW(repository.find(wrong), std::invalid_argument);
    EXPECT_THROW(repository.get_or_create(wrong), std::invalid_argument);
    EXPECT_EQ(repository.size(relation), 1);
    EXPECT_EQ(view.get_data().size(), 4);
}

TEST(YggdrasilTests, FormalismConcurrentRepositoryCanonicalizesRelationBindings)
{
    expect_duplicate_same_relation_insertions<ConcurrentObjectTag>();
    expect_duplicate_same_relation_insertions<ConcurrentPackedObjectTag>();
}

TEST(YggdrasilTests, FormalismConcurrentRepositoryInsertsUniqueRowsIntoOneRelation)
{
    expect_unique_same_relation_insertions<ConcurrentObjectTag>();
    expect_unique_same_relation_insertions<ConcurrentPackedObjectTag>();
}

TEST(YggdrasilTests, FormalismConcurrentPackedViewsRemainStableWhileSharedBlocksGrow)
{
    using ObjectTag = ConcurrentPackedObjectTag;
    auto repository = ConcurrentRepository<ObjectTag>(0, nullptr, formalism::RelationRepositoryConfig(12));
    const auto relation = Index<ConcurrentRelation>(0);
    const auto [first, created] = repository.get_or_create(make_binding<ObjectTag>(relation, 0));
    ASSERT_TRUE(created);
    const auto first_data = first.get_data();

    auto stop = std::atomic_bool { false };
    auto errors = std::atomic_size_t { 0 };
    auto readers = std::vector<std::jthread> {};
    for (std::size_t i = 0; i < 4; ++i)
    {
        readers.emplace_back(
            [&]
            {
                while (!stop.load(std::memory_order_acquire))
                {
                    if (first_data[0].get_value() != 0 || first_data[1].get_value() != 997 || first_data[2].get_value() != 1994
                        || first_data[3].get_value() != 2991)
                        errors.fetch_add(1, std::memory_order_relaxed);
                }
            });
    }

    for (uint_t key = 1; key < 3000; ++key)
        repository.get_or_create(make_binding<ObjectTag>(relation, key));

    stop.store(true, std::memory_order_release);
    for (auto& reader : readers)
        reader.join();

    EXPECT_EQ(errors.load(), 0);
    EXPECT_EQ(repository.size(relation), 3000);
}

TEST(YggdrasilTests, FormalismConcurrentRepositoryReadsFrozenParentWhileGrowingChild)
{
    using ObjectTag = ConcurrentObjectTag;
    auto parent = ConcurrentRepository<ObjectTag>(0, nullptr, formalism::RelationRepositoryConfig(12));
    auto parent_data = Data<ConcurrentElement> {};
    parent_data.value = 7;
    const auto [parent_view, parent_created] = parent.get_or_create(parent_data);
    ASSERT_TRUE(parent_created);

    auto parent_serialized_data = Data<ConcurrentSerializedElement> {};
    parent_serialized_data.values.push_back(parent_view.get_index());
    const auto [parent_serialized_view, parent_serialized_created] = parent.get_or_create(parent_serialized_data);
    ASSERT_TRUE(parent_serialized_created);

    const auto relation = Index<ConcurrentRelation>(0);
    const auto [parent_binding_view, parent_binding_created] = parent.get_or_create(make_binding<ObjectTag>(relation, 7));
    ASSERT_TRUE(parent_binding_created);

    auto child = ConcurrentRepository<ObjectTag>(1, &parent, formalism::RelationRepositoryConfig(12));
    auto errors = std::atomic_size_t { 0 };
    constexpr uint_t rows_per_thread = 256;

    run_threads(kThreads,
                [&](std::size_t thread)
                {
                    auto inherited = Data<ConcurrentElement> {};
                    inherited.value = 7;
                    const auto [inherited_view, inherited_created] = child.get_or_create(inherited);
                    if (inherited_created || &inherited_view.get_context() != &parent)
                        errors.fetch_add(1, std::memory_order_relaxed);

                    auto inherited_serialized = Data<ConcurrentSerializedElement> {};
                    inherited_serialized.values.push_back(parent_view.get_index());
                    const auto [inherited_serialized_view, inherited_serialized_created] = child.get_or_create(inherited_serialized);
                    if (inherited_serialized_created || &inherited_serialized_view.get_context() != &parent
                        || inherited_serialized_view.get_data().values.front() != parent_view.get_index())
                        errors.fetch_add(1, std::memory_order_relaxed);

                    const auto [inherited_binding_view, inherited_binding_created] = child.get_or_create(make_binding<ObjectTag>(relation, 7));
                    if (inherited_binding_created || &inherited_binding_view.get_context() != &parent
                        || !std::ranges::equal(inherited_binding_view.get_data(), parent_binding_view.get_data()))
                        errors.fetch_add(1, std::memory_order_relaxed);

                    auto missing = Data<ConcurrentElement> {};
                    missing.value = std::numeric_limits<uint_t>::max();
                    if (child.find(missing).has_value())
                        errors.fetch_add(1, std::memory_order_relaxed);

                    for (uint_t row = 0; row < rows_per_thread; ++row)
                    {
                        auto local = Data<ConcurrentElement> {};
                        local.value = 1000 + static_cast<uint_t>(thread) * rows_per_thread + row;
                        const auto [local_view, created] = child.get_or_create(local);
                        if (!created || &local_view.get_context() != &child || local_view.get_data().value != local.value
                            || local_view.get_data().index != local_view.get_index())
                            errors.fetch_add(1, std::memory_order_relaxed);
                    }
                });

    EXPECT_EQ(errors.load(), 0);
    EXPECT_EQ(parent.size<ConcurrentElement>(), 1);
    EXPECT_EQ(parent.size<ConcurrentSerializedElement>(), 1);
    EXPECT_EQ(parent.size(relation), 1);
    EXPECT_EQ(child.size<ConcurrentElement>(), 1 + kThreads * rows_per_thread);
    EXPECT_EQ(parent_view.get_data().value, 7);
    EXPECT_EQ(parent_serialized_view.get_data().values.front(), parent_view.get_index());
    EXPECT_TRUE(std::ranges::equal(parent_binding_view.get_data(), make_binding<ObjectTag>(relation, 7).objects));
}

TEST(YggdrasilTests, FormalismConcurrentPackedInsertionFailureDoesNotPublishAndClearReusesStorage)
{
    using ObjectTag = ConcurrentPackedObjectTag;
    using Object = formalism::Object<ObjectTag>;
    auto repository = ConcurrentRepository<ObjectTag>(0, nullptr, formalism::RelationRepositoryConfig(3));
    const auto relation = Index<ConcurrentRelation>(0);

    auto invalid_objects = IndexList<Object> {};
    invalid_objects.push_back(Index<Object>(0));
    invalid_objects.push_back(Index<Object>(1));
    invalid_objects.push_back(Index<Object>(2));
    invalid_objects.push_back(Index<Object>(8));
    auto invalid = Data<Binding<ObjectTag>>(relation, 4, std::move(invalid_objects));
    EXPECT_THROW(repository.get_or_create(invalid), std::out_of_range);
    EXPECT_EQ(repository.size(relation), 0);

    auto valid_objects = IndexList<Object> {};
    for (uint_t value = 0; value < 4; ++value)
        valid_objects.push_back(Index<Object>(value));
    const auto [view, created] = repository.get_or_create(Data<Binding<ObjectTag>>(relation, 4, std::move(valid_objects)));
    EXPECT_TRUE(created);
    EXPECT_EQ(view.get_index().row, Index<formalism::Row>(0));

    repository.clear();
    EXPECT_EQ(repository.size(relation), 0);
    auto reused_objects = IndexList<Object> {};
    for (uint_t value = 4; value < 8; ++value)
        reused_objects.push_back(Index<Object>(value));
    const auto [reused, reused_created] = repository.get_or_create(Data<Binding<ObjectTag>>(relation, 4, std::move(reused_objects)));
    EXPECT_TRUE(reused_created);
    EXPECT_EQ(reused.get_index().row, Index<formalism::Row>(0));
}

TEST(YggdrasilTests, FormalismConcurrentPackedInsertionFailuresDoNotInterfereWithValidWriters)
{
    auto repository = ConcurrentRepository<ConcurrentPackedObjectTag>(0, nullptr, formalism::RelationRepositoryConfig(3));
    const auto relation = Index<ConcurrentRelation>(0);
    constexpr uint_t rows_per_thread = 128;
    constexpr size_t num_writers = kThreads / 2;
    auto created = std::atomic_size_t { 0 };
    auto rejected = std::atomic_size_t { 0 };
    auto errors = std::atomic_size_t { 0 };

    run_threads(kThreads,
                [&](std::size_t thread)
                {
                    for (uint_t row = 0; row < rows_per_thread; ++row)
                    {
                        const auto key = static_cast<uint_t>(thread / 2) * rows_per_thread + row;
                        try
                        {
                            const auto [view, was_created] = repository.get_or_create(make_packed_binding(relation, key, thread % 2 == 0));
                            if (thread % 2 == 0)
                            {
                                created.fetch_add(was_created, std::memory_order_relaxed);
                                if (view.get_data().size() != 4)
                                    errors.fetch_add(1, std::memory_order_relaxed);
                            }
                            else
                            {
                                errors.fetch_add(1, std::memory_order_relaxed);
                            }
                        }
                        catch (const std::out_of_range&)
                        {
                            if (thread % 2 == 0)
                                errors.fetch_add(1, std::memory_order_relaxed);
                            else
                                rejected.fetch_add(1, std::memory_order_relaxed);
                        }
                        catch (...)
                        {
                            errors.fetch_add(1, std::memory_order_relaxed);
                        }
                    }
                });

    EXPECT_EQ(errors.load(), 0);
    EXPECT_EQ(created.load(), num_writers * rows_per_thread);
    EXPECT_EQ(rejected.load(), num_writers * rows_per_thread);
    EXPECT_EQ(repository.size(relation), num_writers * rows_per_thread);

    for (uint_t key = 0; key < num_writers * rows_per_thread; ++key)
    {
        const auto data = make_packed_binding(relation, key, true);
        const auto found = repository.find(data);
        ASSERT_TRUE(found.has_value());
        EXPECT_TRUE(std::ranges::equal(found->get_data(), data.objects));
    }
}
}
}  // namespace ygg::tests
