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

#include "yggdrasil/containers/repository_types.hpp"

#include "yggdrasil/containers/span.hpp"
#include "yggdrasil/formalism/binding_data.hpp"
#include "yggdrasil/formalism/binding_view.hpp"
#include "yggdrasil/formalism/builder.hpp"
#include "yggdrasil/formalism/detail/view.hpp"
#include "yggdrasil/formalism/interning.hpp"
#include "yggdrasil/formalism/membership.hpp"
#include "yggdrasil/formalism/relation_repository.hpp"
#include "yggdrasil/formalism/repository.hpp"
#include "yggdrasil/formalism/repository_factory.hpp"
#include "yggdrasil/formalism/symbol_repository.hpp"
#include "yggdrasil/ids/index_mixins.hpp"
#include "yggdrasil/semantics/equal_to.hpp"
#include "yggdrasil/semantics/hash.hpp"

#include "gtest/gtest.h"
#include <array>
#include <concepts>
#include <cstdint>
#include <forward_list>
#include <iterator>
#include <limits>
#include <list>
#include <ranges>
#include <span>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace ygg::tests
{

struct RepositoryTypesElement;
struct RepositoryTypesSerializedElement;
struct RepositoryTypesRelation;
struct RepositoryTypesObjectTag;
struct RepositoryTypesPackedObjectTag;
struct RepositoryTypesBuilderOther;
struct RepositoryTypesCheckout;

struct RepositoryTypesContext
{
    const RepositoryTypesContext& get_canonical_context(const RepositoryTypesElement&) const noexcept;
    size_t get_index() const noexcept { return 0; }
};

}  // namespace ygg::tests

namespace ygg
{

template<>
struct Index<tests::RepositoryTypesElement> : IndexMixin<Index<tests::RepositoryTypesElement>>
{
    using Base = IndexMixin<Index<tests::RepositoryTypesElement>>;
    using Base::Base;
};

template<>
struct Data<tests::RepositoryTypesElement>
{
    Index<tests::RepositoryTypesElement> index;
    int value = 0;

    auto identifying_members() const noexcept { return std::tie(value); }
};

template<>
struct Index<tests::RepositoryTypesSerializedElement> : IndexMixin<Index<tests::RepositoryTypesSerializedElement>>
{
    using Base = IndexMixin<Index<tests::RepositoryTypesSerializedElement>>;
    using Base::Base;
};

template<>
struct Data<tests::RepositoryTypesSerializedElement>
{
    Index<tests::RepositoryTypesSerializedElement> index;
    IndexList<tests::RepositoryTypesElement> values;

    Data() = default;
    Data(const Data&) = delete;
    Data& operator=(const Data&) = delete;
    Data(Data&&) = default;
    Data& operator=(Data&&) = default;

    auto cista_members() const noexcept { return std::tie(index, values); }
    auto identifying_members() const noexcept { return std::tie(values); }
};

template<>
struct Index<tests::RepositoryTypesRelation> : IndexMixin<Index<tests::RepositoryTypesRelation>>
{
    using Base = IndexMixin<Index<tests::RepositoryTypesRelation>>;
    using Base::Base;
};

template<>
struct Data<tests::RepositoryTypesRelation>
{
    Index<tests::RepositoryTypesRelation> index;
    uint_t value = 0;
    auto identifying_members() const noexcept { return std::tie(value); }
};

template<>
struct Data<formalism::Object<tests::RepositoryTypesObjectTag>>
{
    Index<formalism::Object<tests::RepositoryTypesObjectTag>> index;
    uint_t value = 0;
    auto identifying_members() const noexcept { return std::tie(value); }
};

template<>
struct Index<tests::RepositoryTypesBuilderOther> : IndexMixin<Index<tests::RepositoryTypesBuilderOther>>
{
    using Base = IndexMixin<Index<tests::RepositoryTypesBuilderOther>>;
    using Base::Base;
};

template<>
struct Data<tests::RepositoryTypesBuilderOther>
{
    int value = 0;

    auto identifying_members() const noexcept { return std::tie(value); }
};

template<>
struct Data<tests::RepositoryTypesCheckout>
{
    int index = -1;
    std::vector<int> values;
    size_t clear_count = 0;

    void clear() noexcept
    {
        index = -1;
        values.clear();
        ++clear_count;
    }
};

}  // namespace ygg

namespace ygg::formalism
{

template<>
struct RelationRepositoryTraits<tests::RepositoryTypesPackedObjectTag>
{
    using storage_type = BitPackedArraySetStorage;
};

}  // namespace ygg::formalism

namespace ygg::tests
{

template<typename T>
concept HasWidth = requires(const T& value) { value.width(); };

struct RepositoryTypesRepository
{
    using object_tag = RepositoryTypesObjectTag;
    using RelationTypes = TypeList<RepositoryTypesRelation>;
    using Binding = ygg::formalism::RelationBinding<RepositoryTypesRelation, RepositoryTypesObjectTag>;
    using Object = ygg::formalism::Object<RepositoryTypesObjectTag>;

    std::vector<ygg::Index<Object>> operator[](ygg::Index<Binding>) const { return {}; }
};

inline const RepositoryTypesRepository& get_repository(const RepositoryTypesContext&) noexcept
{
    static const auto repository = RepositoryTypesRepository();
    return repository;
}

TEST(YggdrasilTests, CommonRepositoryTypesUmbrellaHeaderCompiles)
{
    static_assert(CanonicalizableContext<RepositoryTypesElement, RepositoryTypesContext>);
    static_assert(CanonicalizableContextFor<RepositoryTypesContext, RepositoryTypesElement>);

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
static_assert(!CanCheckout<RepositoryTypesElement, ygg::formalism::BuilderStorage<RepositoryTypesElement>>);

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
    EXPECT_EQ(raw->clear_count, 0);
    raw = {};

    auto cleared = ygg::formalism::checkout<RepositoryTypesCheckout>(storage);
    EXPECT_EQ(cleared.get(), address);
    EXPECT_EQ(cleared->index, -1);
    EXPECT_TRUE(cleared->values.empty());
    EXPECT_EQ(cleared->values.data(), buffer);
    EXPECT_EQ(cleared->values.capacity(), capacity);
    EXPECT_EQ(cleared->clear_count, 1);
    cleared = {};

    auto again = storage.checkout<RepositoryTypesCheckout>();
    EXPECT_EQ(again->clear_count, 2);
    EXPECT_EQ(again->values.capacity(), capacity);
}

template<typename Repository, typename T>
concept CanUseAnySymbolOperation =
    requires(Repository& repository, Data<T>& data) { repository.insert(data); }
    || requires(const Repository& repository, const Data<T>& data) { repository.find(data); }
    || requires(const Repository& repository, Index<T> index) { repository[index]; }
    || requires(const Repository& repository, Index<T> index) { repository.get_canonical_context(index); }
    || requires(const Repository& repository, Index<T> index) { repository.contains(index); }
    || requires(const Repository& repository) { repository.template size<T>(); } || requires(const Repository& repository) { repository.template front<T>(); }
    || requires(const Repository& repository) { repository.template memory_usage<T>(); };

template<typename Repository, typename T>
concept CanUseAnyLocalSymbolOperation =
    requires(Repository& repository) { repository.template get<T>(); } || requires(const Repository& repository) { repository.template get<T>(); }
    || requires(const Data<T>& data) { Repository::hash(data); }
    || requires(const Repository& repository, const Data<T>& data) { repository.find_with_hash(data, 0); }
    || requires(const Repository& repository, const Data<T>& data) { repository.find_local(data); }
    || requires(const Repository& repository, const Data<T>& data) { repository.find_local_with_hash(data, 0); }
    || requires(const Repository& repository, const Data<T>& data) { repository.find_local_unsafe_with_hash(data, 0); }
    || requires(Repository& repository, Data<T>& data) { repository.insert_local(data); }
    || requires(Repository& repository, Data<T>& data) { repository.insert_local_with_hash(data, 0); }
    || requires(Repository& repository, Data<T>& data) { repository.create_local_with_hash(data, 0); }
    || requires(const Repository& repository, Index<T> index) { repository.at_local(index); }
    || requires(const Repository& repository, Index<T> index) { repository.is_local(index); }
    || requires(const Repository& repository) { repository.template front_local<T>(); }
    || requires(const Repository& repository) { repository.template local_size<T>(); }
    || requires(const Repository& repository) { repository.template parent_size<T>(); }
    || requires(const Repository& repository) { repository.template exists_parent_mutation<T>(); };

using ContractSymbols = formalism::SymbolRepository<RepositoryTypesElement>;
using ContractConcurrentSymbols = formalism::ConcurrentSymbolRepository<RepositoryTypesElement>;
using ContractRepository = formalism::Repository<ContractSymbols, formalism::RelationRepository<RepositoryTypesObjectTag, RepositoryTypesRelation>>;
using ContractBinding = formalism::RelationBinding<RepositoryTypesRelation, RepositoryTypesObjectTag>;
static_assert(std::same_as<ContractSymbols::SymbolTypes, TypeList<RepositoryTypesElement>>);
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
static_assert(ViewConcept<Index<RepositoryTypesElement>, ContractSymbols>);
static_assert(!ViewConcept<Index<RepositoryTypesSerializedElement>, ContractSymbols>);
static_assert(!ViewConcept<Index<RepositoryTypesSerializedElement>, ContractRepository>);

using ContractRelations = formalism::RelationRepository<RepositoryTypesObjectTag, RepositoryTypesRelation>;
using ContractConcurrentRelations = formalism::ConcurrentRelationRepository<RepositoryTypesObjectTag, RepositoryTypesRelation>;
static_assert(std::same_as<ContractRelations::RelationTypes, TypeList<RepositoryTypesRelation>>);
static_assert(std::same_as<ContractConcurrentRelations::RelationTypes, ContractRelations::RelationTypes>);
static_assert(std::same_as<ContractRepository::RelationTypes, ContractRelations::RelationTypes>);
static_assert(formalism::SupportsRelation<ContractRelations, RepositoryTypesRelation>);
static_assert(formalism::SupportsRelation<ContractRepository, RepositoryTypesRelation>);
static_assert(formalism::RelationRepositoryFor<ContractRelations, ContractBinding>);
static_assert(formalism::RelationRepositoryFor<ContractRepository, ContractBinding>);
static_assert(formalism::RelationContextFor<ContractRepository, ContractBinding>);
static_assert(!formalism::RelationRepositoryFor<ContractRepository, RepositoryTypesElement>);
static_assert(!formalism::RelationContextFor<ContractRepository, formalism::RelationBinding<RepositoryTypesRelation, RepositoryTypesPackedObjectTag>>);
static_assert(!formalism::SupportsRelation<ContractRelations, RepositoryTypesElement>);
static_assert(!formalism::SupportsRelation<int, RepositoryTypesRelation>);
static_assert(formalism::SymbolRepositoryFor<const ContractSymbols&, RepositoryTypesElement>);
static_assert(!formalism::SymbolContextFor<ContractSymbols, RepositoryTypesElement>);
static_assert(formalism::SymbolContextFor<ContractRepository, RepositoryTypesElement>);
static_assert(!formalism::SymbolContextFor<ContractSymbols, RepositoryTypesSerializedElement>);
static_assert(!formalism::SymbolRepositoryFor<ContractRepository, ContractBinding>);
static_assert(std::same_as<formalism::BuilderStorage<RepositoryTypesCheckout>::Types, TypeList<RepositoryTypesCheckout>>);

template<typename Repository, typename T>
concept CanUseUnsupportedRelation =
    requires(Repository& repository, Data<formalism::RelationBinding<T, RepositoryTypesObjectTag>>& data) { repository.insert(data); }
    || requires(const Repository& repository, Data<formalism::RelationBinding<T, RepositoryTypesObjectTag>>& data) { repository.find(data); }
    || requires(const Repository& repository, Index<formalism::RelationBinding<T, RepositoryTypesObjectTag>> index) { repository[index]; }
    || requires(const Repository& repository, Index<formalism::RelationBinding<T, RepositoryTypesObjectTag>> index) { repository.contains(index); }
    || requires(const Repository& repository, Index<T> index) { repository.size(index); } || requires(Repository& repository) { repository.template get<T>(); };
static_assert(!CanUseUnsupportedRelation<ContractRelations, RepositoryTypesElement>);
static_assert(!CanUseUnsupportedRelation<ContractConcurrentRelations, RepositoryTypesElement>);
static_assert(!CanUseUnsupportedRelation<ContractRepository, RepositoryTypesElement>);
static_assert(!ViewConcept<Index<formalism::RelationBinding<RepositoryTypesElement, RepositoryTypesObjectTag>>, ContractRepository>);

struct AdlViewContext
{
    const Data<RepositoryTypesElement>& data;
    const AdlViewContext* owner = nullptr;
    const Data<RepositoryTypesElement>& operator[](Index<RepositoryTypesElement>) const noexcept { return data; }
};

const AdlViewContext& get_canonical_context(Index<RepositoryTypesElement>, const AdlViewContext& context) noexcept
{
    return context.owner ? *context.owner : context;
}

struct WrongAdlViewContext
{
    const Data<RepositoryTypesElement>& operator[](Index<RepositoryTypesElement>) const;
};
int get_canonical_context(Index<RepositoryTypesElement>, const WrongAdlViewContext&);

template<typename Context>
concept CanMakeElementView = requires(const Context& context, Index<RepositoryTypesElement> index) { make_view(index, context); };
static_assert(CanMakeElementView<AdlViewContext>);
static_assert(!CanMakeElementView<WrongAdlViewContext>);

TEST(YggdrasilTests, CommonViewFactoryUsesAdlCanonicalContext)
{
    Data<RepositoryTypesElement> data;
    data.value = 19;
    AdlViewContext parent { data };
    AdlViewContext child { data, &parent };
    const auto view = make_view(Index<RepositoryTypesElement>(0), child);
    static_assert(noexcept(make_view(Index<RepositoryTypesElement>(0), child)));
    EXPECT_EQ(&view.get_context(), &parent);
    EXPECT_EQ(&view.get_data(), &data);
}

TEST(YggdrasilTests, CommonIndexedViewPreservesLookupHandleAndOwner)
{
    ContractRepository repository(23);
    Data<RepositoryTypesElement> data;
    data.value = 31;
    const auto published = repository.insert(data).first;
    formalism::detail::View<Index<RepositoryTypesElement>, ContractRepository> view(published.get_index(), repository);
    EXPECT_EQ(view.get_index(), published.get_index());
    EXPECT_EQ(view.get_handle(), published.get_handle());
    EXPECT_EQ(&view.get_context(), &repository);
    EXPECT_EQ(&view.get_data(), &published.get_data());
    EXPECT_EQ(std::get<1>(view.identifying_members()), repository.get_index());
}

struct WrongRelationObjectRows
{
    using object_tag = RepositoryTypesObjectTag;
    using RelationTypes = TypeList<RepositoryTypesRelation>;
    std::vector<Index<formalism::Object<RepositoryTypesPackedObjectTag>>> operator[](Index<ContractBinding>) const;
};
static_assert(!formalism::RelationRepositoryFor<WrongRelationObjectRows, ContractBinding>);

template<typename Relations>
void expect_binding_access_for_each_relation_type()
{
    using ObjectTag = typename Relations::object_tag;
    using ObjectIndex = Index<formalism::Object<ObjectTag>>;
    using FirstBinding = formalism::RelationBinding<RepositoryTypesRelation, ObjectTag>;
    using SecondBinding = formalism::RelationBinding<RepositoryTypesElement, ObjectTag>;
    using Repository = formalism::Repository<ContractConcurrentSymbols, Relations>;
    static_assert(formalism::RelationRepositoryFor<Relations, FirstBinding>);
    static_assert(formalism::RelationRepositoryFor<Relations, SecondBinding>);
    static_assert(formalism::RelationContextFor<Repository, FirstBinding>);
    static_assert(formalism::RelationContextFor<Repository, SecondBinding>);
    static_assert(ViewConcept<Index<FirstBinding>, Repository>);
    static_assert(ViewConcept<Index<SecondBinding>, Repository>);

    Repository repository(0);
    Data<FirstBinding> first_data;
    first_data.relation = Index<RepositoryTypesRelation>(0);
    first_data.objects.push_back(ObjectIndex(3));
    Data<SecondBinding> second_data;
    second_data.relation = Index<RepositoryTypesElement>(0);
    second_data.objects.push_back(ObjectIndex(7));
    second_data.objects.push_back(ObjectIndex(9));
    const auto first = repository.insert(first_data).first;
    const auto second = repository.insert(second_data).first;
    EXPECT_EQ(first.get_data().size(), 1);
    EXPECT_EQ(first.get_data()[0], ObjectIndex(3));
    EXPECT_EQ(second.get_data().size(), 2);
    EXPECT_EQ(second.get_data()[0], ObjectIndex(7));
    EXPECT_EQ(second.get_data()[1], ObjectIndex(9));
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

struct PreparedRepository
{
    ContractSymbols symbols;
    size_t preparation_count = 0;
    size_t raw_count = 0;

    std::pair<View<Index<RepositoryTypesElement>, PreparedRepository>, bool> insert(Data<RepositoryTypesElement>& data)
    {
        ++raw_count;
        const auto [index, created] = symbols.insert_local(data);
        return { View<Index<RepositoryTypesElement>, PreparedRepository>(index, *this), created };
    }
    const auto& operator[](Index<RepositoryTypesElement> index) const { return symbols[index]; }
};

void prepare_for_insert(PreparedRepository& repository, Data<RepositoryTypesElement>& data)
{
    ++repository.preparation_count;
    data.value %= 10;
}

struct MissingPreparationRepository
{
    std::pair<View<Index<RepositoryTypesElement>, MissingPreparationRepository>, bool> insert(Data<RepositoryTypesElement>&);
};
struct WrongPreparationRepository
{
    std::pair<View<Index<RepositoryTypesElement>, WrongPreparationRepository>, bool> insert(Data<RepositoryTypesElement>&);
};
int prepare_for_insert(WrongPreparationRepository&, Data<RepositoryTypesElement>&);
struct WrongInterningResultRepository
{
    Index<RepositoryTypesElement> insert(Data<RepositoryTypesElement>&);
};
void prepare_for_insert(WrongInterningResultRepository&, Data<RepositoryTypesElement>&);

void prepare_for_insert(ContractRepository&, Data<ContractBinding>&) noexcept {}

template<typename Repository, typename T>
concept CanPrepareAndIntern = requires(Repository& repository, Data<T>& data) { formalism::insert(repository, data); };
static_assert(CanPrepareAndIntern<PreparedRepository, RepositoryTypesElement>);
static_assert(CanPrepareAndIntern<ContractRepository, ContractBinding>);
static_assert(!CanPrepareAndIntern<MissingPreparationRepository, RepositoryTypesElement>);
static_assert(!CanPrepareAndIntern<WrongPreparationRepository, RepositoryTypesElement>);
static_assert(!CanPrepareAndIntern<WrongInterningResultRepository, RepositoryTypesElement>);

TEST(YggdrasilTests, CommonInterningPreparesExactlyOnceBeforeRawInterning)
{
    auto repository = PreparedRepository();
    auto data = Data<RepositoryTypesElement>();
    data.value = 17;
    const auto [first, created] = formalism::insert(repository, data);
    EXPECT_TRUE(created);
    EXPECT_EQ(first.get_data().value, 7);
    EXPECT_EQ(data.index, first.get_index());
    EXPECT_EQ(repository.preparation_count, 1);
    EXPECT_EQ(repository.raw_count, 1);

    data.value = 27;
    data.index = Index<RepositoryTypesElement>(99);
    const auto [duplicate, duplicate_created] = formalism::insert(repository, data);
    EXPECT_FALSE(duplicate_created);
    EXPECT_EQ(duplicate.get_index(), first.get_index());
    EXPECT_EQ(data.index, first.get_index());
    EXPECT_EQ(repository.preparation_count, 2);
    EXPECT_EQ(repository.raw_count, 2);
}

TEST(YggdrasilTests, CommonInterningAcceptsRelationBindingsWithoutSymbolIdentity)
{
    auto repository = ContractRepository(0);
    using Object = formalism::Object<RepositoryTypesObjectTag>;
    auto objects = IndexList<Object>();
    objects.push_back(Index<Object>(3));
    auto data = Data<ContractBinding>(Index<RepositoryTypesRelation>(0), 1, std::move(objects));
    const auto [first, created] = formalism::insert(repository, data);
    EXPECT_TRUE(created);
    const auto [duplicate, duplicate_created] = formalism::insert(repository, data);
    EXPECT_FALSE(duplicate_created);
    EXPECT_EQ(duplicate.get_index(), first.get_index());
    EXPECT_EQ(first.get_data().front(), Index<Object>(3));
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

    EXPECT_EQ(root.get_index(), 0);
    EXPECT_EQ(child.get_index(), 1);
    EXPECT_EQ(shared->get_index(), 2);
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
    using BindingView = ygg::View<ygg::Index<Binding>, Repository>;

    auto first_factory = Factory();
    auto first_repository = first_factory.create();
    auto second_repository = first_factory.create();
    auto independent_factory = Factory();
    auto independent_repository = independent_factory.create();

    auto objects = ygg::IndexList<Object> {};
    objects.push_back(ygg::Index<Object>(0));
    const auto data = ygg::Data<Binding>(ygg::Index<RepositoryTypesRelation>(0), 1, objects);

    const auto [first, first_created] = first_repository.insert(data);
    const auto [second, second_created] = second_repository.insert(data);
    const auto [independent, independent_created] = independent_repository.insert(data);

    ASSERT_TRUE(first_created);
    ASSERT_TRUE(second_created);
    ASSERT_TRUE(independent_created);
    EXPECT_EQ(first.get_index().relation, second.get_index().relation);
    EXPECT_EQ(first.get_index().row, second.get_index().row);
    EXPECT_EQ(first.get_index().relation, independent.get_index().relation);
    EXPECT_EQ(first.get_index().row, independent.get_index().row);

    EXPECT_FALSE(ygg::EqualTo<BindingView> {}(first, second));
    EXPECT_NE(ygg::Hash<BindingView> {}(first), ygg::Hash<BindingView> {}(second));
    EXPECT_TRUE(ygg::EqualTo<BindingView> {}(first, independent));
    EXPECT_TRUE(first_repository.contains(first));
    EXPECT_FALSE(first_repository.contains(second));
    EXPECT_FALSE(first_repository.contains(independent));
    EXPECT_TRUE(first_repository.contains(independent.get_index()));
    EXPECT_EQ(ygg::Hash<BindingView> {}(first), ygg::Hash<BindingView> {}(independent));
    EXPECT_FALSE(ygg::Less<> {}(first.get_key(), second.get_key()));
    EXPECT_FALSE(ygg::Less<> {}(second.get_key(), first.get_key()));

    objects[0] = ygg::Index<Object>(1);
    const auto [larger, larger_created] = second_repository.insert(ygg::Data<Binding>(ygg::Index<RepositoryTypesRelation>(0), 1, objects));
    ASSERT_TRUE(larger_created);
    EXPECT_TRUE(ygg::Less<> {}(first.get_key(), larger.get_key()));
}

template<typename V>
concept HasPublishedBindingIndex = requires(const V& view) { view.get_index(); };

using BindingViewRepository =
    formalism::Repository<formalism::SymbolRepository<RepositoryTypesRelation, formalism::Object<RepositoryTypesObjectTag>>, ContractRelations>;
using BorrowedContractBindingView = View<Data<ContractBinding>, BindingViewRepository>;
using PublishedContractBindingView = View<Index<ContractBinding>, BindingViewRepository>;
static_assert(formalism::RelationBindingViewConcept<BorrowedContractBindingView, RepositoryTypesRelation, RepositoryTypesObjectTag>);
static_assert(formalism::RelationBindingViewConcept<PublishedContractBindingView, RepositoryTypesRelation, RepositoryTypesObjectTag>);
static_assert(!formalism::RelationBindingViewConcept<BorrowedContractBindingView, RepositoryTypesRelation, RepositoryTypesPackedObjectTag>);
static_assert(!formalism::RelationBindingViewConcept<BorrowedContractBindingView, RepositoryTypesElement, RepositoryTypesObjectTag>);
static_assert(!formalism::RelationBindingViewConcept<int, RepositoryTypesRelation, RepositoryTypesObjectTag>);
static_assert(!HasPublishedBindingIndex<BorrowedContractBindingView>);
static_assert(HasPublishedBindingIndex<PublishedContractBindingView>);
static_assert(std::same_as<decltype(std::declval<const BorrowedContractBindingView&>().get_data()), const Data<ContractBinding>&>);
static_assert(std::same_as<decltype(std::declval<const BorrowedContractBindingView&>().get_handle()), const Data<ContractBinding>&>);

TEST(YggdrasilTests, CommonBorrowedBindingViewReadsWithoutPublishingAndMaterializesConstData)
{
    using Object = formalism::Object<RepositoryTypesObjectTag>;
    auto repository = BindingViewRepository(0);
    auto relation_data = Data<RepositoryTypesRelation>();
    const auto relation = repository.insert(relation_data).first.get_index();
    auto object_data = Data<Object>();
    for (uint_t i = 0; i < 8; ++i)
    {
        object_data.value = i;
        repository.insert(object_data);
    }
    auto data = Data<ContractBinding>();
    data.relation = relation;
    data.objects.push_back(Index<Object>(2));
    data.objects.push_back(Index<Object>(5));
    const auto borrowed = make_view(data, repository);
    EXPECT_EQ(&borrowed.get_data(), &data);
    EXPECT_EQ(&borrowed.get_handle(), &data);
    EXPECT_EQ(&borrowed.get_context(), &repository);
    EXPECT_EQ(borrowed.get_relation().get_index(), relation);
    EXPECT_EQ(borrowed.get_objects().get_data().data(), data.objects.data());
    EXPECT_EQ(borrowed.get_objects()[1].get_index(), Index<Object>(5));
    EXPECT_EQ(borrowed.get_key().first, relation);
    EXPECT_EQ(borrowed.get_key().second.data(), data.objects.data());
    EXPECT_FALSE(repository.find(data));

    const auto [published, created] = repository.insert(borrowed.get_data());
    ASSERT_TRUE(created);
    EXPECT_EQ(&published.get_context(), &repository);
    EXPECT_TRUE(std::ranges::equal(published.get_key().second, borrowed.get_key().second));
    const auto [duplicate, duplicate_created] = repository.insert(borrowed.get_data());
    EXPECT_FALSE(duplicate_created);
    EXPECT_EQ(duplicate.get_index(), published.get_index());

    // A returned object span borrows Data directly, independently of the wrapper.
    const auto objects = make_view(data, repository).get_objects();
    data.objects[0] = Index<Object>(7);
    EXPECT_EQ(objects[0].get_index(), Index<Object>(7));
    EXPECT_EQ(published.get_objects()[0].get_index(), Index<Object>(2));
}

TEST(YggdrasilTests, CommonRelationBindingDataValidatesArity)
{
    using Object = ygg::formalism::Object<RepositoryTypesObjectTag>;
    using Binding = ygg::formalism::RelationBinding<RepositoryTypesRelation, RepositoryTypesObjectTag>;

    auto objects = ygg::IndexList<Object> {};
    objects.push_back(ygg::Index<Object>(0));
    objects.push_back(ygg::Index<Object>(1));

    EXPECT_NO_THROW((ygg::Data<Binding>(ygg::Index<RepositoryTypesRelation>(0), 2, objects)));
    EXPECT_THROW((ygg::Data<Binding>(ygg::Index<RepositoryTypesRelation>(0), 1, objects)), std::invalid_argument);
}

TEST(YggdrasilTests, CommonBasicSymbolRepositorySynchronizesScratchIdentityOnEveryInsertionPath)
{
    auto repository = formalism::BasicSymbolRepository<RepositoryTypesElement>();
    auto data = Data<RepositoryTypesElement>();
    data.index = Index<RepositoryTypesElement>(99);
    data.value = 7;
    const auto [index, created] = repository.insert_local(data);
    EXPECT_TRUE(created);
    EXPECT_EQ(index, Index<RepositoryTypesElement>(0));
    EXPECT_EQ(data.index, index);

    data.index = Index<RepositoryTypesElement>(99);
    const auto [duplicate, duplicate_created] = repository.insert_local(data);
    EXPECT_FALSE(duplicate_created);
    EXPECT_EQ(duplicate, index);
    EXPECT_EQ(data.index, index);

    data.index = Index<RepositoryTypesElement>(99);
    const auto [rechecked, rechecked_created] = repository.create_local_with_hash(data, decltype(repository)::hash(data));
    EXPECT_FALSE(rechecked_created);
    EXPECT_EQ(rechecked, index);
    EXPECT_EQ(data.index, index);
    EXPECT_EQ(repository.local_size(), 1);
}

TEST(YggdrasilTests, CommonCompositeRepositorySynchronizesLocalAndInheritedScratchIdentity)
{
    auto root = ContractRepository(0);
    auto data = Data<RepositoryTypesElement>();
    data.index = Index<RepositoryTypesElement>(99);
    data.value = 7;
    const auto [original, created] = root.insert(data);
    EXPECT_TRUE(created);
    EXPECT_EQ(data.index, original.get_index());

    data.index = Index<RepositoryTypesElement>(99);
    const auto [duplicate, duplicate_created] = root.insert(data);
    EXPECT_FALSE(duplicate_created);
    EXPECT_EQ(duplicate.get_index(), original.get_index());
    EXPECT_EQ(data.index, original.get_index());

    auto child = ContractRepository(1, &root);
    data.index = Index<RepositoryTypesElement>(99);
    const auto [inherited, inherited_created] = child.insert(data);
    EXPECT_FALSE(inherited_created);
    EXPECT_EQ(data.index, original.get_index());
    EXPECT_EQ(&inherited.get_context(), &root);

    data.index = Index<RepositoryTypesElement>(99);
    data.value = 11;
    const auto [local, local_created] = child.insert(data);
    EXPECT_TRUE(local_created);
    EXPECT_EQ(data.index, local.get_index());
    EXPECT_NE(local.get_index(), inherited.get_index());
    EXPECT_EQ(&local.get_context(), &child);
    EXPECT_EQ(root.size<RepositoryTypesElement>(), 1);
    EXPECT_EQ(child.size<RepositoryTypesElement>(), 2);
}

TEST(YggdrasilTests, CommonBasicSymbolRepositoryFrontLocalIsChecked)
{
    auto repository = ygg::formalism::BasicSymbolRepository<RepositoryTypesElement>();

    EXPECT_THROW(repository.front_local(), std::out_of_range);
    EXPECT_EQ(repository.memory_usage(), 0);

    auto data = ygg::Data<RepositoryTypesElement> {};
    data.value = 7;
    const auto [index, created] = repository.insert_local(data);

    EXPECT_TRUE(created);
    EXPECT_EQ(index, ygg::Index<RepositoryTypesElement>(0));
    EXPECT_EQ(repository.front_local().value, 7);
    const auto memory_usage = repository.memory_usage();
    EXPECT_GT(memory_usage, 0);

    repository.clear();
    EXPECT_THROW(repository.front_local(), std::out_of_range);
    EXPECT_EQ(repository.memory_usage(), memory_usage);
}

TEST(YggdrasilTests, CommonBasicSymbolRepositorySupportsSerializedStorageAfterMoveAndClear)
{
    using Tag = RepositoryTypesSerializedElement;
    static_assert(!uses_trivial_storage_v<Tag>);

    auto repository = ygg::formalism::BasicSymbolRepository<Tag>();
    EXPECT_EQ(repository.memory_usage(), 0);
    auto data = ygg::Data<Tag> {};
    data.values.push_back(ygg::Index<RepositoryTypesElement>(7));

    const auto [index, created] = repository.insert_local(data);
    EXPECT_TRUE(created);
    EXPECT_EQ(index, ygg::Index<Tag>(0));
    EXPECT_EQ(repository.front_local().values[0], ygg::Index<RepositoryTypesElement>(7));
    EXPECT_GT(repository.memory_usage(), 0);

    data.index = ygg::Index<Tag>(99);
    const auto [duplicate_index, duplicate_created] = repository.insert_local(data);
    EXPECT_FALSE(duplicate_created);
    EXPECT_EQ(duplicate_index, index);
    EXPECT_EQ(data.index, index);

    auto moved = std::move(repository);
    EXPECT_EQ(moved.find_local(data), index);
    EXPECT_EQ(moved.front_local().values[0], ygg::Index<RepositoryTypesElement>(7));

    auto assigned = ygg::formalism::BasicSymbolRepository<Tag>();
    assigned = std::move(moved);
    EXPECT_EQ(assigned.find_local(data), index);

    assigned.clear();
    EXPECT_EQ(assigned.local_size(), 0);
    const auto [reused_index, reused_created] = assigned.insert_local(data);
    EXPECT_TRUE(reused_created);
    EXPECT_EQ(reused_index, ygg::Index<Tag>(0));
    EXPECT_EQ(assigned.front_local().values[0], ygg::Index<RepositoryTypesElement>(7));
}

TEST(YggdrasilTests, CommonSymbolRepositoryTracksParentAndLocalSize)
{
    using Repository = ygg::formalism::SymbolRepository<RepositoryTypesElement>;

    auto root = Repository();
    auto child = Repository(&root);

    EXPECT_EQ(root.size<RepositoryTypesElement>(), 0);
    EXPECT_EQ(child.size<RepositoryTypesElement>(), 0);
    EXPECT_EQ(child.parent_size<RepositoryTypesElement>(), 0);
    EXPECT_EQ(child.local_size<RepositoryTypesElement>(), 0);

    auto root_data = ygg::Data<RepositoryTypesElement> {};
    root_data.value = 7;
    const auto [root_index, root_created] = root.insert_local(root_data);

    EXPECT_TRUE(root_created);
    EXPECT_EQ(root_index, ygg::Index<RepositoryTypesElement>(0));
    EXPECT_EQ(root.size<RepositoryTypesElement>(), 1);
    EXPECT_EQ(child.size<RepositoryTypesElement>(), 0);

    child.clear();
    EXPECT_EQ(child.parent_size<RepositoryTypesElement>(), 1);
    EXPECT_EQ(child.local_size<RepositoryTypesElement>(), 0);
    EXPECT_EQ(child.size<RepositoryTypesElement>(), 1);

    auto child_data = ygg::Data<RepositoryTypesElement> {};
    child_data.value = 11;
    const auto [child_index, child_created] = child.insert_local(child_data);

    EXPECT_TRUE(child_created);
    EXPECT_EQ(child_index, ygg::Index<RepositoryTypesElement>(1));
    EXPECT_EQ(child.local_size<RepositoryTypesElement>(), 1);
    EXPECT_EQ(child.size<RepositoryTypesElement>(), 2);
}

TEST(YggdrasilTests, CommonSymbolRepositoryForwardsAcrossParents)
{
    using Repository = ygg::formalism::SymbolRepository<RepositoryTypesElement>;
    static_assert(ygg::CanonicalizableContext<ygg::Index<RepositoryTypesElement>, Repository>);
    static_assert(ygg::ViewConcept<ygg::Index<RepositoryTypesElement>, Repository>);

    auto root = Repository();
    const auto missing = ygg::Index<RepositoryTypesElement>(0);
    auto data = ygg::Data<RepositoryTypesElement> {};
    data.value = 7;
    const auto hash = Repository::hash(data);

    EXPECT_EQ(root.find_with_hash(data, hash), std::nullopt);
    EXPECT_EQ(root.find(data), std::nullopt);
    EXPECT_THROW(root[missing], std::out_of_range);
    EXPECT_THROW(root.front<RepositoryTypesElement>(), std::out_of_range);
    EXPECT_THROW(root.get_canonical_context(missing), std::out_of_range);

    const auto [view, created] = root.insert(data);

    EXPECT_TRUE(created);
    EXPECT_EQ(view.get_index(), ygg::Index<RepositoryTypesElement>(0));
    EXPECT_EQ(view.get_data().value, 7);
    EXPECT_EQ(&view.get_context(), &root);
    const auto root_found = root.find_with_hash(data, hash);
    ASSERT_TRUE(root_found.has_value());
    EXPECT_EQ(root_found->get_index(), view.get_index());
    EXPECT_EQ(&root_found->get_context(), &root);
    EXPECT_EQ(root[view.get_index()].value, 7);
    EXPECT_EQ(root.front<RepositoryTypesElement>().value, 7);
    EXPECT_EQ(&root.get_canonical_context(view.get_index()), &root);

    data.index = ygg::Index<RepositoryTypesElement>(99);
    const auto [duplicate_view, duplicate_created] = root.insert(data);
    EXPECT_FALSE(duplicate_created);
    EXPECT_EQ(data.index, view.get_index());
    EXPECT_EQ(duplicate_view.get_index(), view.get_index());

    auto child = Repository(&root);
    data.index = ygg::Index<RepositoryTypesElement>(99);
    const auto [child_view, child_created] = child.insert(data);
    EXPECT_EQ(data.index, view.get_index());

    EXPECT_FALSE(child_created);
    EXPECT_EQ(child_view.get_index(), view.get_index());
    EXPECT_EQ(&child_view.get_context(), &root);
    const auto child_found = child.find(data);
    ASSERT_TRUE(child_found.has_value());
    EXPECT_EQ(child_found->get_index(), view.get_index());
    EXPECT_EQ(&child_found->get_context(), &root);
    EXPECT_EQ(child[view.get_index()].value, 7);
    EXPECT_EQ(child.front<RepositoryTypesElement>().value, 7);
    EXPECT_EQ(&child.get_canonical_context(view.get_index()), &root);
    EXPECT_EQ(&ygg::make_view(view.get_index(), child).get_context(), &root);

    auto child_data = ygg::Data<RepositoryTypesElement> {};
    child_data.value = 11;
    const auto [local_view, local_created] = child.insert(child_data);

    EXPECT_TRUE(local_created);
    EXPECT_EQ(local_view.get_index(), ygg::Index<RepositoryTypesElement>(1));
    EXPECT_EQ(&local_view.get_context(), &child);
    EXPECT_EQ(root.find(child_data), std::nullopt);
    const auto local_found = child.find(child_data);
    ASSERT_TRUE(local_found.has_value());
    EXPECT_EQ(local_found->get_index(), local_view.get_index());
    EXPECT_EQ(&local_found->get_context(), &child);
    EXPECT_EQ(child[local_view.get_index()].value, 11);
    EXPECT_EQ(&child.get_canonical_context(local_view.get_index()), &child);
}

TEST(YggdrasilTests, CommonBasicRelationRepositoryFrontLocalIsChecked)
{
    using Object = ygg::formalism::Object<RepositoryTypesObjectTag>;
    using Binding = ygg::formalism::RelationBinding<RepositoryTypesRelation, RepositoryTypesObjectTag>;
    using Repository = ygg::formalism::BasicRelationRepository<RepositoryTypesObjectTag, RepositoryTypesRelation>;

    auto repository = Repository();
    const auto relation = ygg::Index<RepositoryTypesRelation>(0);

    EXPECT_THROW(repository.front_local(relation), std::out_of_range);

    auto objects = ygg::IndexList<Object> {};
    objects.push_back(ygg::Index<Object>(0));
    objects.push_back(ygg::Index<Object>(1));
    const auto data = ygg::Data<Binding>(relation, 2, objects);
    const auto [row, created] = repository.insert_local(data);

    EXPECT_TRUE(created);
    EXPECT_EQ(row, ygg::Index<ygg::formalism::Row>(0));
    EXPECT_EQ(repository.front_local(relation).size(), 2);

    repository.clear();
    EXPECT_THROW(repository.front_local(relation), std::out_of_range);
}

TEST(YggdrasilTests, CommonRelationRepositoryForwardsAcrossParents)
{
    using Object = ygg::formalism::Object<RepositoryTypesObjectTag>;
    using Binding = ygg::formalism::RelationBinding<RepositoryTypesRelation, RepositoryTypesObjectTag>;
    using Repository = ygg::formalism::RelationRepository<RepositoryTypesObjectTag, RepositoryTypesRelation>;
    static_assert(ygg::CanonicalizableContext<ygg::Index<Binding>, Repository>);
    static_assert(ygg::ViewConcept<ygg::Index<Binding>, Repository>);

    auto root = Repository(0);
    const auto relation = ygg::Index<RepositoryTypesRelation>(0);
    const auto missing = ygg::Index<Binding> { relation, ygg::Index<ygg::formalism::Row>(0) };

    auto objects = ygg::IndexList<Object> {};
    objects.push_back(ygg::Index<Object>(0));
    objects.push_back(ygg::Index<Object>(1));
    const auto data = ygg::Data<Binding>(relation, 2, objects);

    EXPECT_EQ(root.find(data), std::nullopt);
    EXPECT_THROW(root[missing], std::out_of_range);
    EXPECT_THROW(root.front(relation), std::out_of_range);
    EXPECT_THROW(root.get_canonical_context(missing), std::out_of_range);

    const auto [view, created] = root.insert(data);

    EXPECT_TRUE(created);
    const auto root_found = root.find(data);
    ASSERT_TRUE(root_found.has_value());
    EXPECT_EQ(root_found->get_index().relation, view.get_index().relation);
    EXPECT_EQ(root_found->get_index().row, view.get_index().row);
    EXPECT_EQ(&root_found->get_context(), &root);
    EXPECT_EQ(view.get_data().size(), 2);
    EXPECT_EQ(view.get_relation().get_index(), relation);
    EXPECT_EQ(view.get_objects().size(), 2);
    EXPECT_EQ(std::get<1>(view.identifying_members()), root.get_index());
    EXPECT_EQ(&view.get_context(), &root);
    EXPECT_EQ(root[view.get_index()].size(), 2);
    EXPECT_EQ(root.front(relation).size(), 2);
    EXPECT_EQ(&root.get_canonical_context(view.get_index()), &root);

    auto child = Repository(1, &root);
    const auto [child_view, child_created] = child.insert(data);

    EXPECT_FALSE(child_created);
    EXPECT_EQ(child_view.get_index().relation, view.get_index().relation);
    EXPECT_EQ(child_view.get_index().row, view.get_index().row);
    EXPECT_EQ(&child_view.get_context(), &root);
    const auto child_found = child.find(data);
    ASSERT_TRUE(child_found.has_value());
    EXPECT_EQ(child_found->get_index().relation, view.get_index().relation);
    EXPECT_EQ(child_found->get_index().row, view.get_index().row);
    EXPECT_EQ(&child_found->get_context(), &root);
    EXPECT_EQ(child[view.get_index()].size(), 2);
    EXPECT_EQ(child.front(relation).size(), 2);
    EXPECT_EQ(&child.get_canonical_context(view.get_index()), &root);
    EXPECT_EQ(&ygg::make_view(view.get_index(), child).get_context(), &root);

    auto child_objects = ygg::IndexList<Object> {};
    child_objects.push_back(ygg::Index<Object>(1));
    child_objects.push_back(ygg::Index<Object>(0));
    const auto child_data = ygg::Data<Binding>(relation, 2, child_objects);
    const auto [local_view, local_created] = child.insert(child_data);

    EXPECT_TRUE(local_created);
    EXPECT_EQ(local_view.get_index().row, ygg::Index<ygg::formalism::Row>(1));
    EXPECT_EQ(&local_view.get_context(), &child);
    EXPECT_EQ(local_view.get_data().size(), 2);
    EXPECT_EQ(root.find(child_data), std::nullopt);
    const auto local_found = child.find(child_data);
    ASSERT_TRUE(local_found.has_value());
    EXPECT_EQ(local_found->get_index().row, local_view.get_index().row);
    EXPECT_EQ(&local_found->get_context(), &child);
}

TEST(YggdrasilTests, CommonRelationRepositoryRejectsWrongArityWithoutMutation)
{
    using Object = ygg::formalism::Object<RepositoryTypesObjectTag>;
    using Binding = ygg::formalism::RelationBinding<RepositoryTypesRelation, RepositoryTypesObjectTag>;
    using Repository = ygg::formalism::RelationRepository<RepositoryTypesObjectTag, RepositoryTypesRelation>;

    auto repository = Repository(0);
    const auto relation = ygg::Index<RepositoryTypesRelation>(0);
    auto objects = ygg::IndexList<Object> {};
    objects.push_back(ygg::Index<Object>(0));
    objects.push_back(ygg::Index<Object>(1));
    const auto data = ygg::Data<Binding>(relation, 2, std::move(objects));
    const auto [view, created] = repository.insert(data);
    ASSERT_TRUE(created);

    auto wrong_objects = ygg::IndexList<Object> {};
    wrong_objects.push_back(ygg::Index<Object>(0));
    const auto wrong = ygg::Data<Binding>(relation, 1, std::move(wrong_objects));
    static_assert(!noexcept(repository.find_with_hash(wrong, Repository::hash(wrong))));
    static_assert(!noexcept(repository.find(wrong)));

    EXPECT_THROW(repository.find_with_hash(wrong, Repository::hash(wrong)), std::invalid_argument);
    EXPECT_THROW(repository.find(wrong), std::invalid_argument);
    EXPECT_THROW(repository.insert(wrong), std::invalid_argument);
    EXPECT_EQ(repository.size(relation), 1);
    EXPECT_EQ(repository[view.get_index()].size(), 2);
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

    auto element = ygg::Data<RepositoryTypesElement> {};
    element.value = 7;
    repository.insert(element);

    auto objects = ygg::IndexList<Object> {};
    objects.push_back(ygg::Index<Object>(0));
    objects.push_back(ygg::Index<Object>(1));
    const auto data = ygg::Data<Binding>(ygg::Index<RepositoryTypesRelation>(0), 2, objects);
    repository.insert(data);

    const auto symbol_memory_usage = repository.memory_usage<RepositoryTypesElement>();
    const auto relation_memory_usage = repository.memory_usage<Binding>();
    EXPECT_GT(symbol_memory_usage, 0);
    EXPECT_GT(relation_memory_usage, 0);

    repository.clear();
    EXPECT_EQ(repository.memory_usage<RepositoryTypesElement>(), symbol_memory_usage);
    EXPECT_EQ(repository.memory_usage<Binding>(), relation_memory_usage);

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

    static_assert(!HasWidth<typename DefaultRelationRepository::container_type>);
    static_assert(HasWidth<typename RelationRepository::container_type>);

    auto factory = Factory();
    auto root = factory.create(4);
    const auto relation = ygg::Index<RepositoryTypesRelation>(0);
    auto objects = ygg::IndexList<Object> {};
    objects.push_back(ygg::Index<Object>(0));
    objects.push_back(ygg::Index<Object>(3));
    const auto data = ygg::Data<Binding>(relation, 2, objects);

    EXPECT_EQ(root.memory_usage<Binding>(), 0);

    const auto [root_view, root_created] = root.insert(data);
    const auto [duplicate_view, duplicate_created] = root.insert(data);

    EXPECT_TRUE(root_created);
    EXPECT_FALSE(duplicate_created);
    EXPECT_GT(root.memory_usage<Binding>(), 0);
    EXPECT_EQ(duplicate_view.get_index().row, root_view.get_index().row);
    EXPECT_EQ(root[root_view.get_index()][0], ygg::Index<Object>(0));
    EXPECT_EQ(root[root_view.get_index()][1], ygg::Index<Object>(3));

    objects[0] = ygg::Index<Object>(4);
    objects[1] = ygg::Index<Object>(0);
    const auto wider_data = ygg::Data<Binding>(relation, 2, objects);

    EXPECT_THROW(root.insert(wider_data), std::out_of_range);
    EXPECT_EQ(root.size(relation), 1);

    const auto [root_duplicate_after_failure, created_after_failure] = root.insert(data);
    EXPECT_FALSE(created_after_failure);
    EXPECT_EQ(root_duplicate_after_failure.get_index().row, root_view.get_index().row);

    auto inherited_width_child = factory.create(2, &root);
    objects[0] = ygg::Index<Object>(3);
    const auto inherited_width_data = ygg::Data<Binding>(relation, 2, objects);
    const auto [inherited_width_view, inherited_width_created] = inherited_width_child.insert(inherited_width_data);
    EXPECT_TRUE(inherited_width_created);
    EXPECT_EQ(inherited_width_child[inherited_width_view.get_index()][0], ygg::Index<Object>(3));

    auto child = factory.create_shared(5, &root);
    const auto [inherited_view, inherited_created] = child->insert(data);
    const auto [child_view, child_created] = child->insert(wider_data);

    EXPECT_FALSE(inherited_created);
    EXPECT_EQ(inherited_view.get_index().row, root_view.get_index().row);
    EXPECT_EQ(&inherited_view.get_context(), &root);
    EXPECT_TRUE(child_created);
    EXPECT_EQ(child_view.get_index().row, ygg::Index<ygg::formalism::Row>(1));
    EXPECT_EQ((*child)[child_view.get_index()][0], ygg::Index<Object>(4));
    EXPECT_EQ((*child)[child_view.get_index()][1], ygg::Index<Object>(0));
}

TEST(YggdrasilTests, CommonRelationRepositoryValidatesObjectIndexWidth)
{
    using Config = ygg::formalism::RelationRepositoryConfig;
    using Repository = ygg::formalism::RelationRepository<RepositoryTypesPackedObjectTag, RepositoryTypesRelation>;
    using SymbolRepository = ygg::formalism::SymbolRepository<RepositoryTypesElement>;
    using Factory = ygg::formalism::RepositoryFactory<SymbolRepository, Repository>;

    EXPECT_EQ(Config().object_index_width, Config::default_object_index_width);
    EXPECT_THROW((Config(0)), std::invalid_argument);
    EXPECT_THROW((Config(static_cast<std::uint8_t>(Config::default_object_index_width + 1))), std::invalid_argument);

    auto parent = Repository(0, nullptr, Config(2));
    EXPECT_THROW((Repository(1, &parent, Config(1))), std::invalid_argument);

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

TEST(YggdrasilTests, CommonRelationBindingRangeViewsExposeRows)
{
    using Binding = ygg::formalism::RelationBinding<RepositoryTypesRelation, RepositoryTypesObjectTag>;

    const auto relation = ygg::Index<RepositoryTypesRelation>(4);
    using Rows = std::vector<ygg::Index<ygg::formalism::Row>>;

    const auto rows = Rows { ygg::Index<ygg::formalism::Row>(2), ygg::Index<ygg::formalism::Row>(3) };
    const auto context = RepositoryTypesContext();
    const auto binding_index = ygg::Index<Binding> { relation, rows.front() };
    const auto binding_view = ygg::View<ygg::Index<Binding>, RepositoryTypesContext>(binding_index, context);
    EXPECT_TRUE(binding_view.get_data().empty());
    EXPECT_EQ(binding_view.get_relation().get_index(), relation);
    EXPECT_EQ(std::get<1>(binding_view.identifying_members()), 0);

    using ForwardRange = ygg::formalism::RelationBindingsForwardRange<RepositoryTypesRelation, RepositoryTypesObjectTag, Rows>;
    const auto forward_range = ForwardRange { relation, rows };
    const auto forward_view = ygg::View<ForwardRange, RepositoryTypesContext>(forward_range, context);
    EXPECT_FALSE(forward_view.empty());
    EXPECT_EQ(forward_view.size(), 2);
    static_assert(!noexcept(forward_view.front()));
    EXPECT_EQ(forward_view.front().get_index().relation, relation);
    EXPECT_EQ(forward_view.front().get_index().row, rows.front());

    const auto empty_rows = Rows {};
    const auto empty_forward_range = ForwardRange { relation, empty_rows };
    const auto empty_forward_view = ygg::View<ForwardRange, RepositoryTypesContext>(empty_forward_range, context);
    EXPECT_TRUE(empty_forward_view.empty());
    EXPECT_EQ(empty_forward_view.size(), 0);
    EXPECT_THROW(empty_forward_view.front(), std::out_of_range);

    using RandomAccessRange = ygg::formalism::RelationBindingsRandomAccessRange<RepositoryTypesRelation, RepositoryTypesObjectTag, Rows>;
    const auto random_access_range = RandomAccessRange { relation, rows };
    const auto random_access_view = ygg::View<RandomAccessRange, RepositoryTypesContext>(random_access_range, context);
    EXPECT_EQ(random_access_view.size(), 2);
    static_assert(!noexcept(random_access_view.front()));
    static_assert(!noexcept(random_access_view.back()));
    EXPECT_EQ(random_access_view.front().get_index().row, rows.front());
    EXPECT_EQ(random_access_view.back().get_index().row, rows.back());
    EXPECT_EQ(random_access_view[1].get_index().relation, relation);
    EXPECT_EQ(random_access_view[1].get_index().row, rows[1]);

    auto it = random_access_view.begin();
    EXPECT_EQ((*(it + 1)).get_index().row, rows[1]);
    EXPECT_EQ(random_access_view.end() - random_access_view.begin(), 2);

    const auto empty_random_access_range = RandomAccessRange { relation, empty_rows };
    const auto empty_random_access_view = ygg::View<RandomAccessRange, RepositoryTypesContext>(empty_random_access_range, context);
    EXPECT_TRUE(empty_random_access_view.empty());
    EXPECT_EQ(empty_random_access_view.size(), 0);
    EXPECT_THROW(empty_random_access_view.front(), std::out_of_range);
    EXPECT_THROW(empty_random_access_view.back(), std::out_of_range);
}

TEST(YggdrasilTests, CommonRelationBindingIteratorsPreserveUnderlyingTraversal)
{
    using Row = Index<formalism::Row>;
    using ForwardRows = std::forward_list<Row>;
    using BidirectionalRows = std::list<Row>;
    using RandomAccessRows = std::vector<Row>;
    using Forward = formalism::RelationBindingsForwardRange<RepositoryTypesRelation, RepositoryTypesObjectTag, ForwardRows>;
    using Bidirectional = formalism::RelationBindingsForwardRange<RepositoryTypesRelation, RepositoryTypesObjectTag, BidirectionalRows>;
    using RandomAccess = formalism::RelationBindingsForwardRange<RepositoryTypesRelation, RepositoryTypesObjectTag, RandomAccessRows>;
    using ExplicitRandomAccess = formalism::RelationBindingsRandomAccessRange<RepositoryTypesRelation, RepositoryTypesObjectTag, RandomAccessRows>;
    using ForwardView = View<Forward, RepositoryTypesContext>;
    using BidirectionalView = View<Bidirectional, RepositoryTypesContext>;
    using RandomAccessView = View<RandomAccess, RepositoryTypesContext>;
    using ExplicitRandomAccessView = View<ExplicitRandomAccess, RepositoryTypesContext>;
    static_assert(std::ranges::forward_range<ForwardView>);
    static_assert(!std::ranges::bidirectional_range<ForwardView>);
    static_assert(std::ranges::bidirectional_range<BidirectionalView>);
    static_assert(!std::ranges::random_access_range<BidirectionalView>);
    static_assert(std::ranges::random_access_range<RandomAccessView>);
    static_assert(std::ranges::random_access_range<ExplicitRandomAccessView>);
    static_assert(std::same_as<RandomAccessView::const_iterator, ExplicitRandomAccessView::const_iterator>);
    static_assert(!std::ranges::contiguous_range<RandomAccessView>);

    const auto relation = Index<RepositoryTypesRelation>(4);
    const auto context = RepositoryTypesContext {};
    const auto check = [&](auto view)
    {
        using V = decltype(view);
        using Iterator = std::ranges::iterator_t<V>;
        static_assert(std::same_as<typename std::iterator_traits<Iterator>::reference, typename Iterator::value_type>);
        static_assert(std::same_as<typename std::iterator_traits<Iterator>::pointer, void>);
        EXPECT_EQ(Iterator {}, Iterator {});
        // Iterators borrow the backing rows/context, not the temporary wrapper.
        const auto begin = V(view).begin();
        const auto end = V(view).end();
        EXPECT_EQ(std::ranges::distance(begin, end), 3);
        auto it = begin;
        const auto copy = it;
        EXPECT_EQ(it++, copy);
        EXPECT_EQ((*copy).get_index().relation, relation);
        EXPECT_EQ((*copy).get_index().row, Row(2));
        EXPECT_EQ((*it).get_index().row, Row(3));
        EXPECT_EQ((*++it).get_index().row, Row(4));
        EXPECT_EQ(++it, end);
        // This fixture returns nullary rows for each binding.
        EXPECT_TRUE((*copy).get_data().empty());
        if constexpr (std::bidirectional_iterator<Iterator>)
        {
            EXPECT_EQ(it--, end);
            EXPECT_EQ((*it).get_index().row, Row(4));
            EXPECT_EQ((*--it).get_index().row, Row(3));
        }
        if constexpr (std::random_access_iterator<Iterator>)
        {
            EXPECT_EQ(end - begin, 3);
            EXPECT_EQ(begin - end, -3);
            EXPECT_LT(begin, end);
            EXPECT_LE(begin, begin);
            EXPECT_GT(end, begin);
            EXPECT_GE(end, end);
            EXPECT_NE(begin, end);
            it = begin;
            it += 3;
            EXPECT_EQ(it, end);
            it += -2;
            EXPECT_EQ(it, 1 + begin);
            it -= -1;
            EXPECT_EQ(it, end - 1);
            it -= 2;
            EXPECT_EQ(it, begin);
            EXPECT_EQ(end[-1].get_index().row, Row(4));
            EXPECT_EQ((begin + 1)[-1].get_index().row, Row(2));
        }
    };
    const auto forward_rows = ForwardRows { Row(2), Row(3), Row(4) };
    const auto bidirectional_rows = BidirectionalRows { Row(2), Row(3), Row(4) };
    const auto random_access_rows = RandomAccessRows { Row(2), Row(3), Row(4) };
    check(ForwardView(Forward { relation, forward_rows }, context));
    check(BidirectionalView(Bidirectional { relation, bidirectional_rows }, context));
    check(RandomAccessView(RandomAccess { relation, random_access_rows }, context));
    check(ExplicitRandomAccessView(ExplicitRandomAccess { relation, random_access_rows }, context));

    const auto check_empty = [](auto view)
    {
        EXPECT_TRUE(view.empty());
        EXPECT_EQ(view.begin(), view.end());
        EXPECT_EQ(std::ranges::distance(view), 0);
        EXPECT_THROW(view.front(), std::out_of_range);
    };
    const auto empty_forward = ForwardRows {};
    const auto empty_bidirectional = BidirectionalRows {};
    const auto empty_random_access = RandomAccessRows {};
    check_empty(ForwardView(Forward { relation, empty_forward }, context));
    check_empty(BidirectionalView(Bidirectional { relation, empty_bidirectional }, context));
    check_empty(RandomAccessView(RandomAccess { relation, empty_random_access }, context));
    check_empty(ExplicitRandomAccessView(ExplicitRandomAccess { relation, empty_random_access }, context));
}

TEST(YggdrasilTests, CommonRepositoryThrowsForMissingSymbolIndices)
{
    using SymbolRepo = ygg::formalism::SymbolRepository<RepositoryTypesElement>;
    using RelationRepo = ygg::formalism::RelationRepository<RepositoryTypesObjectTag, RepositoryTypesRelation>;
    using Repository = ygg::formalism::Repository<SymbolRepo, RelationRepo>;

    auto repository = Repository(0);

    EXPECT_THROW(repository[ygg::Index<RepositoryTypesElement>(0)], std::out_of_range);
    EXPECT_THROW(repository.front<RepositoryTypesElement>(), std::out_of_range);
    EXPECT_THROW(repository.get_canonical_context(ygg::Index<RepositoryTypesElement>(0)), std::out_of_range);
    EXPECT_THROW(ygg::make_view(ygg::Index<RepositoryTypesElement>(0), repository), std::out_of_range);

    auto data = ygg::Data<RepositoryTypesElement> {};
    data.value = 7;
    const auto [view, created] = repository.insert(data);

    EXPECT_TRUE(created);
    EXPECT_EQ(repository[view.get_index()].value, 7);
    EXPECT_EQ(repository.front<RepositoryTypesElement>().value, 7);
    EXPECT_EQ(&repository.get_canonical_context(view.get_index()), &repository);
    EXPECT_THROW(repository[ygg::Index<RepositoryTypesElement>(1)], std::out_of_range);
    EXPECT_THROW(ygg::make_view(ygg::Index<RepositoryTypesElement>(1), repository), std::out_of_range);
}

TEST(YggdrasilTests, CommonRepositoryThrowsForMissingRelationBindingIndices)
{
    using Binding = ygg::formalism::RelationBinding<RepositoryTypesRelation, RepositoryTypesObjectTag>;
    using Object = ygg::formalism::Object<RepositoryTypesObjectTag>;
    using SymbolRepo = ygg::formalism::SymbolRepository<RepositoryTypesElement>;
    using RelationRepo = ygg::formalism::RelationRepository<RepositoryTypesObjectTag, RepositoryTypesRelation>;
    using Repository = ygg::formalism::Repository<SymbolRepo, RelationRepo>;

    auto repository = Repository(0);
    const auto relation = ygg::Index<RepositoryTypesRelation>(0);
    const auto missing = ygg::Index<Binding> { relation, ygg::Index<ygg::formalism::Row>(0) };

    EXPECT_THROW(repository[missing], std::out_of_range);
    EXPECT_THROW(repository.front(relation), std::out_of_range);
    EXPECT_THROW(repository.get_canonical_context(missing), std::out_of_range);
    EXPECT_THROW(ygg::make_view(missing, repository), std::out_of_range);

    auto objects = ygg::IndexList<Object> {};
    objects.push_back(ygg::Index<Object>(0));
    objects.push_back(ygg::Index<Object>(1));
    const auto data = ygg::Data<Binding>(relation, 2, objects);
    const auto [view, created] = repository.insert(data);

    EXPECT_TRUE(created);
    EXPECT_EQ(repository[view.get_index()].size(), 2);
    EXPECT_EQ(repository.front(relation).size(), 2);
    EXPECT_EQ(&repository.get_canonical_context(view.get_index()), &repository);

    const auto missing_row = ygg::Index<Binding> { relation, ygg::Index<ygg::formalism::Row>(1) };
    const auto missing_relation = ygg::Index<Binding> { ygg::Index<RepositoryTypesRelation>(1), ygg::Index<ygg::formalism::Row>(0) };
    EXPECT_THROW(repository[missing_row], std::out_of_range);
    EXPECT_THROW(ygg::make_view(missing_row, repository), std::out_of_range);
    EXPECT_THROW(repository[missing_relation], std::out_of_range);
    EXPECT_THROW(ygg::make_view(missing_relation, repository), std::out_of_range);
}

template<typename Repository>
Repository make_membership_repository(size_t index, const Repository* parent = nullptr)
{
    if constexpr (std::constructible_from<Repository, size_t, const Repository*>)
        return Repository(index, parent);
    else
        return Repository(parent);
}

template<typename Repository>
void check_symbol_membership()
{
    using Handle = Index<RepositoryTypesElement>;
    using ElementView = View<Handle, Repository>;
    auto root = make_membership_repository<Repository>(0);
    auto data = Data<RepositoryTypesElement> {};
    data.value = 7;
    const auto root_view = root.insert(data).first;
    auto child = make_membership_repository<Repository>(1, &root);
    data.value = 11;
    const auto child_view = child.insert(data).first;
    auto grandchild = make_membership_repository<Repository>(2, &child);
    data.value = 13;
    const auto local_view = grandchild.insert(data).first;

    EXPECT_TRUE(grandchild.contains(root_view));
    EXPECT_TRUE(formalism::contains(grandchild, root_view));
    EXPECT_TRUE(grandchild.contains(child_view));
    EXPECT_TRUE(grandchild.contains(local_view));
    EXPECT_FALSE(root.contains(child_view));
    EXPECT_FALSE(child.contains(local_view));
    EXPECT_TRUE(root.contains(ElementView(root_view.get_index(), grandchild)));
    EXPECT_FALSE(grandchild.contains(ElementView(local_view.get_index(), root)));

    const auto indices = std::array { root_view.get_index(), child_view.get_index(), local_view.get_index() };
    EXPECT_TRUE(formalism::contains_all(grandchild, indices));
    EXPECT_FALSE(formalism::contains_all(child, indices));
    EXPECT_TRUE(formalism::contains_all(grandchild, make_view(std::span<const Handle>(indices), grandchild)));
    EXPECT_TRUE(formalism::contains_all(root, std::span<const Handle> {}));
    EXPECT_TRUE(formalism::contains_all(root, std::span<const ElementView> {}));
    EXPECT_FALSE(formalism::contains_all(grandchild, std::array { root_view.get_index(), Handle::max() }));
    EXPECT_FALSE(grandchild.contains(Handle(3)));
    EXPECT_FALSE(grandchild.contains(ElementView(Handle::max(), grandchild)));

    auto unrelated = make_membership_repository<Repository>(0);
    EXPECT_FALSE(grandchild.contains(ElementView(root_view.get_index(), unrelated)));
    data.value = 7;
    const auto unrelated_view = unrelated.insert(data).first;
    ASSERT_EQ(unrelated_view.get_index(), root_view.get_index());
    EXPECT_TRUE(grandchild.contains(unrelated_view.get_index()));
    EXPECT_FALSE(grandchild.contains(unrelated_view));
    EXPECT_FALSE(formalism::contains(grandchild, unrelated_view));
    EXPECT_FALSE(formalism::contains_all(grandchild, std::array { root_view, unrelated_view }));
}

TEST(YggdrasilTests, CommonSymbolMembershipChecksCanonicalOwnersAcrossLayers)
{
    check_symbol_membership<ContractSymbols>();
    check_symbol_membership<ContractConcurrentSymbols>();
    check_symbol_membership<ContractRepository>();
    check_symbol_membership<formalism::Repository<ContractConcurrentSymbols, ContractConcurrentRelations>>();
}

template<typename Repository>
void check_relation_membership()
{
    using Handle = Index<ContractBinding>;
    using BindingView = View<Handle, Repository>;
    using Object = formalism::Object<RepositoryTypesObjectTag>;
    const auto relation = Index<RepositoryTypesRelation>(0);
    auto objects = IndexList<Object> {};
    objects.push_back(Index<Object>(0));
    auto root = Repository(0);
    const auto root_view = root.insert(Data<ContractBinding>(relation, 1, objects)).first;
    auto child = Repository(1, &root);
    objects[0] = Index<Object>(1);
    const auto child_view = child.insert(Data<ContractBinding>(relation, 1, objects)).first;
    auto grandchild = Repository(2, &child);
    objects[0] = Index<Object>(2);
    const auto local_view = grandchild.insert(Data<ContractBinding>(relation, 1, objects)).first;

    EXPECT_TRUE(grandchild.contains(root_view));
    EXPECT_TRUE(formalism::contains(grandchild, root_view));
    EXPECT_TRUE(grandchild.contains(child_view));
    EXPECT_TRUE(grandchild.contains(local_view));
    EXPECT_FALSE(root.contains(child_view));
    EXPECT_FALSE(child.contains(local_view));
    EXPECT_TRUE(root.contains(BindingView(root_view.get_index(), grandchild)));
    EXPECT_FALSE(grandchild.contains(BindingView(local_view.get_index(), root)));
    EXPECT_TRUE(formalism::contains_all(grandchild, std::array { root_view, child_view, local_view }));
    EXPECT_TRUE(formalism::contains_all(grandchild, std::array { root_view.get_index(), child_view.get_index(), local_view.get_index() }));
    EXPECT_TRUE(formalism::contains_all(root, std::span<const Handle> {}));
    EXPECT_TRUE(formalism::contains_all(root, std::span<const BindingView> {}));

    const auto missing_row = Handle { relation, Index<formalism::Row>(3) };
    const auto missing_relation = Handle { Index<RepositoryTypesRelation>(1), Index<formalism::Row>(0) };
    const auto max_row = Handle { relation, Index<formalism::Row>::max() };
    const auto max_relation = Handle { Index<RepositoryTypesRelation>::max(), Index<formalism::Row>(0) };
    for (const auto index : { missing_row, missing_relation, max_row, max_relation })
    {
        EXPECT_FALSE(grandchild.contains(index));
        EXPECT_FALSE(grandchild.contains(BindingView(index, grandchild)));
    }
    EXPECT_FALSE(formalism::contains_all(grandchild, std::array { root_view.get_index(), max_row }));

    auto unrelated = Repository(0);
    EXPECT_FALSE(grandchild.contains(BindingView(root_view.get_index(), unrelated)));
    objects[0] = Index<Object>(0);
    const auto unrelated_view = unrelated.insert(Data<ContractBinding>(relation, 1, objects)).first;
    EXPECT_TRUE(grandchild.contains(unrelated_view.get_index()));
    EXPECT_FALSE(grandchild.contains(unrelated_view));
    EXPECT_FALSE(formalism::contains(grandchild, unrelated_view));
}

TEST(YggdrasilTests, CommonRelationMembershipChecksCanonicalOwnersAcrossLayers)
{
    check_relation_membership<ContractRelations>();
    check_relation_membership<ContractConcurrentRelations>();
    check_relation_membership<ContractRepository>();
    check_relation_membership<formalism::Repository<ContractConcurrentSymbols, ContractConcurrentRelations>>();
}

}  // namespace ygg::tests
