/*
 * Copyright (C) 2026 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <urd/repr/sequences/sequence.hpp>
#include <urd/repr/values/dense_set.hpp>
#include <urd/store.hpp>
#include <urd/elements/flat.hpp>
#include <boost/mp11.hpp>
#include "yggdrasil/core/config.hpp"
#include "yggdrasil/formalism/row_data.hpp"
#include "yggdrasil/formalism/row_view.hpp"
#include "yggdrasil/formalism/repository.hpp"
#include "symbol_concepts.hpp"

#include "gtest/gtest.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <concepts>
#include <cstdint>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <urd/config.hpp>

namespace ygg::tests
{
struct ConcurrentElement;
struct ConcurrentSerializedElement;
template<typename ObjectTag>
struct ConcurrentRelation
{
    using object_tag = ObjectTag;
};
struct ConcurrentObjectTag;
struct ConcurrentPackedObjectTag;
}

namespace urd
{

template<>
struct data<ygg::tests::ConcurrentElement>
{
    urd::index<ygg::tests::ConcurrentElement> index;
    std::uint64_t value = 0;

    static constexpr auto cista_members() noexcept { return urd::Members<&data::index, &data::value> {}; }
    static constexpr auto identifying_members() noexcept { return urd::Members<&data::value> {}; }
};

template<>
struct data<ygg::tests::ConcurrentSerializedElement>
{
    urd::index<ygg::tests::ConcurrentSerializedElement> index;
    urd::flat_vector<urd::index<ygg::tests::ConcurrentElement>> values;

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
struct RelationRepositoryTraits<tests::ConcurrentPackedObjectTag>
{
    using width = urd::bit;
};
}

namespace ygg::tests
{
namespace
{
constexpr std::size_t kThreads = 8;

template<typename Function>
void run_threads(std::size_t count, Function function)
{
    auto start = std::barrier<>(static_cast<std::ptrdiff_t>(count));
    auto threads = urd::Store<urd::sequence<std::jthread>> {};
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
using Binding = Row<ConcurrentRelation<ObjectTag>>;

/// Four object columns: the type of make_binding's rows, as declared pools of raw keys take them.
constexpr auto object_columns = std::array { ygg::type_id<urd::index<formalism::Object<void>>>, ygg::type_id<urd::index<formalism::Object<void>>>,
                                             ygg::type_id<urd::index<formalism::Object<void>>>, ygg::type_id<urd::index<formalism::Object<void>>> };

template<typename ObjectTag>
urd::data<Binding<ObjectTag>> make_binding(urd::index<ConcurrentRelation<ObjectTag>> relation, uint_t key)
{
    using Object = formalism::Object<ObjectTag>;

    auto objects = urd::flat_vector<ygg::Value> {};
    for (uint_t i = 0; i < 4; ++i)
        objects.push_back((key + i * 997U) & 4095U);
    return urd::data<Binding<ObjectTag>>(relation, std::move(objects));
}

template<typename ObjectTag>
using ConcurrentRepository = formalism::Repository<formalism::ConcurrentSymbolRepository<ConcurrentElement, ConcurrentSerializedElement>,
                                                   formalism::ConcurrentRelationRepository<ObjectTag, ConcurrentRelation<ObjectTag>>>;

using SequentialRepository = formalism::Repository<formalism::SymbolRepository<ConcurrentElement, ConcurrentSerializedElement>,
                                                   formalism::RelationRepository<ConcurrentObjectTag, ConcurrentRelation<ConcurrentObjectTag>>>;
using MixedRepository = formalism::Repository<formalism::ConcurrentSymbolRepository<ConcurrentElement, ConcurrentSerializedElement>,
                                              formalism::RelationRepository<ConcurrentObjectTag, ConcurrentRelation<ConcurrentObjectTag>>>;

/// Whether both halves of a repository layer are thread-safe.
template<typename Repository>
constexpr bool thread_safe = std::remove_cvref_t<decltype(std::declval<const Repository&>().symbols())>::thread_safe
                             && std::remove_cvref_t<decltype(std::declval<const Repository&>().relations())>::thread_safe;

static_assert(thread_safe<ConcurrentRepository<ConcurrentObjectTag>>);
static_assert(!thread_safe<SequentialRepository>);
static_assert(!thread_safe<MixedRepository>);
static_assert(alignof(ConcurrentRepository<ConcurrentObjectTag>) < 256);

class PublishedRepository : public formalism::SymbolRepositoryBase<PublishedRepository, boost::mp11::mp_list<ConcurrentElement, ConcurrentSerializedElement>>
{
public:
    /// The repository's number, as urd's views ask of a context.
    [[nodiscard]] std::size_t layer() const noexcept { return 0; }
    friend const PublishedRepository& get_repository(const PublishedRepository& repository URD_LIFETIMEBOUND) noexcept { return repository; }
};

static_assert(SymbolRepositoryFor<PublishedRepository, ConcurrentElement>);
static_assert(!formalism::SupportsSymbol<PublishedRepository, ConcurrentRelation<ConcurrentObjectTag>>);
static_assert(!std::is_move_constructible_v<PublishedRepository>);
static_assert(!std::is_copy_constructible_v<PublishedRepository>);
static_assert(std::same_as<decltype(get_repository(std::declval<const PublishedRepository&>())), const PublishedRepository&>);

TEST(YggdrasilTests, PublicSymbolRepositoryBasePreservesPublishedContextAndScratchIndices)
{
    auto repository = PublishedRepository {};
    auto data = urd::data<ConcurrentElement> { urd::index<ConcurrentElement>(42), 7 };
    const auto [view, inserted] = repository.insert(data);
    EXPECT_TRUE(inserted);
    EXPECT_EQ(&view.context(), &repository);
    EXPECT_EQ(&view.data(), &repository[view.index()]);
    EXPECT_EQ(view.data().value, 7);
    EXPECT_EQ(data.index, view.index());

    data.index = urd::index<ConcurrentElement>(84);
    const auto [duplicate, duplicate_inserted] = repository.insert(data);
    EXPECT_FALSE(duplicate_inserted);
    EXPECT_EQ(duplicate.index(), view.index());
    EXPECT_EQ(&duplicate.context(), &repository);
    EXPECT_EQ(data.index, view.index());
    const auto found = repository.find(data);
    ASSERT_TRUE(found);
    EXPECT_EQ(found.value().index(), view.index());
    EXPECT_EQ(&found.value().context(), &repository);

    auto serialized = urd::data<ConcurrentSerializedElement> {};
    serialized.values.push_back(view.index());
    const auto [serialized_view, serialized_inserted] = repository.insert(serialized);
    EXPECT_TRUE(serialized_inserted);
    EXPECT_EQ(&serialized_view.context(), &repository);
    EXPECT_EQ(serialized.index, serialized_view.index());
    serialized.index = urd::index<ConcurrentSerializedElement>(84);
    const auto [serialized_duplicate, serialized_duplicate_inserted] = repository.insert(serialized);
    EXPECT_FALSE(serialized_duplicate_inserted);
    EXPECT_EQ(serialized.index, serialized_view.index());
    EXPECT_EQ(&serialized_duplicate.data(), &serialized_view.data());

    repository.clear();
    EXPECT_EQ(repository.size<ConcurrentElement>(), 0);
    EXPECT_EQ(repository.size<ConcurrentSerializedElement>(), 0);
}

template<typename ObjectTag>
void expect_duplicate_same_relation_insertions()
{
    auto repository = ConcurrentRepository<ObjectTag>(4096, nullptr);
    const auto relation = urd::index<ConcurrentRelation<ObjectTag>>(0);
    repository.declare(relation, object_columns);  // a raw key: its pool declared here
    constexpr uint_t rows = 512;
    constexpr auto invalid_row = std::numeric_limits<uint_t>::max();
    auto canonical_rows = urd::Store<urd::sequence<std::atomic<uint_t>>>(rows);
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
                        const auto [view, was_created] = repository.insert(data);
                        created.fetch_add(was_created, std::memory_order_relaxed);

                        const auto returned_row = view.data().row.value;
                        auto expected = invalid_row;
                        if (!canonical_rows[key].compare_exchange_strong(expected, returned_row, std::memory_order_relaxed) && expected != returned_row)
                            errors.fetch_add(1, std::memory_order_relaxed);
                    }
                });

    EXPECT_EQ(errors.load(), 0);
    EXPECT_EQ(created.load(), rows);
    EXPECT_EQ(repository.relations().size(relation), rows);
    for (uint_t key = 0; key < rows; ++key)
    {
        const auto data = make_binding<ObjectTag>(relation, key);
        const auto found = repository.find(data);
        ASSERT_TRUE(found.has_value());
        EXPECT_EQ(found.value().data().row.value, canonical_rows[key].load(std::memory_order_relaxed));
        EXPECT_TRUE(std::ranges::equal(found.value().get_key().second, data.values));
    }
}

template<typename ObjectTag>
void expect_unique_same_relation_insertions()
{
    auto repository = ConcurrentRepository<ObjectTag>(4096, nullptr);
    const auto relation = urd::index<ConcurrentRelation<ObjectTag>>(0);
    repository.declare(relation, object_columns);  // a raw key: its pool declared here
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
                        const auto [view, was_created] = repository.insert(data);
                        created.fetch_add(was_created, std::memory_order_relaxed);
                        if (!std::ranges::equal(view.get_key().second, data.values))
                            errors.fetch_add(1, std::memory_order_relaxed);
                    }
                });

    EXPECT_EQ(errors.load(), 0);
    EXPECT_EQ(created.load(), expected_rows);
    EXPECT_EQ(repository.relations().size(relation), expected_rows);

    auto seen = urd::Store<urd::set<uint_t>, urd::dense> {};
    for (uint_t key = 0; key < expected_rows; ++key)
    {
        const auto data = make_binding<ObjectTag>(relation, key);
        const auto found = repository.find(data);
        ASSERT_TRUE(found.has_value());
        const auto row = found.value().data().row.value;
        ASSERT_LT(row, expected_rows);
        EXPECT_TRUE(seen.insert(row).second);
        EXPECT_TRUE(std::ranges::equal(found.value().get_key().second, data.values));
    }
}

urd::data<Binding<ConcurrentPackedObjectTag>> make_packed_binding(urd::index<ConcurrentRelation<ConcurrentPackedObjectTag>> relation, uint_t key, bool valid)
{
    using Object = formalism::Object<ConcurrentPackedObjectTag>;
    auto objects = urd::flat_vector<ygg::Value> {};
    for (uint_t i = 0; i < 4; ++i)
        objects.push_back((key >> (i * 3U)) & 7U);
    if (!valid)
        objects.back() = 8;
    return urd::data<Binding<ConcurrentPackedObjectTag>>(relation, std::move(objects));
}

TEST(YggdrasilTests, FormalismConcurrentRepositoryCanonicalizesTrivialAndSerializedSymbols)
{
    auto repository = ConcurrentRepository<ConcurrentObjectTag>(4096, nullptr);
    auto errors = std::atomic_size_t { 0 };
    auto trivial_created = std::atomic_size_t { 0 };
    constexpr uint_t num_trivial = 1024;

    run_threads(kThreads,
                [&](std::size_t thread)
                {
                    for (uint_t offset = 0; offset < num_trivial; ++offset)
                    {
                        const auto key = (offset + static_cast<uint_t>(thread)) % num_trivial;
                        auto data = urd::data<ConcurrentElement> {};
                        data.value = key;
                        const auto [view, created] = repository.insert(data);
                        trivial_created.fetch_add(created, std::memory_order_relaxed);
                        if (view.data().value != key || view.data().index != view.index() || data.index != view.index())
                            errors.fetch_add(1, std::memory_order_relaxed);
                    }
                });

    EXPECT_EQ(errors.load(), 0);
    EXPECT_EQ(trivial_created.load(), num_trivial);
    EXPECT_EQ(repository.symbols().size<ConcurrentElement>(), num_trivial);

    auto seen = urd::Store<urd::set<uint_t>, urd::dense> {};
    for (uint_t key = 0; key < num_trivial; ++key)
    {
        auto data = urd::data<ConcurrentElement> {};
        data.value = key;
        const auto found = repository.find(data);
        ASSERT_TRUE(found.has_value());
        const auto index = found.value().index().value;
        ASSERT_LT(index, num_trivial);
        EXPECT_TRUE(seen.insert(index).second);
    }

    auto serialized_created = std::atomic_size_t { 0 };
    constexpr uint_t num_serialized = 256;
    run_threads(kThreads,
                [&](std::size_t thread)
                {
                    for (uint_t offset = 0; offset < num_serialized; ++offset)
                    {
                        const auto key = (offset + static_cast<uint_t>(thread)) % num_serialized;
                        auto data = urd::data<ConcurrentSerializedElement> {};
                        data.values.push_back(urd::index<ConcurrentElement>(key));
                        const auto [view, created] = repository.insert(data);
                        serialized_created.fetch_add(created, std::memory_order_relaxed);
                        if (view.data().values.size() != 1 || view.data().values.front().value != key || view.data().index != view.index()
                            || data.index != view.index())
                            errors.fetch_add(1, std::memory_order_relaxed);
                    }
                });

    EXPECT_EQ(errors.load(), 0);
    EXPECT_EQ(serialized_created.load(), num_serialized);
    EXPECT_EQ(repository.symbols().size<ConcurrentSerializedElement>(), num_serialized);
}

TEST(YggdrasilTests, FormalismConcurrentRepositoryUsesIndependentRelationGroupLanes)
{
    using ObjectTag = ConcurrentObjectTag;
    auto lane_repository = ConcurrentRepository<ObjectTag>(4096, nullptr);
    for (uint_t lane = 0; lane < kThreads; ++lane)
        lane_repository.declare(urd::index<ConcurrentRelation<ObjectTag>>(lane), object_columns);
    constexpr uint_t lane_rows = 256;
    run_threads(kThreads,
                [&](std::size_t thread)
                {
                    const auto lane = urd::index<ConcurrentRelation<ObjectTag>>(static_cast<uint_t>(thread));
                    for (uint_t key = 0; key < lane_rows; ++key)
                        lane_repository.insert(make_binding<ObjectTag>(lane, key));
                });

    for (uint_t lane = 0; lane < kThreads; ++lane)
        EXPECT_EQ(lane_repository.relations().size(urd::index<ConcurrentRelation<ObjectTag>>(lane)), lane_rows);
}

TEST(YggdrasilTests, FormalismRepositoryRejectsReservedRelationIndex)
{
    using ObjectTag = ConcurrentObjectTag;
    const auto relation = urd::index<ConcurrentRelation<ObjectTag>>();
    auto sequential = SequentialRepository(4096, nullptr);
    auto concurrent = ConcurrentRepository<ObjectTag>(4096, nullptr);

    EXPECT_THROW(sequential.declare(relation, object_columns), std::invalid_argument);  // the reserved index is no pool
    EXPECT_THROW(concurrent.declare(relation, object_columns), std::invalid_argument);
    EXPECT_THROW(sequential.insert(make_binding<ObjectTag>(relation, 7)), std::out_of_range);  // so never declared
    EXPECT_THROW(concurrent.insert(make_binding<ObjectTag>(relation, 7)), std::out_of_range);
}

TEST(YggdrasilTests, FormalismConcurrentPackedRepositoryRejectsWrongArityWithoutMutation)
{
    using ObjectTag = ConcurrentPackedObjectTag;
    using Object = formalism::Object<ObjectTag>;
    auto repository = ConcurrentRepository<ObjectTag>(4096, nullptr);
    const auto relation = urd::index<ConcurrentRelation<ObjectTag>>(0);
    repository.declare(relation, object_columns);  // a raw key: its pool declared here
    const auto [view, created] = repository.insert(make_binding<ObjectTag>(relation, 7));
    ASSERT_TRUE(created);

    auto objects = urd::flat_vector<ygg::Value> {};
    objects.push_back(0);
    objects.push_back(1);
    objects.push_back(2);
    const auto wrong = urd::data<Binding<ObjectTag>>(relation, std::move(objects));
    EXPECT_FALSE(repository.find(wrong));
    EXPECT_THROW(repository.insert(wrong), std::invalid_argument);
    EXPECT_EQ(repository.relations().size(relation), 1);
    EXPECT_EQ(view.get_key().second.size(), 4);
}

TEST(YggdrasilTests, FormalismConcurrentRepositoryCanonicalizesRows)
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
    auto repository = ConcurrentRepository<ObjectTag>(4096, nullptr);
    const auto relation = urd::index<ConcurrentRelation<ObjectTag>>(0);
    repository.declare(relation, object_columns);  // a raw key: its pool declared here
    const auto [first, created] = repository.insert(make_binding<ObjectTag>(relation, 0));
    ASSERT_TRUE(created);
    const auto first_data = repository[first.data()].as<urd::index<formalism::Object<ObjectTag>>>();

    auto stop = std::atomic_bool { false };
    auto errors = std::atomic_size_t { 0 };
    auto readers = urd::Store<urd::sequence<std::jthread>> {};
    for (std::size_t i = 0; i < 4; ++i)
    {
        readers.emplace_back(
            [&]
            {
                while (!stop.load(std::memory_order_acquire))
                {
                    if (first_data[0].value != 0 || first_data[1].value != 997 || first_data[2].value != 1994
                        || first_data[3].value != 2991)
                        errors.fetch_add(1, std::memory_order_relaxed);
                }
            });
    }

    for (uint_t key = 1; key < 3000; ++key)
        repository.insert(make_binding<ObjectTag>(relation, key));

    stop.store(true, std::memory_order_release);
    for (auto& reader : readers)
        reader.join();

    EXPECT_EQ(errors.load(), 0);
    EXPECT_EQ(repository.relations().size(relation), 3000);
}

TEST(YggdrasilTests, FormalismConcurrentRepositoryReadsFrozenParentWhileGrowingChild)
{
    using ObjectTag = ConcurrentObjectTag;
    auto parent = ConcurrentRepository<ObjectTag>(4096, nullptr);
    auto parent_data = urd::data<ConcurrentElement> {};
    parent_data.value = 7;
    const auto [parent_view, parent_created] = parent.insert(parent_data);
    ASSERT_TRUE(parent_created);

    auto parent_serialized_data = urd::data<ConcurrentSerializedElement> {};
    parent_serialized_data.values.push_back(parent_view.index());
    const auto [parent_serialized_view, parent_serialized_created] = parent.insert(parent_serialized_data);
    ASSERT_TRUE(parent_serialized_created);

    const auto relation = urd::index<ConcurrentRelation<ObjectTag>>(0);
    parent.declare(relation, object_columns);  // a raw key: its pool declared here
    const auto [parent_binding_view, parent_binding_created] = parent.insert(make_binding<ObjectTag>(relation, 7));
    ASSERT_TRUE(parent_binding_created);

    auto child = ConcurrentRepository<ObjectTag>(4096, &parent);
    auto errors = std::atomic_size_t { 0 };
    constexpr uint_t rows_per_thread = 256;

    run_threads(kThreads,
                [&](std::size_t thread)
                {
                    auto inherited = urd::data<ConcurrentElement> {};
                    inherited.value = 7;
                    const auto [inherited_view, inherited_created] = child.insert(inherited);
                    if (inherited_created || &inherited_view.context() != &parent || inherited.index != inherited_view.index())
                        errors.fetch_add(1, std::memory_order_relaxed);

                    auto inherited_serialized = urd::data<ConcurrentSerializedElement> {};
                    inherited_serialized.values.push_back(parent_view.index());
                    const auto [inherited_serialized_view, inherited_serialized_created] = child.insert(inherited_serialized);
                    if (inherited_serialized_created || &inherited_serialized_view.context() != &parent
                        || inherited_serialized_view.data().values.front() != parent_view.index()
                        || inherited_serialized.index != inherited_serialized_view.index())
                        errors.fetch_add(1, std::memory_order_relaxed);

                    const auto [inherited_binding_view, inherited_binding_created] = child.insert(make_binding<ObjectTag>(relation, 7));
                    if (inherited_binding_created || &inherited_binding_view.context() != &parent
                        || !std::ranges::equal(inherited_binding_view.get_key().second, parent_binding_view.get_key().second))
                        errors.fetch_add(1, std::memory_order_relaxed);

                    auto missing = urd::data<ConcurrentElement> {};
                    missing.value = std::numeric_limits<uint_t>::max();
                    if (child.find(missing).has_value())
                        errors.fetch_add(1, std::memory_order_relaxed);

                    for (uint_t row = 0; row < rows_per_thread; ++row)
                    {
                        auto local = urd::data<ConcurrentElement> {};
                        local.value = 1000 + static_cast<uint_t>(thread) * rows_per_thread + row;
                        const auto [local_view, created] = child.insert(local);
                        if (!created || &local_view.context() != &child || local_view.data().value != local.value
                            || local_view.data().index != local_view.index() || local.index != local_view.index())
                            errors.fetch_add(1, std::memory_order_relaxed);
                    }
                });

    EXPECT_EQ(errors.load(), 0);
    EXPECT_EQ(parent.symbols().size<ConcurrentElement>(), 1);
    EXPECT_EQ(parent.symbols().size<ConcurrentSerializedElement>(), 1);
    EXPECT_EQ(parent.relations().size(relation), 1);
    EXPECT_EQ(child.symbols().size<ConcurrentElement>(), 1 + kThreads * rows_per_thread);
    EXPECT_EQ(parent_view.data().value, 7);
    EXPECT_EQ(parent_serialized_view.data().values.front(), parent_view.index());
    EXPECT_TRUE(std::ranges::equal(parent_binding_view.get_key().second, make_binding<ObjectTag>(relation, 7).values));
}

TEST(YggdrasilTests, FormalismConcurrentPackedInsertionFailureDoesNotPublishAndClearReusesStorage)
{
    using ObjectTag = ConcurrentPackedObjectTag;
    using Object = formalism::Object<ObjectTag>;
    auto repository = ConcurrentRepository<ObjectTag>(8, nullptr);
    const auto relation = urd::index<ConcurrentRelation<ObjectTag>>(0);
    repository.declare(relation, object_columns);  // a raw key: its pool declared here

    auto invalid_objects = urd::flat_vector<ygg::Value> {};
    invalid_objects.push_back(0);
    invalid_objects.push_back(1);
    invalid_objects.push_back(2);
    invalid_objects.push_back(8);
    auto invalid = urd::data<Binding<ObjectTag>>(relation, std::move(invalid_objects));
    EXPECT_THROW(repository.insert(invalid), std::out_of_range);
    EXPECT_EQ(repository.relations().size(relation), 0);
    repository.relations().clear();
    repository.relations().clear();

    auto valid_objects = urd::flat_vector<ygg::Value> {};
    for (uint_t value = 0; value < 4; ++value)
        valid_objects.push_back(value);
    const auto [view, created] = repository.insert(urd::data<Binding<ObjectTag>>(relation, std::move(valid_objects)));
    EXPECT_TRUE(created);
    EXPECT_EQ(view.data().row, urd::index<Row<ConcurrentRelation<ObjectTag>>>(0));

    repository.relations().clear();
    EXPECT_EQ(repository.relations().size(relation), 0);
    auto reused_objects = urd::flat_vector<ygg::Value> {};
    for (uint_t value = 4; value < 8; ++value)
        reused_objects.push_back(value);
    const auto [reused, reused_created] = repository.insert(urd::data<Binding<ObjectTag>>(relation, std::move(reused_objects)));
    EXPECT_TRUE(reused_created);
    EXPECT_EQ(reused.data().row, urd::index<Row<ConcurrentRelation<ObjectTag>>>(0));
}

TEST(YggdrasilTests, FormalismConcurrentPackedInsertionFailuresDoNotInterfereWithValidWriters)
{
    auto repository = ConcurrentRepository<ConcurrentPackedObjectTag>(8, nullptr);
    const auto relation = urd::index<ConcurrentRelation<ConcurrentPackedObjectTag>>(0);
    repository.declare(relation, object_columns);  // a raw key: its pool declared here
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
                            const auto [view, was_created] = repository.insert(make_packed_binding(relation, key, thread % 2 == 0));
                            if (thread % 2 == 0)
                            {
                                created.fetch_add(was_created, std::memory_order_relaxed);
                                if (view.get_key().second.size() != 4)
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
    EXPECT_EQ(repository.relations().size(relation), num_writers * rows_per_thread);

    for (uint_t key = 0; key < num_writers * rows_per_thread; ++key)
    {
        const auto data = make_packed_binding(relation, key, true);
        const auto found = repository.find(data);
        ASSERT_TRUE(found.has_value());
        EXPECT_TRUE(std::ranges::equal(found.value().get_key().second, data.values));
    }
}
}
}  // namespace ygg::tests
