/*
 * Copyright (C) 2025-2026 Dominik Drexler
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

#include "yggdrasil/semantics/canonicalization.hpp"

#include <algorithm>
#include <urd/repr/sequences/sequence.hpp>
#include <urd/store.hpp>

#include <urd/elements/flat.hpp>
#include <boost/mp11.hpp>
#include "yggdrasil/core/config.hpp"
#include "yggdrasil/formalism/binding_data.hpp"
#include "yggdrasil/formalism/binding_view.hpp"
#include "yggdrasil/formalism/builder.hpp"
#include "yggdrasil/formalism/interning.hpp"
#include "yggdrasil/formalism/membership.hpp"
#include "yggdrasil/formalism/relation_repository.hpp"
#include "yggdrasil/formalism/repository.hpp"
#include "yggdrasil/formalism/repository_factory.hpp"
#include "yggdrasil/formalism/symbol_repository.hpp"

#include "gtest/gtest.h"
#include <array>
#include <concepts>
#include <cstdint>
#include <forward_list>
#include <iterator>
#include <limits>
#include <list>
#include <memory>
#include <ranges>
#include <span>
#include <stdexcept>
#include <tuple>
#include <urd/elements/equal_to.hpp>
#include <urd/elements/hash.hpp>
#include <vector>
#include <urd/config.hpp>

namespace ygg::tests
{

/// Whether the context makes views of the handle.
template<typename H, typename C>
concept Viewable = requires(const C& context, H handle) { context.view(handle); };

struct RepositoryTypesElement;
struct RepositoryTypesSerializedElement;
struct RepositoryTypesRelation;
struct RepositoryTypesObjectTag;
struct RepositoryTypesPackedObjectTag;
struct RepositoryTypesBuilderOther;
struct RepositoryTypesCheckout;

namespace
{
struct RepositoryTypesContext
{
    using Binding = formalism::RelationBindingIndex<RepositoryTypesRelation, RepositoryTypesObjectTag>;

    [[nodiscard]] const RepositoryTypesContext& get_canonical_context(const RepositoryTypesElement&) const noexcept;
    [[nodiscard]] std::size_t layer() const noexcept { return 0; }
    urd::Store<urd::sequence<urd::index<formalism::Object<RepositoryTypesObjectTag>>>> operator[](Binding) const { return {}; }
    [[nodiscard]] formalism::RelationBindingView<Binding, RepositoryTypesContext> view(Binding index) const { return { index, this }; }
    [[nodiscard]] urd::index_view<RepositoryTypesRelation, RepositoryTypesContext> view(urd::index<RepositoryTypesRelation> index) const { return { index, this }; }
};
}  // namespace

}  // namespace ygg::tests

namespace urd
{

template<>
struct data<ygg::tests::RepositoryTypesElement>
{
    urd::index<ygg::tests::RepositoryTypesElement> index;
    int value = 0;

    static constexpr auto cista_members() noexcept { return urd::Members<&data::index, &data::value> {}; }
    static constexpr auto identifying_members() noexcept { return urd::Members<&data::value> {}; }
};

template<>
struct data<ygg::tests::RepositoryTypesSerializedElement>
{
    urd::index<ygg::tests::RepositoryTypesSerializedElement> index;
    urd::flat_vector<urd::index<ygg::tests::RepositoryTypesElement>> values;

    data() = default;
    data(const data&) = delete;
    data& operator=(const data&) = delete;
    data(data&&) = default;
    data& operator=(data&&) = default;
    ~data() = default;

    static constexpr auto cista_members() noexcept { return urd::Members<&data::index, &data::values> {}; }
    static constexpr auto identifying_members() noexcept { return urd::Members<&data::values> {}; }
};

template<>
struct data<ygg::tests::RepositoryTypesRelation>
{
    urd::index<ygg::tests::RepositoryTypesRelation> index;
    ygg::uint_t value = 0;
    static constexpr auto cista_members() noexcept { return urd::Members<&data::index, &data::value> {}; }
    static constexpr auto identifying_members() noexcept { return urd::Members<&data::value> {}; }
};

template<>
struct data<ygg::formalism::Object<ygg::tests::RepositoryTypesObjectTag>>
{
    urd::index<ygg::formalism::Object<ygg::tests::RepositoryTypesObjectTag>> index;
    ygg::uint_t value = 0;
    static constexpr auto cista_members() noexcept { return urd::Members<&data::index, &data::value> {}; }
    static constexpr auto identifying_members() noexcept { return urd::Members<&data::value> {}; }
};

template<>
struct data<ygg::tests::RepositoryTypesBuilderOther>
{
    int value = 0;

    static constexpr auto cista_members() noexcept { return urd::Members<&data::value> {}; }
    static constexpr auto identifying_members() noexcept { return urd::Members<&data::value> {}; }
};

template<>
struct data<ygg::tests::RepositoryTypesCheckout>
{
    int index = 0;
    urd::Store<urd::sequence<int>, urd::contiguous> values;

    static constexpr auto cista_members() noexcept { return urd::Members<&data::index, &data::values> {}; }
};

}  // namespace urd

namespace ygg::formalism
{

template<>
struct RelationRepositoryTraits<tests::RepositoryTypesPackedObjectTag>
{
    using width = urd::bit;
};

}  // namespace ygg::formalism

namespace ygg::tests
{

namespace
{
struct RepositoryTypesRepository
{
    using object_tag = RepositoryTypesObjectTag;
    using RelationTypes = boost::mp11::mp_list<RepositoryTypesRelation>;
    using Binding = ygg::formalism::RelationBinding<RepositoryTypesRelation, RepositoryTypesObjectTag>;
    using Object = ygg::formalism::Object<RepositoryTypesObjectTag>;

    urd::Store<urd::sequence<urd::index<Object>>> operator[](ygg::formalism::RelationBindingIndex<RepositoryTypesRelation, RepositoryTypesObjectTag>) const { return {}; }
    [[nodiscard]] std::size_t layer() const noexcept { return 0; }
};

inline const RepositoryTypesRepository& get_repository(const RepositoryTypesContext&) noexcept
{
    static const auto repository = RepositoryTypesRepository();
    return repository;
}
}  // namespace

TEST(YggdrasilTests, CommonRepositoryTypesUmbrellaHeaderCompiles)
{

    SUCCEED();
}

TEST(YggdrasilTests, CommonBuilderStorageReusesReleasedBuildersByType)
{
    auto storage = ygg::formalism::BuilderStorage<RepositoryTypesElement, RepositoryTypesBuilderOther>();

    auto element_builder = storage.get_builder<RepositoryTypesElement>();
    element_builder->value = 7;
    const auto* first_element_address = element_builder.get();
    element_builder = {};

    auto reused_element_builder = storage.get_builder<RepositoryTypesElement>();
    EXPECT_EQ(reused_element_builder.get(), first_element_address);
    EXPECT_EQ(reused_element_builder->value, 7);

    auto other_builder = storage.get_builder<RepositoryTypesBuilderOther>();
    other_builder->value = 3;
    EXPECT_NE(static_cast<const void*>(other_builder.get()), static_cast<const void*>(reused_element_builder.get()));
}

template<typename T, typename Storage>
concept CanCheckout = requires(Storage& storage) {
    storage.template checkout<T>();
    ygg::formalism::checkout<T>(storage);
};

using ContractBuilderStorage = ygg::formalism::BuilderStorage<RepositoryTypesCheckout>;
static_assert(std::is_default_constructible_v<ContractBuilderStorage>);
static_assert(!std::is_copy_constructible_v<ContractBuilderStorage>);
static_assert(!std::is_copy_assignable_v<ContractBuilderStorage>);
static_assert(!std::is_move_constructible_v<ContractBuilderStorage>);
static_assert(!std::is_move_assignable_v<ContractBuilderStorage>);

static_assert(CanCheckout<RepositoryTypesCheckout, ygg::formalism::BuilderStorage<RepositoryTypesCheckout>>);
static_assert(!CanCheckout<RepositoryTypesCheckout, ygg::formalism::BuilderStorage<RepositoryTypesElement>>);

TEST(YggdrasilTests, CommonBuilderCheckoutClearsOnceAndRetainsCapacity)
{
    auto storage = ygg::formalism::BuilderStorage<RepositoryTypesCheckout>();
    auto data = storage.get_builder<RepositoryTypesCheckout>();
    data->index = 7;
    data->values.assign(32, 42);
    const auto* address = data.get();
    const auto* buffer = data->values.data();
    const auto capacity = data->values.capacity();
    data = {};

    auto raw = storage.get_builder<RepositoryTypesCheckout>();
    EXPECT_EQ(raw->index, 7);
    EXPECT_EQ(raw->values.size(), 32);
    raw = {};

    auto cleared = ygg::formalism::checkout<RepositoryTypesCheckout>(storage);
    EXPECT_EQ(cleared.get(), address);
    EXPECT_EQ(cleared->index, 0);
    EXPECT_TRUE(cleared->values.empty());
    EXPECT_EQ(cleared->values.data(), buffer);
    EXPECT_EQ(cleared->values.capacity(), capacity);
    cleared = {};

    auto again = storage.checkout<RepositoryTypesCheckout>();
    EXPECT_EQ(again.get(), address);
    EXPECT_EQ(again->values.capacity(), capacity);
}

template<typename Repository, typename T>
concept CanUseAnySymbolOperation =
    requires(Repository& repository, urd::data<T>& data) { repository.insert(data); }
    || requires(const Repository& repository, const urd::data<T>& data) { repository.find(data); }
    || requires(const Repository& repository, urd::index<T> index) { repository[index]; }
    || requires(const Repository& repository, urd::index<T> index) { repository.get_canonical_context(index); }
    || requires(const Repository& repository, urd::index<T> index) { repository.contains(index); }
    || requires(const Repository& repository) { repository.template size<T>(); }
    || requires(const Repository& repository) { repository.template memory_usage<T>(); };

template<typename Repository, typename T>
concept CanUseAnyLocalSymbolOperation =
    requires(Repository& repository) { repository.template get<T>(); } || requires(const Repository& repository) { repository.template get<T>(); };

using ContractSymbols = formalism::SymbolRepository<RepositoryTypesElement>;
using ContractConcurrentSymbols = formalism::ConcurrentSymbolRepository<RepositoryTypesElement>;
using ContractRepository = formalism::Repository<ContractSymbols, formalism::RelationRepository<RepositoryTypesObjectTag, RepositoryTypesRelation>>;
using ContractBinding = formalism::RelationBinding<RepositoryTypesRelation, RepositoryTypesObjectTag>;
static_assert(std::same_as<ContractSymbols::SymbolTypes, boost::mp11::mp_list<RepositoryTypesElement>>);
static_assert(std::same_as<ContractConcurrentSymbols::SymbolTypes, ContractSymbols::SymbolTypes>);
static_assert(std::same_as<ContractRepository::SymbolTypes, ContractSymbols::SymbolTypes>);
static_assert(formalism::SupportsSymbol<const ContractSymbols&, RepositoryTypesElement>);
static_assert(formalism::SupportsSymbol<ContractConcurrentSymbols, RepositoryTypesElement>);
static_assert(formalism::SupportsSymbol<ContractRepository, RepositoryTypesElement>);
static_assert(!formalism::SupportsSymbol<ContractSymbols, RepositoryTypesSerializedElement>);
static_assert(!formalism::SupportsSymbol<ContractRepository, ContractBinding>);
static_assert(!formalism::SupportsSymbol<int, RepositoryTypesElement>);
static_assert(!CanUseAnySymbolOperation<ContractSymbols, RepositoryTypesSerializedElement>);
static_assert(!CanUseAnyLocalSymbolOperation<ContractSymbols, RepositoryTypesSerializedElement>);
static_assert(!CanUseAnySymbolOperation<ContractConcurrentSymbols, RepositoryTypesSerializedElement>);
static_assert(!CanUseAnyLocalSymbolOperation<ContractConcurrentSymbols, RepositoryTypesSerializedElement>);
static_assert(!CanUseAnySymbolOperation<ContractRepository, RepositoryTypesSerializedElement>);
static_assert(Viewable<urd::index<RepositoryTypesElement>, ContractSymbols>);
static_assert(!Viewable<urd::index<RepositoryTypesSerializedElement>, ContractSymbols>);
static_assert(!Viewable<urd::index<RepositoryTypesSerializedElement>, ContractRepository>);

using ContractRelations = formalism::RelationRepository<RepositoryTypesObjectTag, RepositoryTypesRelation>;
using ContractConcurrentRelations = formalism::ConcurrentRelationRepository<RepositoryTypesObjectTag, RepositoryTypesRelation>;
static_assert(std::same_as<ContractRelations::RelationTypes, boost::mp11::mp_list<RepositoryTypesRelation>>);
static_assert(std::same_as<ContractConcurrentRelations::RelationTypes, ContractRelations::RelationTypes>);
static_assert(std::same_as<ContractRepository::RelationTypes, ContractRelations::RelationTypes>);
static_assert(formalism::SupportsRelation<ContractRelations, RepositoryTypesRelation>);
static_assert(formalism::SupportsRelation<ContractRepository, RepositoryTypesRelation>);
static_assert(formalism::RelationRepositoryFor<ContractRepository, RepositoryTypesRelation, RepositoryTypesObjectTag>);
static_assert(formalism::RelationContextFor<ContractRepository, RepositoryTypesRelation, RepositoryTypesObjectTag>);
static_assert(!formalism::RelationRepositoryFor<ContractRepository, RepositoryTypesElement, RepositoryTypesObjectTag>);
static_assert(!formalism::RelationContextFor<ContractRepository, RepositoryTypesRelation, RepositoryTypesPackedObjectTag>);
static_assert(!formalism::SupportsRelation<ContractRelations, RepositoryTypesElement>);
static_assert(!formalism::SupportsRelation<int, RepositoryTypesRelation>);
static_assert(!formalism::SymbolContextFor<ContractSymbols, RepositoryTypesElement>);
static_assert(formalism::SymbolContextFor<ContractRepository, RepositoryTypesElement>);
static_assert(!formalism::SymbolContextFor<ContractSymbols, RepositoryTypesSerializedElement>);
static_assert(!formalism::SymbolRepositoryFor<ContractRepository, ContractBinding>);
static_assert(std::same_as<formalism::BuilderStorage<RepositoryTypesCheckout>::Types, boost::mp11::mp_list<RepositoryTypesCheckout>>);

template<typename Repository, typename T>
concept CanUseUnsupportedRelation =
    requires(Repository& repository, urd::data<formalism::RelationBinding<T, RepositoryTypesObjectTag>>& data) { repository.insert(data); }
    || requires(const Repository& repository, urd::data<formalism::RelationBinding<T, RepositoryTypesObjectTag>>& data) { repository.find(data); }
    || requires(const Repository& repository, ygg::formalism::RelationBindingIndex<T, RepositoryTypesObjectTag> index) { repository[index]; }
    || requires(const Repository& repository, ygg::formalism::RelationBindingIndex<T, RepositoryTypesObjectTag> index) { repository.contains(index); }
    || requires(const Repository& repository, urd::index<T> index) { repository.size(index); } || requires(Repository& repository) { repository.template get<T>(); };
static_assert(!CanUseUnsupportedRelation<ContractRelations, RepositoryTypesElement>);
static_assert(!CanUseUnsupportedRelation<ContractConcurrentRelations, RepositoryTypesElement>);
static_assert(!CanUseUnsupportedRelation<ContractRepository, RepositoryTypesElement>);
static_assert(!Viewable<ygg::formalism::RelationBindingIndex<RepositoryTypesElement, RepositoryTypesObjectTag>, ContractRepository>);

TEST(YggdrasilTests, CommonIndexedViewPreservesLookupHandleAndOwner)
{
    ContractRepository repository(23);
    urd::data<RepositoryTypesElement> data;
    data.value = 31;
    const auto published = repository.insert(data).first;
    const auto view = urd::index_view<RepositoryTypesElement, ContractRepository>(published.index(), &repository);
    EXPECT_EQ(view.index(), published.index());
    EXPECT_EQ(&view.context(), &repository);
    EXPECT_EQ(&view.data(), &published.data());
    EXPECT_EQ(std::get<1>(urd::detail::identity(view)), repository.layer());
}

namespace
{
struct WrongRelationObjectRows
{
    using object_tag = RepositoryTypesObjectTag;
    using RelationTypes = boost::mp11::mp_list<RepositoryTypesRelation>;
    urd::Store<urd::sequence<urd::index<formalism::Object<RepositoryTypesPackedObjectTag>>>> operator[](ygg::formalism::RelationBindingIndex<RepositoryTypesRelation, RepositoryTypesObjectTag>) const;
};
}  // namespace
static_assert(!formalism::RelationRepositoryFor<WrongRelationObjectRows, RepositoryTypesRelation, RepositoryTypesObjectTag>);

namespace
{
struct AliasFreeRelationRows
{
    using RelationTypes = boost::mp11::mp_list<RepositoryTypesRelation>;
    template<typename RelationTag>
    formalism::RelationBindingRow<RelationTag, std::span<const urd::index<formalism::Object<RepositoryTypesObjectTag>>>>
    operator[](ygg::formalism::RelationBindingIndex<RelationTag, RepositoryTypesObjectTag>) const;
    friend const AliasFreeRelationRows& get_repository(const AliasFreeRelationRows& repository URD_LIFETIMEBOUND) noexcept { return repository; }
};

struct MutableRelationRows : AliasFreeRelationRows
{
    formalism::RelationBindingRow<RepositoryTypesRelation, std::span<const urd::index<formalism::Object<RepositoryTypesObjectTag>>>>
    operator[](ygg::formalism::RelationBindingIndex<RepositoryTypesRelation, RepositoryTypesObjectTag>);
};

struct MutableRelationContext
{
    friend const AliasFreeRelationRows& get_repository(const MutableRelationContext&) = delete;
};
}  // namespace

static_assert(formalism::RelationRepositoryFor<AliasFreeRelationRows, RepositoryTypesRelation, RepositoryTypesObjectTag>);
static_assert(formalism::RelationRepositoryFor<const AliasFreeRelationRows&, RepositoryTypesRelation, RepositoryTypesObjectTag>);
static_assert(formalism::RelationContextFor<AliasFreeRelationRows&, RepositoryTypesRelation, RepositoryTypesObjectTag>);
static_assert(!formalism::RelationRepositoryFor<AliasFreeRelationRows, RepositoryTypesRelation, RepositoryTypesPackedObjectTag>);
static_assert(!formalism::RelationRepositoryFor<AliasFreeRelationRows, RepositoryTypesElement, RepositoryTypesObjectTag>);
static_assert(!formalism::RelationRepositoryFor<MutableRelationRows&, RepositoryTypesRelation, RepositoryTypesObjectTag>);
static_assert(!formalism::RelationContextFor<MutableRelationContext&, RepositoryTypesRelation, RepositoryTypesObjectTag>);
static_assert(!formalism::RelationRepositoryFor<int, RepositoryTypesRelation, RepositoryTypesObjectTag>);

TEST(YggdrasilTests, CommonRelationRowsAreTheirOwnRepository)
{
    const auto rows = AliasFreeRelationRows {};
    EXPECT_EQ(&get_repository(rows), &rows);
}

template<typename Relations>
static void expect_binding_access_for_each_relation_type()
{
    using ObjectTag = typename Relations::object_tag;
    using ObjectIndex = urd::index<formalism::Object<ObjectTag>>;
    using FirstBinding = formalism::RelationBinding<RepositoryTypesRelation, ObjectTag>;
    using SecondBinding = formalism::RelationBinding<RepositoryTypesElement, ObjectTag>;
    using Repository = formalism::Repository<ContractConcurrentSymbols, Relations>;
    static_assert(formalism::RelationContextFor<Repository, RepositoryTypesRelation, ObjectTag>);
    static_assert(formalism::RelationContextFor<Repository, RepositoryTypesElement, ObjectTag>);
    static_assert(Viewable<ygg::formalism::RelationBindingIndex<RepositoryTypesRelation, ObjectTag>, Repository>);
    static_assert(Viewable<ygg::formalism::RelationBindingIndex<RepositoryTypesElement, ObjectTag>, Repository>);

    Repository repository(0);
    urd::data<FirstBinding> first_data;
    first_data.relation = urd::index<RepositoryTypesRelation>(0);
    first_data.objects.push_back(ObjectIndex(3));
    urd::data<SecondBinding> second_data;
    second_data.relation = urd::index<RepositoryTypesElement>(0);
    second_data.objects.push_back(ObjectIndex(7));
    second_data.objects.push_back(ObjectIndex(9));
    const auto first = repository.insert(first_data).first;
    const auto second = repository.insert(second_data).first;
    EXPECT_EQ(first.data().objects.size(), 1);
    EXPECT_EQ(first.data().objects[0], ObjectIndex(3));
    EXPECT_EQ(second.data().objects.size(), 2);
    EXPECT_EQ(second.data().objects[0], ObjectIndex(7));
    EXPECT_EQ(second.data().objects[1], ObjectIndex(9));
}

TEST(YggdrasilTests, CommonRelationAccessUsesDecodedObjectTypeForEveryRelation)
{
    expect_binding_access_for_each_relation_type<formalism::RelationRepository<RepositoryTypesObjectTag, RepositoryTypesRelation, RepositoryTypesElement>>();
    expect_binding_access_for_each_relation_type<
        formalism::ConcurrentRelationRepository<RepositoryTypesObjectTag, RepositoryTypesRelation, RepositoryTypesElement>>();
    expect_binding_access_for_each_relation_type<
        formalism::RelationRepository<RepositoryTypesPackedObjectTag, RepositoryTypesRelation, RepositoryTypesElement>>();
    expect_binding_access_for_each_relation_type<
        formalism::ConcurrentRelationRepository<RepositoryTypesPackedObjectTag, RepositoryTypesRelation, RepositoryTypesElement>>();
}

namespace
{
struct PreparedRepository
{
    ContractSymbols symbols;
    size_t preparation_count = 0;
    size_t raw_count = 0;

    std::pair<urd::index_view<RepositoryTypesElement, PreparedRepository>, bool> insert(urd::data<RepositoryTypesElement>& data)
    {
        ++raw_count;
        const auto [index, created] = formalism::intern(symbols.get<RepositoryTypesElement>(), data);
        return { urd::index_view<RepositoryTypesElement, PreparedRepository>(index, this), created };
    }
    const auto& operator[](urd::index<RepositoryTypesElement> index) const { return symbols[index]; }
    friend const PreparedRepository& get_repository(const PreparedRepository& repository URD_LIFETIMEBOUND) noexcept { return repository; }
};

void prepare_for_insert(PreparedRepository& repository, urd::data<RepositoryTypesElement>& data)
{
    ++repository.preparation_count;
    data.value %= 10;
}
}  // namespace

namespace
{
struct MissingPreparationRepository
{
    std::pair<urd::index_view<RepositoryTypesElement, MissingPreparationRepository>, bool> insert(urd::data<RepositoryTypesElement>&);
};
struct WrongPreparationRepository
{
    std::pair<urd::index_view<RepositoryTypesElement, WrongPreparationRepository>, bool> insert(urd::data<RepositoryTypesElement>&);
};
[[maybe_unused]] int prepare_for_insert(WrongPreparationRepository&, urd::data<RepositoryTypesElement>&) { return 0; }
}  // namespace
namespace
{
struct WrongInterningResultRepository
{
    urd::index<RepositoryTypesElement> insert(urd::data<RepositoryTypesElement>&);
};
[[maybe_unused]] void prepare_for_insert(WrongInterningResultRepository&, urd::data<RepositoryTypesElement>&) {}
}  // namespace

static void prepare_for_insert(ContractRepository&, urd::data<ContractBinding>&) noexcept {}

template<typename Repository, typename T>
concept CanPrepareAndIntern = requires(Repository& repository, urd::data<T>& data) { formalism::insert(repository, data); };
static_assert(CanPrepareAndIntern<PreparedRepository, RepositoryTypesElement>);
static_assert(CanPrepareAndIntern<ContractRepository, ContractBinding>);
static_assert(!CanPrepareAndIntern<MissingPreparationRepository, RepositoryTypesElement>);
static_assert(!CanPrepareAndIntern<WrongPreparationRepository, RepositoryTypesElement>);
static_assert(!CanPrepareAndIntern<WrongInterningResultRepository, RepositoryTypesElement>);

TEST(YggdrasilTests, CommonInterningPreparesExactlyOnceBeforeRawInterning)
{
    auto repository = PreparedRepository();
    auto data = urd::data<RepositoryTypesElement>();
    data.value = 17;
    const auto [first, created] = formalism::insert(repository, data);
    EXPECT_TRUE(created);
    EXPECT_EQ(first.data().value, 7);
    EXPECT_EQ(data.index, first.index());
    EXPECT_EQ(repository.preparation_count, 1);
    EXPECT_EQ(repository.raw_count, 1);

    data.value = 27;
    data.index = urd::index<RepositoryTypesElement>(99);
    const auto [duplicate, duplicate_created] = formalism::insert(repository, data);
    EXPECT_FALSE(duplicate_created);
    EXPECT_EQ(duplicate.index(), first.index());
    EXPECT_EQ(data.index, first.index());
    EXPECT_EQ(repository.preparation_count, 2);
    EXPECT_EQ(repository.raw_count, 2);
}

TEST(YggdrasilTests, CommonInterningAcceptsRelationBindingsWithoutSymbolIdentity)
{
    auto repository = ContractRepository(0);
    using Object = formalism::Object<RepositoryTypesObjectTag>;
    auto objects = urd::flat_vector<urd::index<Object>>();
    objects.push_back(urd::index<Object>(3));
    auto data = urd::data<ContractBinding>(urd::index<RepositoryTypesRelation>(0), std::move(objects));
    const auto [first, created] = formalism::insert(repository, data);
    EXPECT_TRUE(created);
    const auto [duplicate, duplicate_created] = formalism::insert(repository, data);
    EXPECT_FALSE(duplicate_created);
    EXPECT_EQ(duplicate.index(), first.index());
    EXPECT_EQ(first.data().objects.front(), urd::index<Object>(3));
}

TEST(YggdrasilTests, CommonRepositoryFactoryAssignsIncreasingIndicesAndParents)
{
    using SymbolRepo = ygg::formalism::SymbolRepository<RepositoryTypesElement>;
    using RelationRepo = ygg::formalism::RelationRepository<RepositoryTypesObjectTag, RepositoryTypesRelation>;
    using Factory = ygg::formalism::RepositoryFactory<SymbolRepo, RelationRepo>;

    auto factory = Factory();
    auto root = factory.create();
    auto child = factory.create(&root);
    auto shared = factory.create_shared(&child);

    EXPECT_EQ(root.layer(), 0);
    EXPECT_EQ(child.layer(), 1);
    EXPECT_EQ(shared->layer(), 2);
    EXPECT_EQ(&root.get_root(), &root);
    EXPECT_EQ(&child.get_root(), &root);
    EXPECT_EQ(&shared->get_root(), &root);
}

TEST(YggdrasilTests, CommonRelationBindingViewIdentityUsesFactoryLocalRepositoryIndices)
{
    using Object = ygg::formalism::Object<RepositoryTypesObjectTag>;
    using Binding = ygg::formalism::RelationBinding<RepositoryTypesRelation, RepositoryTypesObjectTag>;
    using SymbolRepo = ygg::formalism::SymbolRepository<RepositoryTypesElement>;
    using RelationRepo = ygg::formalism::RelationRepository<RepositoryTypesObjectTag, RepositoryTypesRelation>;
    using Repository = ygg::formalism::Repository<SymbolRepo, RelationRepo>;
    using Factory = ygg::formalism::RepositoryFactory<SymbolRepo, RelationRepo>;
    using BindingView = ::ygg::formalism::RelationBindingView<ygg::formalism::RelationBindingIndex<RepositoryTypesRelation, RepositoryTypesObjectTag>, Repository>;

    auto first_factory = Factory();
    auto first_repository = first_factory.create();
    auto second_repository = first_factory.create();
    auto independent_factory = Factory();
    auto independent_repository = independent_factory.create();

    auto objects = urd::flat_vector<urd::index<Object>> {};
    objects.push_back(urd::index<Object>(0));
    const auto data = urd::data<Binding>(urd::index<RepositoryTypesRelation>(0), objects);

    const auto [first, first_created] = first_repository.insert(data);
    const auto [second, second_created] = second_repository.insert(data);
    const auto [independent, independent_created] = independent_repository.insert(data);

    ASSERT_TRUE(first_created);
    ASSERT_TRUE(second_created);
    ASSERT_TRUE(independent_created);
    EXPECT_EQ(first.index().relation, second.index().relation);
    EXPECT_EQ(first.index().row, second.index().row);
    EXPECT_EQ(first.index().relation, independent.index().relation);
    EXPECT_EQ(first.index().row, independent.index().row);

    EXPECT_FALSE(urd::EqualTo<BindingView> {}(first, second));
    EXPECT_NE(urd::Hash<BindingView> {}(first), urd::Hash<BindingView> {}(second));
    EXPECT_TRUE(urd::EqualTo<BindingView> {}(first, independent));
    EXPECT_TRUE(first_repository.contains(first));
    EXPECT_FALSE(first_repository.contains(second));
    EXPECT_FALSE(first_repository.contains(independent));
    EXPECT_TRUE(first_repository.contains(independent.index()));
    EXPECT_EQ(urd::Hash<BindingView> {}(first), urd::Hash<BindingView> {}(independent));
    EXPECT_FALSE(urd::Less<> {}(first.get_key(), second.get_key()));
    EXPECT_FALSE(urd::Less<> {}(second.get_key(), first.get_key()));

    objects[0] = urd::index<Object>(1);
    const auto [larger, larger_created] = second_repository.insert(urd::data<Binding>(urd::index<RepositoryTypesRelation>(0), objects));
    ASSERT_TRUE(larger_created);
    EXPECT_TRUE(urd::Less<> {}(first.get_key(), larger.get_key()));
}

template<typename V>
concept HasPublishedBindingIndex = requires(const V& view) { view.index(); };

using BindingViewRepository =
    formalism::Repository<formalism::SymbolRepository<RepositoryTypesRelation, formalism::Object<RepositoryTypesObjectTag>>, ContractRelations>;
using BorrowedContractBindingView = urd::borrowed_view<ContractBinding, BindingViewRepository>;
using PublishedContractBindingView = ::ygg::formalism::RelationBindingView<ygg::formalism::RelationBindingIndex<RepositoryTypesRelation, RepositoryTypesObjectTag>, BindingViewRepository>;
static_assert(formalism::RelationBindingViewConcept<BorrowedContractBindingView, RepositoryTypesRelation, RepositoryTypesObjectTag>);
static_assert(formalism::RelationBindingViewConcept<PublishedContractBindingView, RepositoryTypesRelation, RepositoryTypesObjectTag>);
static_assert(formalism::RelationBindingViewConcept<const PublishedContractBindingView&, RepositoryTypesRelation, RepositoryTypesObjectTag>);
static_assert(formalism::RelationBindingViewConcept<BorrowedContractBindingView&&, RepositoryTypesRelation, RepositoryTypesObjectTag>);

namespace
{
struct MutableBindingView : PublishedContractBindingView
{
    std::pair<urd::index<RepositoryTypesRelation>, std::span<const urd::index<formalism::Object<RepositoryTypesObjectTag>>>> get_key();
};
}  // namespace
static_assert(!formalism::RelationBindingViewConcept<MutableBindingView&, RepositoryTypesRelation, RepositoryTypesObjectTag>);
static_assert(!formalism::RelationBindingViewConcept<BorrowedContractBindingView, RepositoryTypesRelation, RepositoryTypesPackedObjectTag>);
static_assert(!formalism::RelationBindingViewConcept<BorrowedContractBindingView, RepositoryTypesElement, RepositoryTypesObjectTag>);
static_assert(!formalism::RelationBindingViewConcept<int, RepositoryTypesRelation, RepositoryTypesObjectTag>);
static_assert(!HasPublishedBindingIndex<BorrowedContractBindingView>);
static_assert(HasPublishedBindingIndex<PublishedContractBindingView>);
static_assert(std::same_as<decltype(std::declval<const BorrowedContractBindingView&>().data()), const urd::data<ContractBinding>&>);
static_assert(std::same_as<decltype(std::declval<const BorrowedContractBindingView&>().data()), const urd::data<ContractBinding>&>);

TEST(YggdrasilTests, CommonBorrowedBindingViewReadsWithoutPublishingAndMaterializesConstData)
{
    using Object = formalism::Object<RepositoryTypesObjectTag>;
    auto repository = BindingViewRepository(0);
    auto relation_data = urd::data<RepositoryTypesRelation>();
    const auto relation = repository.insert(relation_data).first.index();
    auto object_data = urd::data<Object>();
    for (uint_t i = 0; i < 8; ++i)
    {
        object_data.value = i;
        repository.insert(object_data);
    }
    auto data = urd::data<ContractBinding>();
    data.relation = relation;
    data.objects.push_back(urd::index<Object>(2));
    data.objects.push_back(urd::index<Object>(5));
    const auto borrowed = urd::resolve(data, repository);
    EXPECT_EQ(&borrowed.data(), &data);
    EXPECT_EQ(&borrowed.data(), &data);
    EXPECT_EQ(&borrowed.context(), &repository);
    EXPECT_EQ(borrowed.get_relation().index(), relation);
    EXPECT_EQ(borrowed.get_objects()[1].index(), urd::index<Object>(5));
    EXPECT_EQ(borrowed.get_key().first, relation);
    EXPECT_EQ(borrowed.get_key().second.data(), data.objects.data());
    EXPECT_FALSE(repository.find(data));

    const auto [published, created] = repository.insert(borrowed.data());
    ASSERT_TRUE(created);
    EXPECT_EQ(&published.context(), &repository);
    EXPECT_TRUE(std::ranges::equal(published.get_key().second, borrowed.get_key().second));
    const auto [duplicate, duplicate_created] = repository.insert(borrowed.data());
    EXPECT_FALSE(duplicate_created);
    EXPECT_EQ(duplicate.index(), published.index());

    // A returned object span borrows Data directly, independently of the wrapper.
    const auto objects = urd::resolve(data, repository).get_objects();
    data.objects[0] = urd::index<Object>(7);
    EXPECT_EQ(objects[0].index(), urd::index<Object>(7));
    EXPECT_EQ(published.get_objects()[0].index(), urd::index<Object>(2));
}

TEST(YggdrasilTests, CommonCompositeRepositorySynchronizesLocalAndInheritedScratchIdentity)
{
    auto root = ContractRepository(0);
    auto data = urd::data<RepositoryTypesElement>();
    data.index = urd::index<RepositoryTypesElement>(99);
    data.value = 7;
    const auto [original, created] = root.insert(data);
    EXPECT_TRUE(created);
    EXPECT_EQ(data.index, original.index());

    data.index = urd::index<RepositoryTypesElement>(99);
    const auto [duplicate, duplicate_created] = root.insert(data);
    EXPECT_FALSE(duplicate_created);
    EXPECT_EQ(duplicate.index(), original.index());
    EXPECT_EQ(data.index, original.index());

    auto child = ContractRepository(1, &root);
    data.index = urd::index<RepositoryTypesElement>(99);
    const auto [inherited, inherited_created] = child.insert(data);
    EXPECT_FALSE(inherited_created);
    EXPECT_EQ(data.index, original.index());
    EXPECT_EQ(&inherited.context(), &root);

    data.index = urd::index<RepositoryTypesElement>(99);
    data.value = 11;
    const auto [local, local_created] = child.insert(data);
    EXPECT_TRUE(local_created);
    EXPECT_EQ(data.index, local.index());
    EXPECT_NE(local.index(), inherited.index());
    EXPECT_EQ(&local.context(), &child);
    EXPECT_EQ(root.size<RepositoryTypesElement>(), 1);
    EXPECT_EQ(child.size<RepositoryTypesElement>(), 2);
}

TEST(YggdrasilTests, CommonRepositoryRejectsWrongArityWithoutMutation)
{
    using Object = ygg::formalism::Object<RepositoryTypesObjectTag>;
    using Binding = ygg::formalism::RelationBinding<RepositoryTypesRelation, RepositoryTypesObjectTag>;

    auto repository = ContractRepository(0);
    const auto relation = urd::index<RepositoryTypesRelation>(0);
    auto objects = urd::flat_vector<urd::index<Object>> {};
    objects.push_back(urd::index<Object>(0));
    objects.push_back(urd::index<Object>(1));
    const auto data = urd::data<Binding>(relation, std::move(objects));
    const auto [view, created] = repository.insert(data);
    ASSERT_TRUE(created);

    auto wrong_objects = urd::flat_vector<urd::index<Object>> {};
    wrong_objects.push_back(urd::index<Object>(0));
    const auto wrong = urd::data<Binding>(relation, std::move(wrong_objects));
    EXPECT_FALSE(repository.find(wrong));
    EXPECT_THROW(repository.insert(wrong), std::invalid_argument);
    EXPECT_EQ(repository.size(relation), 1);
    EXPECT_EQ(repository[view.index()].objects.size(), 2);
}

TEST(YggdrasilTests, CommonRepositoryReportsOwnedMemory)
{
    using Object = ygg::formalism::Object<RepositoryTypesObjectTag>;
    using Binding = ygg::formalism::RelationBinding<RepositoryTypesRelation, RepositoryTypesObjectTag>;
    using SymbolRepository = ygg::formalism::SymbolRepository<RepositoryTypesElement>;
    using RelationRepository = ygg::formalism::RelationRepository<RepositoryTypesObjectTag, RepositoryTypesRelation>;
    using Repository = ygg::formalism::Repository<SymbolRepository, RelationRepository>;

    auto repository = Repository(0);
    EXPECT_EQ(repository.memory_usage<RepositoryTypesElement>(), 0);
    EXPECT_EQ(repository.memory_usage<Binding>(), 0);

    auto element = urd::data<RepositoryTypesElement> {};
    element.value = 7;
    repository.insert(element);

    auto objects = urd::flat_vector<urd::index<Object>> {};
    objects.push_back(urd::index<Object>(0));
    objects.push_back(urd::index<Object>(1));
    const auto data = urd::data<Binding>(urd::index<RepositoryTypesRelation>(0), objects);
    repository.insert(data);

    const auto symbol_memory_usage = repository.memory_usage<RepositoryTypesElement>();
    const auto relation_memory_usage = repository.memory_usage<Binding>();
    EXPECT_GT(symbol_memory_usage, 0);
    EXPECT_GT(relation_memory_usage, 0);

    repository.clear();
    EXPECT_EQ(repository.memory_usage<RepositoryTypesElement>(), symbol_memory_usage);
    EXPECT_LE(repository.memory_usage<Binding>(), relation_memory_usage);

    auto child = Repository(1, &repository);
    EXPECT_EQ(child.memory_usage<RepositoryTypesElement>(), 0);
    EXPECT_EQ(child.memory_usage<Binding>(), 0);
}

TEST(YggdrasilTests, CommonRelationRepositorySupportsBitPackedStorageTrait)
{
    using DefaultRelationRepository = ygg::formalism::RelationRepository<RepositoryTypesObjectTag, RepositoryTypesRelation>;
    using Object = ygg::formalism::Object<RepositoryTypesPackedObjectTag>;
    using Binding = ygg::formalism::RelationBinding<RepositoryTypesRelation, RepositoryTypesPackedObjectTag>;
    using SymbolRepository = ygg::formalism::SymbolRepository<RepositoryTypesElement>;
    using RelationRepository = ygg::formalism::RelationRepository<RepositoryTypesPackedObjectTag, RepositoryTypesRelation>;
    using Factory = ygg::formalism::RepositoryFactory<SymbolRepository, RelationRepository>;

    static_assert(!std::constructible_from<typename DefaultRelationRepository::Rows, urd::bit_width>);
    static_assert(std::constructible_from<typename RelationRepository::Rows, urd::bit_width>);

    auto factory = Factory();
    auto root = factory.create(4);
    const auto relation = urd::index<RepositoryTypesRelation>(0);
    auto objects = urd::flat_vector<urd::index<Object>> {};
    objects.push_back(urd::index<Object>(0));
    objects.push_back(urd::index<Object>(3));
    const auto data = urd::data<Binding>(relation, objects);

    EXPECT_EQ(root.memory_usage<Binding>(), 0);

    const auto [root_view, root_created] = root.insert(data);
    const auto [duplicate_view, duplicate_created] = root.insert(data);

    EXPECT_TRUE(root_created);
    EXPECT_FALSE(duplicate_created);
    EXPECT_GT(root.memory_usage<Binding>(), 0);
    EXPECT_EQ(duplicate_view.index().row, root_view.index().row);
    EXPECT_EQ(root[root_view.index()].objects[0], urd::index<Object>(0));
    EXPECT_EQ(root[root_view.index()].objects[1], urd::index<Object>(3));

    objects[0] = urd::index<Object>(4);
    objects[1] = urd::index<Object>(0);
    const auto wider_data = urd::data<Binding>(relation, objects);

    EXPECT_THROW(root.insert(wider_data), std::out_of_range);
    EXPECT_EQ(root.size(relation), 1);

    const auto [root_duplicate_after_failure, created_after_failure] = root.insert(data);
    EXPECT_FALSE(created_after_failure);
    EXPECT_EQ(root_duplicate_after_failure.index().row, root_view.index().row);

    auto inherited_width_child = factory.create(2, &root);
    objects[0] = urd::index<Object>(3);
    const auto inherited_width_data = urd::data<Binding>(relation, objects);
    const auto [inherited_width_view, inherited_width_created] = inherited_width_child.insert(inherited_width_data);
    EXPECT_TRUE(inherited_width_created);
    EXPECT_EQ(inherited_width_child[inherited_width_view.index()].objects[0], urd::index<Object>(3));

    auto child = factory.create_shared(5, &root);
    const auto [inherited_view, inherited_created] = child->insert(data);
    const auto [child_view, child_created] = child->insert(wider_data);

    EXPECT_FALSE(inherited_created);
    EXPECT_EQ(inherited_view.index().row, root_view.index().row);
    EXPECT_EQ(&inherited_view.context(), &root);
    EXPECT_TRUE(child_created);
    EXPECT_EQ(child_view.index().row, urd::index<ygg::formalism::Row>(1));
    EXPECT_EQ((*child)[child_view.index()].objects[0], urd::index<Object>(4));
    EXPECT_EQ((*child)[child_view.index()].objects[1], urd::index<Object>(0));
}

TEST(YggdrasilTests, CommonRelationRepositoryValidatesObjectIndexWidth)
{
    using Repository = ygg::formalism::RelationRepository<RepositoryTypesPackedObjectTag, RepositoryTypesRelation>;
    using SymbolRepository = ygg::formalism::SymbolRepository<RepositoryTypesElement>;
    using Factory = ygg::formalism::RepositoryFactory<SymbolRepository, Repository>;
    constexpr auto full_width = ygg::formalism::default_object_index_width.bits;

    EXPECT_EQ(Repository().get_object_index_width(), full_width);
    EXPECT_THROW((Repository(nullptr, urd::bit_width { 0 })), std::invalid_argument);
    EXPECT_THROW((Repository(nullptr, urd::bit_width { static_cast<std::uint8_t>(full_width + 1) })), std::invalid_argument);

    const auto parent = Repository(nullptr, urd::bit_width { 2 });
    EXPECT_THROW((Repository(&parent, urd::bit_width { 1 })), std::invalid_argument);

    auto factory = Factory();
    EXPECT_NO_THROW(factory.create(size_t { 0 }));
    EXPECT_NO_THROW(factory.create_shared(1));
    auto full_width_parent = factory.create_shared();
    EXPECT_NO_THROW(factory.create(size_t { 2 }, full_width_parent.get()));
    if constexpr (std::numeric_limits<size_t>::digits > std::numeric_limits<ygg::uint_t>::digits)
    {
        const auto oversized_domain = static_cast<size_t>(std::numeric_limits<ygg::uint_t>::max()) + size_t { 1 };
        EXPECT_THROW(factory.create(oversized_domain), std::invalid_argument);
    }
}

TEST(YggdrasilTests, CommonRepositoryThrowsForMissingSymbolIndices)
{
    using SymbolRepo = ygg::formalism::SymbolRepository<RepositoryTypesElement>;
    using RelationRepo = ygg::formalism::RelationRepository<RepositoryTypesObjectTag, RepositoryTypesRelation>;
    using Repository = ygg::formalism::Repository<SymbolRepo, RelationRepo>;

    auto repository = Repository(0);

    EXPECT_THROW(repository[urd::index<RepositoryTypesElement>(0)], std::out_of_range);
    EXPECT_THROW((void) repository.get_canonical_context(urd::index<RepositoryTypesElement>(0)), std::out_of_range);
    EXPECT_THROW(repository.view(urd::index<RepositoryTypesElement>(0)), std::out_of_range);

    auto data = urd::data<RepositoryTypesElement> {};
    data.value = 7;
    const auto [view, created] = repository.insert(data);

    EXPECT_TRUE(created);
    EXPECT_EQ(repository[view.index()].value, 7);
    EXPECT_EQ(&repository.get_canonical_context(view.index()), &repository);
    EXPECT_THROW(repository[urd::index<RepositoryTypesElement>(1)], std::out_of_range);
    EXPECT_THROW(repository.view(urd::index<RepositoryTypesElement>(1)), std::out_of_range);
}

TEST(YggdrasilTests, CommonRepositoryThrowsForMissingRelationBindingIndices)
{
    using Binding = ygg::formalism::RelationBinding<RepositoryTypesRelation, RepositoryTypesObjectTag>;
    using Object = ygg::formalism::Object<RepositoryTypesObjectTag>;
    using SymbolRepo = ygg::formalism::SymbolRepository<RepositoryTypesElement>;
    using RelationRepo = ygg::formalism::RelationRepository<RepositoryTypesObjectTag, RepositoryTypesRelation>;
    using Repository = ygg::formalism::Repository<SymbolRepo, RelationRepo>;

    auto repository = Repository(0);
    const auto relation = urd::index<RepositoryTypesRelation>(0);
    const auto missing = ygg::formalism::RelationBindingIndex<RepositoryTypesRelation, RepositoryTypesObjectTag> { relation, urd::index<ygg::formalism::Row>(0) };

    EXPECT_THROW(repository[missing], std::out_of_range);
    EXPECT_THROW((void) repository.get_canonical_context(missing), std::out_of_range);
    EXPECT_THROW(urd::resolve(missing, repository), std::out_of_range);
    const auto rows = std::array { urd::index<ygg::formalism::Row>(0) };
    const auto bindings = formalism::bindings_view<RepositoryTypesObjectTag>(relation, rows, repository);
    EXPECT_THROW((void) bindings[0], std::out_of_range);

    auto objects = urd::flat_vector<urd::index<Object>> {};
    objects.push_back(urd::index<Object>(0));
    objects.push_back(urd::index<Object>(1));
    const auto data = urd::data<Binding>(relation, objects);
    const auto [view, created] = repository.insert(data);

    EXPECT_TRUE(created);
    EXPECT_EQ(repository[view.index()].objects.size(), 2);
    EXPECT_EQ(&repository.get_canonical_context(view.index()), &repository);

    const auto missing_row = ygg::formalism::RelationBindingIndex<RepositoryTypesRelation, RepositoryTypesObjectTag> { relation, urd::index<ygg::formalism::Row>(1) };
    const auto missing_relation = ygg::formalism::RelationBindingIndex<RepositoryTypesRelation, RepositoryTypesObjectTag> { urd::index<RepositoryTypesRelation>(1), urd::index<ygg::formalism::Row>(0) };
    EXPECT_THROW(repository[missing_row], std::out_of_range);
    EXPECT_THROW(urd::resolve(missing_row, repository), std::out_of_range);
    EXPECT_THROW(repository[missing_relation], std::out_of_range);
    EXPECT_THROW(urd::resolve(missing_relation, repository), std::out_of_range);
}

template<typename Repository>
static void check_symbol_membership()
{
    using Handle = urd::index<RepositoryTypesElement>;
    using ElementView = urd::index_view<RepositoryTypesElement, Repository>;
    auto root = Repository(0);
    auto data = urd::data<RepositoryTypesElement> {};
    data.value = 7;
    const auto root_view = root.insert(data).first;
    auto child = Repository(1, &root);
    data.value = 11;
    const auto child_view = child.insert(data).first;
    auto grandchild = Repository(2, &child);
    data.value = 13;
    const auto local_view = grandchild.insert(data).first;

    EXPECT_TRUE(grandchild.contains(root_view));
    EXPECT_TRUE(formalism::contains(grandchild, root_view));
    EXPECT_TRUE(grandchild.contains(child_view));
    EXPECT_TRUE(grandchild.contains(local_view));
    EXPECT_FALSE(root.contains(child_view));
    EXPECT_FALSE(child.contains(local_view));
    EXPECT_TRUE(root.contains(ElementView(root_view.index(), &grandchild)));
    EXPECT_FALSE(grandchild.contains(ElementView(local_view.index(), &root)));

    const auto indices = std::array { root_view.index(), child_view.index(), local_view.index() };
    EXPECT_TRUE(std::ranges::all_of(indices, [&](const auto& value) { return grandchild.contains(value); }));
    EXPECT_FALSE(std::ranges::all_of(indices, [&](const auto& value) { return child.contains(value); }));
    EXPECT_TRUE(std::ranges::all_of(urd::resolve(std::span<const Handle>(indices), grandchild), [&](const auto& value) { return grandchild.contains(value); }));
    EXPECT_TRUE(std::ranges::all_of(std::span<const Handle> {}, [&](const auto& value) { return root.contains(value); }));
    EXPECT_TRUE(std::ranges::all_of(std::span<const ElementView> {}, [&](const auto& value) { return root.contains(value); }));
    EXPECT_FALSE(std::ranges::all_of(std::array { root_view.index(), Handle() }, [&](const auto& value) { return grandchild.contains(value); }));
    EXPECT_FALSE(grandchild.contains(Handle(3)));
    EXPECT_FALSE(grandchild.contains(ElementView(Handle(), &grandchild)));

    auto unrelated = Repository(0);
    EXPECT_FALSE(grandchild.contains(ElementView(root_view.index(), &unrelated)));
    data.value = 7;
    const auto unrelated_view = unrelated.insert(data).first;
    ASSERT_EQ(unrelated_view.index(), root_view.index());
    EXPECT_TRUE(grandchild.contains(unrelated_view.index()));
    EXPECT_FALSE(grandchild.contains(unrelated_view));
    EXPECT_FALSE(formalism::contains(grandchild, unrelated_view));
    EXPECT_FALSE(std::ranges::all_of(std::array { root_view, unrelated_view }, [&](const auto& value) { return grandchild.contains(value); }));
}

TEST(YggdrasilTests, CommonSymbolMembershipChecksCanonicalOwnersAcrossLayers)
{
    check_symbol_membership<ContractRepository>();
    check_symbol_membership<formalism::Repository<ContractConcurrentSymbols, ContractConcurrentRelations>>();
}

template<typename Repository>
static void check_relation_membership()
{
    using Handle = ygg::formalism::RelationBindingIndex<RepositoryTypesRelation, RepositoryTypesObjectTag>;
    using BindingView = formalism::RelationBindingView<Handle, Repository>;
    using Object = formalism::Object<RepositoryTypesObjectTag>;
    const auto relation = urd::index<RepositoryTypesRelation>(0);
    auto objects = urd::flat_vector<urd::index<Object>> {};
    objects.push_back(urd::index<Object>(0));
    auto root = Repository(0);
    const auto root_view = root.insert(urd::data<ContractBinding>(relation, objects)).first;
    auto child = Repository(1, &root);
    objects[0] = urd::index<Object>(1);
    const auto child_view = child.insert(urd::data<ContractBinding>(relation, objects)).first;
    auto grandchild = Repository(2, &child);
    objects[0] = urd::index<Object>(2);
    const auto local_view = grandchild.insert(urd::data<ContractBinding>(relation, objects)).first;

    EXPECT_TRUE(grandchild.contains(root_view));
    EXPECT_TRUE(formalism::contains(grandchild, root_view));
    EXPECT_TRUE(grandchild.contains(child_view));
    EXPECT_TRUE(grandchild.contains(local_view));
    EXPECT_FALSE(root.contains(child_view));
    EXPECT_FALSE(child.contains(local_view));
    EXPECT_TRUE(root.contains(BindingView(root_view.index(), &grandchild)));
    EXPECT_FALSE(grandchild.contains(BindingView(local_view.index(), &root)));
    EXPECT_TRUE(std::ranges::all_of(std::array { root_view, child_view, local_view }, [&](const auto& value) { return grandchild.contains(value); }));
    EXPECT_TRUE(std::ranges::all_of(std::array { root_view.index(), child_view.index(), local_view.index() }, [&](const auto& value) { return grandchild.contains(value); }));
    EXPECT_TRUE(std::ranges::all_of(std::span<const Handle> {}, [&](const auto& value) { return root.contains(value); }));
    EXPECT_TRUE(std::ranges::all_of(std::span<const BindingView> {}, [&](const auto& value) { return root.contains(value); }));

    const auto missing_row = Handle { relation, urd::index<formalism::Row>(3) };
    const auto missing_relation = Handle { urd::index<RepositoryTypesRelation>(1), urd::index<formalism::Row>(0) };
    const auto max_row = Handle { relation, urd::index<formalism::Row>() };
    const auto max_relation = Handle { urd::index<RepositoryTypesRelation>(), urd::index<formalism::Row>(0) };
    for (const auto index : { missing_row, missing_relation, max_row, max_relation })
    {
        EXPECT_FALSE(grandchild.contains(index));
        EXPECT_FALSE(grandchild.contains(BindingView(index, &grandchild)));
    }
    EXPECT_FALSE(std::ranges::all_of(std::array { root_view.index(), max_row }, [&](const auto& value) { return grandchild.contains(value); }));

    auto unrelated = Repository(0);
    EXPECT_FALSE(grandchild.contains(BindingView(root_view.index(), &unrelated)));
    objects[0] = urd::index<Object>(0);
    const auto unrelated_view = unrelated.insert(urd::data<ContractBinding>(relation, objects)).first;
    EXPECT_TRUE(grandchild.contains(unrelated_view.index()));
    EXPECT_FALSE(grandchild.contains(unrelated_view));
    EXPECT_FALSE(formalism::contains(grandchild, unrelated_view));
}

TEST(YggdrasilTests, CommonRelationMembershipChecksCanonicalOwnersAcrossLayers)
{
    check_relation_membership<ContractRepository>();
    check_relation_membership<formalism::Repository<ContractConcurrentSymbols, ContractConcurrentRelations>>();
}

}  // namespace ygg::tests
