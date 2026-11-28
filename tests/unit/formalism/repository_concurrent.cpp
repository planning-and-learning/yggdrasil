/*
 * Copyright (C) 2026 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "yggdrasil/core/config.hpp"
#include "yggdrasil/formalism/repository_impl.hpp"
#include "yggdrasil/formalism/row_data.hpp"
#include "yggdrasil/formalism/row_view.hpp"

#include "gtest/gtest.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <boost/mp11.hpp>
#include <concepts>
#include <cstdint>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <type_traits>
#include <urd/config.hpp>
#include <urd/elements/flat.hpp>
#include <urd/elements/view.hpp>
#include <urd/repr/sequences/sequence.hpp>
#include <urd/repr/values/dense_set.hpp>
#include <urd/store.hpp>
#include <utility>
#include <yggdrasil/formalism/symbol_context_impl.hpp>

namespace ygg::tests
{
struct ConcurrentElement;
struct ConcurrentSerializedElement;

namespace
{
template<typename Width>
struct ConcurrentRelation
{
};

/// Four object columns: the column types of every ConcurrentRelation's pool, make_binding's rows.
constexpr auto object_columns = std::array { ygg::type_id<urd::index<formalism::Object>>,
                                             ygg::type_id<urd::index<formalism::Object>>,
                                             ygg::type_id<urd::index<formalism::Object>>,
                                             ygg::type_id<urd::index<formalism::Object>> };
}
}

namespace urd
{

template<>
struct data<ygg::tests::ConcurrentElement>
{
    urd::key<ygg::tests::ConcurrentElement> index;
    std::uint64_t value = 0;

    data() = default;

    explicit data(std::uint64_t value_) : index(), value(value_) {}

    static constexpr bool copyable() noexcept
    {
        static_assert(std::is_trivially_copyable_v<data>, "a copyable record is trivially copyable");
        return true;
    }

    static constexpr auto fields() noexcept { return urd::Fields<urd::field<"value", &data::value>> {}; }

    static constexpr auto cista_members() noexcept { return urd::cista_members_of<data>(); }

    [[nodiscard]] urd::key<ygg::tests::ConcurrentElement> key() const noexcept { return urd::key<ygg::tests::ConcurrentElement>(index); }
};

template<typename V>
struct view_mixin<ygg::tests::ConcurrentElement, V>
{
private:
    [[nodiscard]] const V& self() const noexcept { return static_cast<const V&>(*this); }

public:
    [[nodiscard]] decltype(auto) value() const { return self().template member<&urd::record_of<V>::value>(); }
};

/// A key of row pools: a relation, told apart by its lane; its pool has four object columns.
template<typename Width>
struct data<ygg::tests::ConcurrentRelation<Width>>
{
    urd::key<ygg::tests::ConcurrentRelation<Width>> index;
    ygg::uint_t lane = 0;

    data() = default;

    explicit data(ygg::uint_t lane_) : index(), lane(lane_) {}

    static constexpr bool copyable() noexcept
    {
        static_assert(std::is_trivially_copyable_v<data>, "a copyable record is trivially copyable");
        return true;
    }

    static constexpr auto fields() noexcept { return urd::Fields<urd::field<"lane", &data::lane>> {}; }

    static constexpr auto cista_members() noexcept { return urd::cista_members_of<data>(); }

    [[nodiscard]] urd::key<ygg::tests::ConcurrentRelation<Width>> key() const noexcept { return urd::key<ygg::tests::ConcurrentRelation<Width>>(index); }
};

template<typename Width, typename V>
struct view_mixin<ygg::tests::ConcurrentRelation<Width>, V>
{
private:
    [[nodiscard]] const V& self() const noexcept { return static_cast<const V&>(*this); }

public:
    [[nodiscard]] decltype(auto) lane() const { return self().template member<&urd::record_of<V>::lane>(); }

    /// As a key of row pools, its rows' column types: those its record stores.
    [[nodiscard]] ygg::UniformColumnTypes column_types() const
    { return ygg::uniform_column_types<urd::index<ygg::formalism::Object>>(ygg::tests::object_columns.size()); }

    static constexpr auto functions() noexcept { return urd::Functions<urd::function<"column_types", &view_mixin::column_types>> {}; }
};

template<>
struct data<ygg::tests::ConcurrentSerializedElement>
{
    urd::key<ygg::tests::ConcurrentSerializedElement> index;
    urd::flat_vector<urd::index<ygg::tests::ConcurrentElement>> values;

    data() = default;

    explicit data(urd::flat_vector<urd::index<ygg::tests::ConcurrentElement>> values_) : index(), values(std::move(values_)) {}

    data(const data& other) = delete;
    data& operator=(const data& other) = delete;
    data(data&& other) = default;
    data& operator=(data&& other) = default;
    ~data() = default;

    static constexpr auto fields() noexcept { return urd::Fields<urd::field<"values", &data::values>> {}; }

    static constexpr auto cista_members() noexcept { return urd::cista_members_of<data>(); }

    [[nodiscard]] urd::key<ygg::tests::ConcurrentSerializedElement> key() const noexcept { return urd::key<ygg::tests::ConcurrentSerializedElement>(index); }
};

template<typename V>
struct view_mixin<ygg::tests::ConcurrentSerializedElement, V>
{
private:
    [[nodiscard]] const V& self() const noexcept { return static_cast<const V&>(*this); }

public:
    [[nodiscard]] decltype(auto) values() const { return self().template member<&urd::record_of<V>::values>(); }
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

template<typename Width>
using Binding = Row<ConcurrentRelation<Width>>;

/// A row to store: its relation (its key's known part) and its values (its content).
template<typename F>
struct Bound
{
    urd::index<F> relation;
    urd::flat_vector<ygg::Value> values;
};

/// The row of `bound` stored where no layer holds it (the repository's one insert: under its relation): its view, and
/// whether it was stored now.
template<typename F, typename Repository>
std::pair<RowView<F, Repository>, bool> insert_bound(Repository& repository, const Bound<F>& bound)
{
    auto content = urd::data<Row<F>>(bound.relation, urd::flat_vector<ygg::Value>(bound.values.begin(), bound.values.end()));
    return repository.insert(content);
}

/// The view of the row of `bound`, where some layer holds it (the repository's one find).
template<typename F, typename Repository>
std::optional<RowView<F, Repository>> find_bound(const Repository& repository, const Bound<F>& bound)
{
    auto content = urd::data<Row<F>>(bound.relation, urd::flat_vector<ygg::Value>(bound.values.begin(), bound.values.end()));
    return repository.find(content);
}

template<typename Width>
Bound<ConcurrentRelation<Width>> make_binding(urd::index<ConcurrentRelation<Width>> relation, uint_t key)
{
    auto objects = urd::flat_vector<ygg::Value> {};
    for (uint_t i = 0; i < 4; ++i)
        objects.push_back((key + i * 997U) & 4095U);
    return { relation, std::move(objects) };
}

/// The relation of `lane`: its record, interned in `repository`, which declares its pool.
template<typename Width, typename Repository>
urd::index<ConcurrentRelation<Width>> declared_relation(Repository& repository, uint_t lane)
{
    auto key = urd::data<ConcurrentRelation<Width>>(lane);
    return repository.insert(key).first.index();
}

// The repositories' symbols and relations, named by classes of their own (brokkr's template rule T20).
template<typename Width>
struct ConcurrentTestSymbols : formalism::ConcurrentSymbols<ConcurrentElement, ConcurrentSerializedElement, ConcurrentRelation<Width>>
{
};

template<typename Width>
struct ConcurrentTestRelations : formalism::ConcurrentRelations<ConcurrentRelation<Width>>
{
};

struct SequentialTestSymbols : formalism::Symbols<ConcurrentElement, ConcurrentSerializedElement, ConcurrentRelation<urd::word>>
{
};

struct SequentialTestRelations : formalism::Relations<ConcurrentRelation<urd::word>>
{
};

template<typename Width>
using ConcurrentRepository = formalism::Repository<ConcurrentTestSymbols<Width>, ConcurrentTestRelations<Width>>;

using SequentialRepository = formalism::Repository<SequentialTestSymbols, SequentialTestRelations>;
using MixedRepository = formalism::Repository<ConcurrentTestSymbols<urd::word>, SequentialTestRelations>;

/// Whether both halves of a repository layer are thread-safe.
template<typename Repository>
constexpr bool thread_safe = Repository::thread_safe;

static_assert(thread_safe<ConcurrentRepository<urd::word>>);
static_assert(!thread_safe<SequentialRepository>);
static_assert(!thread_safe<MixedRepository>);
static_assert(alignof(ConcurrentRepository<urd::word>) < 256);

/// A repository of its own symbols: a symbol context over them, the context of their views.
class PublishedRepository : public formalism::SymbolContext<PublishedRepository, boost::mp11::mp_list<ConcurrentElement, ConcurrentSerializedElement>>
{
public:
    /// The repository's number, as urd's views ask of a context.
    [[nodiscard]] std::size_t layer() const noexcept { return 0; }
};

}  // namespace
}  // namespace ygg::tests

namespace ygg::tests
{
namespace
{

static_assert(!formalism::SupportsSymbol<PublishedRepository, ConcurrentRelation<urd::word>>);
static_assert(!std::is_move_constructible_v<PublishedRepository>);
static_assert(!std::is_copy_constructible_v<PublishedRepository>);

TEST(YggdrasilTests, PublicSymbolContextPreservesPublishedContextAndScratchIndices)
{
    auto repository = PublishedRepository {};
    auto data = urd::data<ConcurrentElement>(7);
    data.index = urd::index<ConcurrentElement>(42);
    const auto [view, inserted] = repository.insert(data);
    EXPECT_TRUE(inserted);
    EXPECT_EQ(&view.context(), &repository);
    EXPECT_EQ(&view.value(), &repository[view.index()]->value);
    EXPECT_EQ(view.value(), 7);
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
    EXPECT_EQ(serialized_duplicate, serialized_view);

    repository.clear();
    EXPECT_EQ(repository.size<ConcurrentElement>(), 0);
    EXPECT_EQ(repository.size<ConcurrentSerializedElement>(), 0);
}

TEST(YggdrasilTests, FormalismConcurrentRepositoryCanonicalizesTrivialAndSerializedSymbols)
{
    auto repository = ConcurrentRepository<urd::word>(nullptr);
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
                        if (view.value() != key || view.context()[view.index()]->index != view.index() || data.index != view.index())
                            errors.fetch_add(1, std::memory_order_relaxed);
                    }
                });

    EXPECT_EQ(errors.load(), 0);
    EXPECT_EQ(trivial_created.load(), num_trivial);
    EXPECT_EQ(repository.size<ConcurrentElement>(), num_trivial);

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
                        if (view.context()[view.index()]->values.size() != 1 || view.context()[view.index()]->values.front().value != key
                            || view.context()[view.index()]->index != view.index() || data.index != view.index())
                            errors.fetch_add(1, std::memory_order_relaxed);
                    }
                });

    EXPECT_EQ(errors.load(), 0);
    EXPECT_EQ(serialized_created.load(), num_serialized);
    EXPECT_EQ(repository.size<ConcurrentSerializedElement>(), num_serialized);
}

TEST(YggdrasilTests, FormalismConcurrentRepositoryUsesIndependentRelationGroupLanes)
{
    using Width = urd::word;
    auto lane_repository = ConcurrentRepository<Width>(nullptr);
    auto lanes = std::array<urd::index<ConcurrentRelation<Width>>, kThreads> {};
    for (uint_t lane = 0; lane < kThreads; ++lane)
        lanes[lane] = declared_relation<Width>(lane_repository, lane);
    constexpr uint_t lane_rows = 256;
    run_threads(kThreads,
                [&](std::size_t thread)
                {
                    const auto lane = lanes[thread];
                    for (uint_t key = 0; key < lane_rows; ++key)
                        insert_bound(lane_repository, make_binding<Width>(lane, key));
                });

    for (uint_t lane = 0; lane < kThreads; ++lane)
        EXPECT_EQ(lane_repository.template size<Row<ConcurrentRelation<Width>>>(lanes[lane]), lane_rows);
}

TEST(YggdrasilTests, FormalismRepositoryRejectsReservedRelationIndex)
{
    using Width = urd::word;
    const auto relation = urd::index<ConcurrentRelation<Width>>();
    auto sequential = SequentialRepository(nullptr);
    auto concurrent = ConcurrentRepository<Width>(nullptr);

    EXPECT_THROW(insert_bound(sequential, make_binding<Width>(relation, 7)), std::out_of_range);  // the reserved index is no pool
    EXPECT_THROW(insert_bound(concurrent, make_binding<Width>(relation, 7)), std::out_of_range);
}

TEST(YggdrasilTests, FormalismConcurrentPackedRepositoryRejectsWrongArityWithoutMutation)
{
    using Width = urd::word;
    auto repository = ConcurrentRepository<Width>(nullptr);
    const auto relation = declared_relation<Width>(repository, 0);
    const auto [view, created] = insert_bound(repository, make_binding<Width>(relation, 7));
    ASSERT_TRUE(created);

    auto objects = urd::flat_vector<ygg::Value> {};
    objects.push_back(0);
    objects.push_back(1);
    objects.push_back(2);
    const auto wrong = Bound<ConcurrentRelation<Width>> { relation, std::move(objects) };
    EXPECT_FALSE(find_bound(repository, wrong).has_value());
    EXPECT_THROW(insert_bound(repository, wrong), std::invalid_argument);
    EXPECT_EQ(repository.template size<Row<ConcurrentRelation<Width>>>(relation), 1);
    EXPECT_EQ(view.values().size(), 4);
}

TEST(YggdrasilTests, FormalismConcurrentPackedViewsRemainStableWhileSharedBlocksGrow)
{
    using Width = urd::word;
    auto repository = ConcurrentRepository<Width>(nullptr);
    const auto relation = declared_relation<Width>(repository, 0);
    const auto [first, created] = insert_bound(repository, make_binding<Width>(relation, 0));
    ASSERT_TRUE(created);
    const auto first_data = first.template get<urd::index<formalism::Object>>();

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
                    if (first_data[0].value != 0 || first_data[1].value != 997 || first_data[2].value != 1994 || first_data[3].value != 2991)
                        errors.fetch_add(1, std::memory_order_relaxed);
                }
            });
    }

    for (uint_t key = 1; key < 3000; ++key)
        insert_bound(repository, make_binding<Width>(relation, key));

    stop.store(true, std::memory_order_release);
    for (auto& reader : readers)
        reader.join();

    EXPECT_EQ(errors.load(), 0);
    EXPECT_EQ(repository.template size<Row<ConcurrentRelation<Width>>>(relation), 3000);
}

TEST(YggdrasilTests, FormalismConcurrentRepositoryReadsFrozenParentWhileGrowingChild)
{
    using Width = urd::word;
    auto parent = ConcurrentRepository<Width>(nullptr);
    auto parent_data = urd::data<ConcurrentElement> {};
    parent_data.value = 7;
    const auto [parent_view, parent_created] = parent.insert(parent_data);
    ASSERT_TRUE(parent_created);

    auto parent_serialized_data = urd::data<ConcurrentSerializedElement> {};
    parent_serialized_data.values.push_back(parent_view.index());
    const auto [parent_serialized_view, parent_serialized_created] = parent.insert(parent_serialized_data);
    ASSERT_TRUE(parent_serialized_created);

    const auto relation = declared_relation<Width>(parent, 0);
    const auto [parent_binding_view, parent_binding_created] = insert_bound(parent, make_binding<Width>(relation, 7));
    ASSERT_TRUE(parent_binding_created);

    auto child = ConcurrentRepository<Width>(&parent);
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
                        || inherited_serialized_view.context()[inherited_serialized_view.index()]->values.front() != parent_view.index()
                        || inherited_serialized.index != inherited_serialized_view.index())
                        errors.fetch_add(1, std::memory_order_relaxed);

                    const auto [inherited_binding_view, inherited_binding_created] = insert_bound(child, make_binding<Width>(relation, 7));
                    if (inherited_binding_created || &inherited_binding_view.context() != &parent
                        || !std::ranges::equal(inherited_binding_view.values(), parent_binding_view.values()))
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
                        if (!created || &local_view.context() != &child || local_view.value() != local.value
                            || local_view.context()[local_view.index()]->index != local_view.index() || local.index != local_view.index())
                            errors.fetch_add(1, std::memory_order_relaxed);
                    }
                });

    EXPECT_EQ(errors.load(), 0);
    EXPECT_EQ(parent.size<ConcurrentElement>(), 1);
    EXPECT_EQ(parent.size<ConcurrentSerializedElement>(), 1);
    EXPECT_EQ(parent.template size<Row<ConcurrentRelation<Width>>>(relation), 1);
    EXPECT_EQ(child.size<ConcurrentElement>(), 1 + kThreads * rows_per_thread);
    EXPECT_EQ(parent_view.value(), 7);
    EXPECT_EQ(parent_serialized_view.context()[parent_serialized_view.index()]->values.front(), parent_view.index());
    EXPECT_TRUE(std::ranges::equal(parent_binding_view.values(), make_binding<Width>(relation, 7).values));
}

TEST(YggdrasilTests, FormalismConcurrentRepositoryWithOneWriterInsertsRowsAsAShared)
{
    using Width = urd::word;
    auto parent = ConcurrentRepository<Width>(nullptr);
    const auto relation = declared_relation<Width>(parent, 0);
    const auto [parent_binding_view, parent_binding_created] = insert_bound(parent, make_binding<Width>(relation, 7));
    ASSERT_TRUE(parent_binding_created);

    auto shared = ConcurrentRepository<Width>(&parent);
    auto exclusive = ConcurrentRepository<Width>(&parent);
    exclusive.set_one_writer(true);

    // A row of the parent is the parent's in both layers.
    const auto [inherited_view, inherited_created] = insert_bound(exclusive, make_binding<Width>(relation, 7));
    EXPECT_FALSE(inherited_created);
    EXPECT_EQ(&inherited_view.context(), &parent);
    EXPECT_EQ(inherited_view.index(), parent_binding_view.index());

    // New rows are numbered alike, stored once, and found again.
    constexpr uint_t rows = 512;
    for (uint_t key = 0; key < rows; ++key)
    {
        const auto [shared_view, shared_created] = insert_bound(shared, make_binding<Width>(relation, 100 + key));
        const auto [exclusive_view, exclusive_created] = insert_bound(exclusive, make_binding<Width>(relation, 100 + key));
        EXPECT_TRUE(shared_created);
        EXPECT_TRUE(exclusive_created);
        EXPECT_EQ(&exclusive_view.context(), &exclusive);
        EXPECT_EQ(exclusive_view.index(), shared_view.index());
        EXPECT_TRUE(std::ranges::equal(exclusive_view.values(), shared_view.values()));
    }
    for (uint_t key = 0; key < rows; ++key)
    {
        const auto [again_view, again_created] = insert_bound(exclusive, make_binding<Width>(relation, 100 + key));
        EXPECT_FALSE(again_created);
        EXPECT_EQ(&again_view.context(), &exclusive);
    }
    EXPECT_EQ(exclusive.template size<Row<ConcurrentRelation<Width>>>(relation), shared.template size<Row<ConcurrentRelation<Width>>>(relation));

    // Back to shared use, the layer finds what it stored alone.
    exclusive.set_one_writer(false);
    const auto [shared_again_view, shared_again_created] = insert_bound(exclusive, make_binding<Width>(relation, 100));
    EXPECT_FALSE(shared_again_created);
    EXPECT_EQ(&shared_again_view.context(), &exclusive);
}

}
}  // namespace ygg::tests
