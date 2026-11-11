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

#include "../formalism/symbol_concepts.hpp"
#include "yggdrasil/core/config.hpp"
#include "yggdrasil/formalism/builder.hpp"
#include "yggdrasil/formalism/interning.hpp"
#include "yggdrasil/formalism/membership.hpp"
#include "yggdrasil/formalism/relation_repository.hpp"
#include "yggdrasil/formalism/repository.hpp"
#include "yggdrasil/formalism/row_data.hpp"
#include "yggdrasil/formalism/row_view.hpp"
#include "yggdrasil/formalism/symbol_repository.hpp"
#include "yggdrasil/semantics/canonicalization.hpp"

#include "gtest/gtest.h"
#include <algorithm>
#include <array>
#include <boost/mp11.hpp>
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
#include <urd/config.hpp>
#include <urd/elements/equal_to.hpp>
#include <urd/elements/flat.hpp>
#include <urd/elements/hash.hpp>
#include <urd/repr/sequences/sequence.hpp>
#include <urd/store.hpp>
#include <vector>

namespace ygg::tests
{

/// Whether the context makes views of the handle.
template<typename H, typename C>
concept Viewable = requires(const C& context, H handle) { context.view(handle); };

struct RepositoryTypesObjectTag;
struct RepositoryTypesPackedObjectTag;
struct RepositoryTypesSerializedElement;
struct RepositoryTypesBuilderOther;
namespace
{
struct RepositoryTypesElement
{
    using object_tag = RepositoryTypesObjectTag;
};
struct RepositoryTypesRelation
{
    using object_tag = RepositoryTypesObjectTag;
};
/// A family whose rows are objects of the packed tag.
struct RepositoryTypesPackedRelation
{
    using object_tag = RepositoryTypesPackedObjectTag;
};
}
struct RepositoryTypesCheckout;

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

/// A key of row pools gives its rows' column types (here: `value` object columns).
template<typename V>
struct view_mixin<ygg::tests::RepositoryTypesRelation, V>
{
    [[nodiscard]] auto get_column_types() const
    {
        return std::views::iota(ygg::uint_t { 0 }, static_cast<const V&>(*this).data().value)
               | std::views::transform([](ygg::uint_t) { return ygg::type_id<urd::index<ygg::formalism::Object<ygg::tests::RepositoryTypesObjectTag>>>; });
    }
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
/// An object column's type, as declared pools of raw keys (no symbols) take it.
constexpr auto object_column = ygg::type_id<urd::index<formalism::Object<RepositoryTypesObjectTag>>>;

TEST(YggdrasilTests, CommonRepositoryTypesUmbrellaHeaderCompiles) { SUCCEED(); }

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
concept CanUseAnySymbolOperation = requires(Repository& repository, urd::data<T>& data) { repository.insert(data); }
                                   || requires(const Repository& repository, const urd::data<T>& data) { repository.find(data); }
                                   || requires(const Repository& repository, urd::index<T> index) { repository[index]; }
                                   || requires(const Repository& repository, urd::index<T> index) { repository.owner(index); }
                                   || requires(const Repository& repository, urd::index<T> index) { repository.contains(index); }
                                   || requires(const Repository& repository) { repository.template size<T>(); }
                                   || requires(const Repository& repository) { repository.template memory_usage<T>(); };

template<typename Repository, typename T>
concept CanUseAnyLocalSymbolOperation =
    requires(Repository& repository) { repository.template get<T>(); } || requires(const Repository& repository) { repository.template get<T>(); };

using ContractSymbols = formalism::SymbolRepository<RepositoryTypesElement>;
using ContractConcurrentSymbols = formalism::ConcurrentSymbolRepository<RepositoryTypesElement>;
using ContractRepository = formalism::Repository<ContractSymbols, formalism::RelationRepository<RepositoryTypesObjectTag, RepositoryTypesRelation>>;
using ContractBinding = Row<RepositoryTypesRelation>;
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
static_assert(formalism::HasRows<ContractRelations, RepositoryTypesRelation>);
static_assert(formalism::HasRows<ContractRepository, RepositoryTypesRelation>);
static_assert(!formalism::HasRelations<ContractRepository, RepositoryTypesRelation>);
static_assert(!formalism::HasRows<ContractRelations, RepositoryTypesElement>);
static_assert(!formalism::HasRows<int, RepositoryTypesRelation>);
static_assert(!SymbolContextFor<ContractSymbols, RepositoryTypesElement>);
static_assert(SymbolContextFor<ContractRepository, RepositoryTypesElement>);
static_assert(!SymbolContextFor<ContractSymbols, RepositoryTypesSerializedElement>);
static_assert(!SymbolRepositoryFor<ContractRepository, ContractBinding>);
static_assert(std::same_as<formalism::BuilderStorage<RepositoryTypesCheckout>::Types, boost::mp11::mp_list<RepositoryTypesCheckout>>);

template<typename Repository, typename T>
concept CanUseUnsupportedRelation = requires(Repository& repository, urd::data<Row<T>>& data) { repository.insert(data); }
                                    || requires(const Repository& repository, urd::data<Row<T>>& data) { repository.find(data); }
                                    || requires(const Repository& repository, RowIndex<T> index) { repository[index]; }
                                    || requires(const Repository& repository, RowIndex<T> index) { repository.owner(index); }
                                    || requires(const Repository& repository, urd::index<T> index) { repository.size(index); }
                                    || requires(Repository& repository) { repository.template get<T>(); };
static_assert(!CanUseUnsupportedRelation<ContractRelations, RepositoryTypesElement>);
static_assert(!CanUseUnsupportedRelation<ContractConcurrentRelations, RepositoryTypesElement>);
static_assert(!CanUseUnsupportedRelation<ContractRepository, RepositoryTypesElement>);
static_assert(!Viewable<RowIndex<RepositoryTypesElement>, ContractRepository>);

TEST(YggdrasilTests, CommonIndexedViewPreservesLookupHandleAndOwner)
{
    ContractRepository repository;
    urd::data<RepositoryTypesElement> data;
    data.value = 31;
    const auto published = repository.insert(data).first;
    const auto view = urd::index_view<RepositoryTypesElement, ContractRepository>(published.index(), &repository);
    EXPECT_EQ(view.index(), published.index());
    EXPECT_EQ(&view.context(), &repository);
    EXPECT_EQ(&view.data(), &published.data());
    EXPECT_EQ(std::get<1>(urd::detail::identity(view)), repository.layer());
}

template<typename Relations>
static void expect_row_access_for_each_relation_type()
{
    using ObjectIndex = urd::index<formalism::Object<RepositoryTypesObjectTag>>;
    using Repository = formalism::Repository<ContractConcurrentSymbols, Relations>;
    static_assert(Viewable<RowIndex<RepositoryTypesRelation>, Repository>);
    static_assert(Viewable<RowIndex<RepositoryTypesElement>, Repository>);

    Repository repository;
    repository.declare(urd::index<RepositoryTypesRelation>(0), std::array { object_column });  // raw keys, no symbols: declared here
    repository.declare(urd::index<RepositoryTypesElement>(0), std::array { object_column, object_column });
    urd::data<Row<RepositoryTypesRelation>> first_data;
    first_data.relation = urd::index<RepositoryTypesRelation>(0);
    first_data.values.push_back(ygg::encode_value(ObjectIndex(3)));
    urd::data<Row<RepositoryTypesElement>> second_data;
    second_data.relation = urd::index<RepositoryTypesElement>(0);
    second_data.values.push_back(ygg::encode_value(ObjectIndex(7)));
    second_data.values.push_back(ygg::encode_value(ObjectIndex(9)));
    const auto first = repository.insert(first_data).first;
    const auto second = repository.insert(second_data).first;
    EXPECT_EQ(first.size(), 1);
    EXPECT_EQ(first.get_key().second[0], 3U);
    EXPECT_EQ(second.size(), 2);
    EXPECT_EQ(second.get_key().second[0], 7U);
    EXPECT_EQ(second.get_key().second[1], 9U);
    EXPECT_EQ(second.value(1), 9);
    EXPECT_EQ(second.type(1), ygg::type_id<ObjectIndex>);
    EXPECT_EQ(std::get<urd::index<formalism::Object<void>>>(second.get(1)).value, 9U);
    EXPECT_EQ(second.template get<ObjectIndex>(1), ObjectIndex(9));
    EXPECT_EQ(second.template get<ObjectIndex>()[0], ObjectIndex(7));
    EXPECT_THROW((void) second.template get<double>(1), std::invalid_argument);
}

TEST(YggdrasilTests, CommonRelationAccessUsesDecodedObjectTypeForEveryRelation)
{
    expect_row_access_for_each_relation_type<formalism::RelationRepository<RepositoryTypesObjectTag, RepositoryTypesRelation, RepositoryTypesElement>>();
    expect_row_access_for_each_relation_type<
        formalism::ConcurrentRelationRepository<RepositoryTypesObjectTag, RepositoryTypesRelation, RepositoryTypesElement>>();
    expect_row_access_for_each_relation_type<formalism::RelationRepository<RepositoryTypesPackedObjectTag, RepositoryTypesRelation, RepositoryTypesElement>>();
    expect_row_access_for_each_relation_type<
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

TEST(YggdrasilTests, CommonInterningAcceptsRowsWithoutSymbolIdentity)
{
    auto repository = ContractRepository();
    repository.declare(urd::index<RepositoryTypesRelation>(0), std::array { object_column });
    using Object = formalism::Object<RepositoryTypesObjectTag>;
    auto objects = urd::flat_vector<ygg::Value>();
    objects.push_back(ygg::encode_value(urd::index<Object>(3)));
    auto data = urd::data<ContractBinding>(urd::index<RepositoryTypesRelation>(0), std::move(objects));
    const auto [first, created] = formalism::insert(repository, data);
    EXPECT_TRUE(created);
    const auto [duplicate, duplicate_created] = formalism::insert(repository, data);
    EXPECT_FALSE(duplicate_created);
    EXPECT_EQ(duplicate.data(), first.data());
    EXPECT_EQ(first.template get<urd::index<Object>>(0), urd::index<Object>(3));
}

TEST(YggdrasilTests, CommonRepositoryLayersHaveDistinctNumbersAndParents)
{
    using SymbolRepo = ygg::formalism::SymbolRepository<RepositoryTypesElement>;
    using RelationRepo = ygg::formalism::RelationRepository<RepositoryTypesObjectTag, RepositoryTypesRelation>;
    using Repository = ygg::formalism::Repository<SymbolRepo, RelationRepo>;

    auto root = Repository();
    auto child = Repository(&root);
    auto shared = std::make_shared<Repository>(&child);

    EXPECT_LT(root.layer(), child.layer());
    EXPECT_LT(child.layer(), shared->layer());
    EXPECT_EQ(root.parent(), nullptr);
    EXPECT_EQ(child.parent(), &root);
    EXPECT_EQ(shared->parent(), &child);
}

TEST(YggdrasilTests, CommonRowViewIdentityUsesTheOwningLayer)
{
    using Object = ygg::formalism::Object<RepositoryTypesObjectTag>;
    using Binding = Row<RepositoryTypesRelation>;
    using SymbolRepo = ygg::formalism::SymbolRepository<RepositoryTypesElement>;
    using RelationRepo = ygg::formalism::RelationRepository<RepositoryTypesObjectTag, RepositoryTypesRelation>;
    using Repository = ygg::formalism::Repository<SymbolRepo, RelationRepo>;
    using StoredRowView = RowView<RepositoryTypesRelation, Repository>;

    auto first_repository = Repository();
    auto second_repository = Repository();
    auto independent_repository = Repository();

    auto objects = urd::flat_vector<ygg::Value> {};
    objects.push_back(ygg::encode_value(urd::index<Object>(0)));
    const auto data = urd::data<Binding>(urd::index<RepositoryTypesRelation>(0), objects);
    for (auto* each : { &first_repository, &second_repository, &independent_repository })
        each->declare(data.relation, std::array { object_column });

    const auto [first, first_created] = first_repository.insert(data);
    const auto [second, second_created] = second_repository.insert(data);
    const auto [independent, independent_created] = independent_repository.insert(data);

    ASSERT_TRUE(first_created);
    ASSERT_TRUE(second_created);
    ASSERT_TRUE(independent_created);
    EXPECT_EQ(first.data(), second.data());
    EXPECT_EQ(first.data(), independent.data());

    EXPECT_FALSE(urd::EqualTo<StoredRowView> {}(first, second));
    EXPECT_NE(urd::Hash<StoredRowView> {}(first), urd::Hash<StoredRowView> {}(second));
    EXPECT_FALSE(urd::EqualTo<StoredRowView> {}(first, independent));
    EXPECT_TRUE(urd::EqualTo<StoredRowView> {}(first, StoredRowView(first.data(), &first_repository)));
    EXPECT_TRUE(formalism::contains(first_repository, first));
    EXPECT_FALSE(formalism::contains(first_repository, second));
    EXPECT_FALSE(formalism::contains(first_repository, independent));
    EXPECT_NE(first_repository.owner(independent.data()), nullptr);
    EXPECT_FALSE(urd::Less<> {}(first.get_key(), second.get_key()));
    EXPECT_FALSE(urd::Less<> {}(second.get_key(), first.get_key()));

    objects[0] = ygg::encode_value(urd::index<Object>(1));
    const auto [larger, larger_created] = second_repository.insert(urd::data<Binding>(urd::index<RepositoryTypesRelation>(0), objects));
    ASSERT_TRUE(larger_created);
    EXPECT_TRUE(urd::Less<> {}(first.get_key(), larger.get_key()));
}

template<typename V>
concept HasPublishedRowIndex = requires(const V& view) { view.index(); };

using RowViewRepository =
    formalism::Repository<formalism::SymbolRepository<RepositoryTypesRelation, formalism::Object<RepositoryTypesObjectTag>>, ContractRelations>;
using BorrowedContractRowView = urd::borrowed_view<ContractBinding, RowViewRepository>;
using PublishedContractRowView = RowView<RepositoryTypesRelation, RowViewRepository>;
static_assert(RowViewConcept<BorrowedContractRowView, RepositoryTypesRelation>);
static_assert(RowViewConcept<PublishedContractRowView, RepositoryTypesRelation>);
static_assert(RowViewConcept<const PublishedContractRowView&, RepositoryTypesRelation>);
static_assert(RowViewConcept<BorrowedContractRowView&&, RepositoryTypesRelation>);

namespace
{
struct MutableRowView : PublishedContractRowView
{
    std::pair<urd::index<RepositoryTypesRelation>, std::span<const urd::index<formalism::Object<RepositoryTypesObjectTag>>>> get_key();
};
}  // namespace
static_assert(!RowViewConcept<MutableRowView&, RepositoryTypesRelation>);
static_assert(!RowViewConcept<BorrowedContractRowView, RepositoryTypesElement>);
static_assert(!RowViewConcept<int, RepositoryTypesRelation>);
static_assert(!HasPublishedRowIndex<BorrowedContractRowView>);
static_assert(!HasPublishedRowIndex<PublishedContractRowView>);
static_assert(std::same_as<decltype(std::declval<const PublishedContractRowView&>().data()), const RowIndex<RepositoryTypesRelation>&>);
static_assert(std::same_as<decltype(std::declval<const BorrowedContractRowView&>().data()), const urd::data<ContractBinding>&>);
static_assert(std::same_as<decltype(std::declval<const BorrowedContractRowView&>().data()), const urd::data<ContractBinding>&>);

TEST(YggdrasilTests, CommonBorrowedRowViewReadsWithoutPublishingAndMaterializesConstData)
{
    using Object = formalism::Object<RepositoryTypesObjectTag>;
    auto repository = RowViewRepository();
    auto relation_data = urd::data<RepositoryTypesRelation>();
    relation_data.value = 2;  // its rows' arity: interning it declares its pool
    const auto relation = repository.insert(relation_data).first.index();
    auto object_data = urd::data<Object>();
    for (uint_t i = 0; i < 8; ++i)
    {
        object_data.value = i;
        repository.insert(object_data);
    }
    auto data = urd::data<ContractBinding>();
    data.relation = relation;
    data.values.push_back(ygg::encode_value(urd::index<Object>(2)));
    data.values.push_back(ygg::encode_value(urd::index<Object>(5)));
    const auto borrowed = urd::resolve(data, repository);
    EXPECT_EQ(&borrowed.data(), &data);
    EXPECT_EQ(&borrowed.context(), &repository);
    EXPECT_EQ(borrowed.get_relation().index(), relation);
    EXPECT_EQ(borrowed.type(0), ygg::type_id<urd::index<Object>>);  // declared with its key, before any row
    EXPECT_EQ(borrowed.value(1), 5U);
    EXPECT_EQ(borrowed.get_key().first, relation);
    EXPECT_EQ(borrowed.get_key().second.data(), data.values.data());
    EXPECT_FALSE(repository.find(data));

    const auto [published, created] = repository.insert(borrowed.data());
    ASSERT_TRUE(created);
    EXPECT_EQ(&published.context(), &repository);
    EXPECT_TRUE(std::ranges::equal(published.get_key().second, borrowed.get_key().second));
    const auto [duplicate, duplicate_created] = repository.insert(borrowed.data());
    EXPECT_FALSE(duplicate_created);
    EXPECT_EQ(duplicate.data(), published.data());

    // A returned object range borrows Data directly, independently of the wrapper; its objects resolve.
    EXPECT_EQ(borrowed.template get<urd::index<Object>>()[1], urd::index<Object>(5));
    const auto objects = urd::resolve(urd::resolve(data, repository).template get<urd::index<Object>>(), repository);
    data.values[0] = ygg::encode_value(urd::index<Object>(7));
    EXPECT_EQ(objects[0].index(), urd::index<Object>(7));
    EXPECT_EQ(urd::resolve(published.template get<urd::index<Object>>(), repository)[0].index(), urd::index<Object>(2));
}

TEST(YggdrasilTests, CommonCompositeRepositorySynchronizesLocalAndInheritedScratchIdentity)
{
    auto root = ContractRepository();
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

    auto child = ContractRepository(&root);
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
    EXPECT_EQ(root.symbols().size<RepositoryTypesElement>(), 1);
    EXPECT_EQ(child.symbols().size<RepositoryTypesElement>(), 2);
}

TEST(YggdrasilTests, CommonRepositoryRejectsWrongArityWithoutMutation)
{
    using Object = ygg::formalism::Object<RepositoryTypesObjectTag>;
    using Binding = Row<RepositoryTypesRelation>;

    auto repository = ContractRepository();
    const auto relation = urd::index<RepositoryTypesRelation>(0);
    repository.declare(relation, std::array { object_column, object_column });
    auto objects = urd::flat_vector<ygg::Value> {};
    objects.push_back(ygg::encode_value(urd::index<Object>(0)));
    objects.push_back(ygg::encode_value(urd::index<Object>(1)));
    const auto data = urd::data<Binding>(relation, std::move(objects));
    const auto [view, created] = repository.insert(data);
    ASSERT_TRUE(created);

    auto wrong_objects = urd::flat_vector<ygg::Value> {};
    wrong_objects.push_back(ygg::encode_value(urd::index<Object>(0)));
    const auto wrong = urd::data<Binding>(relation, std::move(wrong_objects));
    EXPECT_FALSE(repository.find(wrong));
    EXPECT_THROW(repository.insert(wrong), std::invalid_argument);
    EXPECT_EQ(repository.relations().size(relation), 1);
    EXPECT_EQ(view.get_key().second.size(), 2);
}

TEST(YggdrasilTests, CommonRepositoryReportsOwnedMemory)
{
    using Object = ygg::formalism::Object<RepositoryTypesObjectTag>;
    using Binding = Row<RepositoryTypesRelation>;
    using SymbolRepository = ygg::formalism::SymbolRepository<RepositoryTypesElement>;
    using RelationRepository = ygg::formalism::RelationRepository<RepositoryTypesObjectTag, RepositoryTypesRelation>;
    using Repository = ygg::formalism::Repository<SymbolRepository, RelationRepository>;

    auto repository = Repository();
    EXPECT_EQ(repository.symbols().memory_usage<RepositoryTypesElement>(), 0);
    EXPECT_EQ(repository.relations().memory_usage<RepositoryTypesRelation>(), 0);

    auto element = urd::data<RepositoryTypesElement> {};
    element.value = 7;
    repository.insert(element);

    auto objects = urd::flat_vector<ygg::Value> {};
    objects.push_back(ygg::encode_value(urd::index<Object>(0)));
    objects.push_back(ygg::encode_value(urd::index<Object>(1)));
    const auto data = urd::data<Binding>(urd::index<RepositoryTypesRelation>(0), objects);
    repository.declare(data.relation, std::array { object_column, object_column });
    repository.insert(data);

    const auto symbol_memory_usage = repository.symbols().memory_usage<RepositoryTypesElement>();
    const auto relation_memory_usage = repository.relations().memory_usage<RepositoryTypesRelation>();
    EXPECT_GT(symbol_memory_usage, 0);
    EXPECT_GT(relation_memory_usage, 0);

    repository.symbols().clear();
    repository.relations().clear();
    EXPECT_EQ(repository.symbols().memory_usage<RepositoryTypesElement>(), symbol_memory_usage);
    EXPECT_LE(repository.relations().memory_usage<RepositoryTypesRelation>(), relation_memory_usage);

    auto child = Repository(&repository);
    EXPECT_EQ(child.symbols().memory_usage<RepositoryTypesElement>(), 0);
    EXPECT_EQ(child.relations().memory_usage<RepositoryTypesRelation>(), 0);
}

TEST(YggdrasilTests, CommonRelationRepositorySupportsBitPackedStorageTrait)
{
    using DefaultRelationRepository = ygg::formalism::RelationRepository<RepositoryTypesObjectTag, RepositoryTypesRelation>;
    using Object = ygg::formalism::Object<RepositoryTypesPackedObjectTag>;
    using Binding = Row<RepositoryTypesPackedRelation>;
    using SymbolRepository = ygg::formalism::SymbolRepository<RepositoryTypesElement>;
    using RelationRepository = ygg::formalism::RelationRepository<RepositoryTypesPackedObjectTag, RepositoryTypesPackedRelation>;
    using Repository = ygg::formalism::Repository<SymbolRepository, RelationRepository>;

    // Both store rows as object columns (urd::column_widths): whole (32 bits) by default, in the layer's object width packed.
    static_assert(std::constructible_from<typename DefaultRelationRepository::template Rows<RepositoryTypesRelation>, const urd::column_widths&>);
    static_assert(std::constructible_from<typename RelationRepository::template Rows<RepositoryTypesPackedRelation>, const urd::column_widths&>);

    auto root = Repository(4, nullptr);
    const auto relation = urd::index<RepositoryTypesPackedRelation>(0);
    root.declare(relation, std::array { object_column, object_column });
    auto objects = urd::flat_vector<ygg::Value> {};
    objects.push_back(ygg::encode_value(urd::index<Object>(0)));
    objects.push_back(ygg::encode_value(urd::index<Object>(3)));
    const auto data = urd::data<Binding>(relation, objects);

    EXPECT_EQ(root.relations().memory_usage<RepositoryTypesPackedRelation>(), 0);

    const auto [root_view, root_created] = root.insert(data);
    const auto [duplicate_view, duplicate_created] = root.insert(data);

    EXPECT_TRUE(root_created);
    EXPECT_FALSE(duplicate_created);
    EXPECT_GT(root.relations().memory_usage<RepositoryTypesPackedRelation>(), 0);
    EXPECT_EQ(duplicate_view.data().row, root_view.data().row);
    EXPECT_EQ(root_view.template get<urd::index<Object>>(0), urd::index<Object>(0));
    EXPECT_EQ(root_view.template get<urd::index<Object>>(1), urd::index<Object>(3));

    objects[0] = ygg::encode_value(urd::index<Object>(4));
    objects[1] = ygg::encode_value(urd::index<Object>(0));
    const auto wider_data = urd::data<Binding>(relation, objects);

    EXPECT_THROW(root.insert(wider_data), std::out_of_range);
    EXPECT_EQ(root.relations().size(relation), 1);

    const auto [root_duplicate_after_failure, created_after_failure] = root.insert(data);
    EXPECT_FALSE(created_after_failure);
    EXPECT_EQ(root_duplicate_after_failure.data().row, root_view.data().row);

    auto inherited_width_child = Repository(2, &root);
    objects[0] = ygg::encode_value(urd::index<Object>(3));
    const auto inherited_width_data = urd::data<Binding>(relation, objects);
    const auto [inherited_width_view, inherited_width_created] = inherited_width_child.insert(inherited_width_data);
    EXPECT_TRUE(inherited_width_created);
    EXPECT_EQ(inherited_width_view.template get<urd::index<Object>>(0), urd::index<Object>(3));

    auto child = std::make_shared<Repository>(5, &root);
    const auto [inherited_view, inherited_created] = child->insert(data);
    const auto [child_view, child_created] = child->insert(wider_data);

    EXPECT_FALSE(inherited_created);
    EXPECT_EQ(inherited_view.data().row, root_view.data().row);
    EXPECT_EQ(&inherited_view.context(), &root);
    EXPECT_TRUE(child_created);
    EXPECT_EQ(child_view.data().row, urd::index<Row<RepositoryTypesPackedRelation>>(1));
    EXPECT_EQ(child_view.template get<urd::index<Object>>(0), urd::index<Object>(4));
    EXPECT_EQ(child_view.template get<urd::index<Object>>(1), urd::index<Object>(0));
}

TEST(YggdrasilTests, CommonRelationRepositoryValidatesObjectIndexWidth)
{
    using Repository = ygg::formalism::RelationRepository<RepositoryTypesPackedObjectTag, RepositoryTypesRelation>;
    using SymbolRepository = ygg::formalism::SymbolRepository<RepositoryTypesElement>;
    using Layer = ygg::formalism::Repository<SymbolRepository, Repository>;
    constexpr auto full_width = ygg::formalism::default_object_index_width.bits;

    EXPECT_EQ(Repository().get_object_index_width(), full_width);
    EXPECT_THROW((Repository(nullptr, urd::bit_width { 0 })), std::invalid_argument);
    EXPECT_THROW((Repository(nullptr, urd::bit_width { static_cast<std::uint8_t>(full_width + 1) })), std::invalid_argument);

    const auto parent = Repository(nullptr, urd::bit_width { 2 });
    EXPECT_THROW((Repository(&parent, urd::bit_width { 1 })), std::invalid_argument);

    EXPECT_NO_THROW(Layer(size_t { 0 }, nullptr));
    EXPECT_NO_THROW(std::make_shared<Layer>(1, nullptr));
    const auto full_width_parent = Layer();
    EXPECT_NO_THROW(Layer(size_t { 2 }, &full_width_parent));
    if constexpr (std::numeric_limits<size_t>::digits > std::numeric_limits<ygg::uint_t>::digits)
    {
        const auto oversized_domain = static_cast<size_t>(std::numeric_limits<ygg::uint_t>::max()) + size_t { 1 };
        EXPECT_THROW(Layer(oversized_domain, nullptr), std::invalid_argument);
    }
}

TEST(YggdrasilTests, CommonRepositoryThrowsForMissingSymbolIndices)
{
    using SymbolRepo = ygg::formalism::SymbolRepository<RepositoryTypesElement>;
    using RelationRepo = ygg::formalism::RelationRepository<RepositoryTypesObjectTag, RepositoryTypesRelation>;
    using Repository = ygg::formalism::Repository<SymbolRepo, RelationRepo>;

    auto repository = Repository();

    EXPECT_THROW(repository[urd::index<RepositoryTypesElement>(0)], std::out_of_range);
    EXPECT_EQ(repository.owner(urd::index<RepositoryTypesElement>(0)), nullptr);
    EXPECT_THROW(repository.view(urd::index<RepositoryTypesElement>(0)), std::out_of_range);

    auto data = urd::data<RepositoryTypesElement> {};
    data.value = 7;
    const auto [view, created] = repository.insert(data);

    EXPECT_TRUE(created);
    EXPECT_EQ(repository[view.index()].value, 7);
    EXPECT_EQ(repository.owner(view.index()), &repository);
    EXPECT_THROW(repository[urd::index<RepositoryTypesElement>(1)], std::out_of_range);
    EXPECT_THROW(repository.view(urd::index<RepositoryTypesElement>(1)), std::out_of_range);
}

TEST(YggdrasilTests, CommonRepositoryThrowsForMissingRowIndices)
{
    using Binding = Row<RepositoryTypesRelation>;
    using Object = ygg::formalism::Object<RepositoryTypesObjectTag>;
    using SymbolRepo = ygg::formalism::SymbolRepository<RepositoryTypesElement>;
    using RelationRepo = ygg::formalism::RelationRepository<RepositoryTypesObjectTag, RepositoryTypesRelation>;
    using Repository = ygg::formalism::Repository<SymbolRepo, RelationRepo>;

    auto repository = Repository();
    const auto relation = urd::index<RepositoryTypesRelation>(0);
    repository.declare(relation, std::array { object_column, object_column });
    const auto missing = RowIndex<RepositoryTypesRelation> { relation, urd::index<Row<RepositoryTypesRelation>>(0) };

    EXPECT_THROW(repository[missing], std::out_of_range);
    EXPECT_EQ(repository.owner(missing), nullptr);
    EXPECT_THROW(urd::resolve(missing, repository), std::out_of_range);
    const auto rows = std::array { urd::index<Row<RepositoryTypesRelation>>(0) };
    const auto bindings = rows_view(relation, rows, repository);
    EXPECT_THROW((void) bindings[0], std::out_of_range);

    auto objects = urd::flat_vector<ygg::Value> {};
    objects.push_back(ygg::encode_value(urd::index<Object>(0)));
    objects.push_back(ygg::encode_value(urd::index<Object>(1)));
    const auto data = urd::data<Binding>(relation, objects);
    const auto [view, created] = repository.insert(data);

    EXPECT_TRUE(created);
    EXPECT_EQ(view.get_key().second.size(), 2);
    EXPECT_EQ(repository.owner(view.data()), &repository);

    const auto missing_row = RowIndex<RepositoryTypesRelation> { relation, urd::index<Row<RepositoryTypesRelation>>(1) };
    const auto missing_relation = RowIndex<RepositoryTypesRelation> { urd::index<RepositoryTypesRelation>(1), urd::index<Row<RepositoryTypesRelation>>(0) };
    EXPECT_THROW(repository[missing_row], std::out_of_range);
    EXPECT_THROW(urd::resolve(missing_row, repository), std::out_of_range);
    EXPECT_THROW(repository[missing_relation], std::out_of_range);
    EXPECT_THROW(urd::resolve(missing_relation, repository), std::out_of_range);
}

/// Whether the repository holds an index or binding key (some layer owns it), or the record a view denotes.
template<typename Repository, typename X>
static bool has(const Repository& repository, const X& x)
{
    if constexpr (requires { formalism::contains(repository, x); })
        return formalism::contains(repository, x);
    else
        return repository.owner(x) != nullptr;
}

template<typename Repository>
static void check_symbol_membership()
{
    using Handle = urd::index<RepositoryTypesElement>;
    using ElementView = urd::index_view<RepositoryTypesElement, Repository>;
    auto root = Repository();
    auto data = urd::data<RepositoryTypesElement> {};
    data.value = 7;
    const auto root_view = root.insert(data).first;
    auto child = Repository(&root);
    data.value = 11;
    const auto child_view = child.insert(data).first;
    auto grandchild = Repository(&child);
    data.value = 13;
    const auto local_view = grandchild.insert(data).first;

    EXPECT_TRUE(has(grandchild, root_view));
    EXPECT_TRUE(formalism::contains(grandchild, root_view));
    EXPECT_TRUE(has(grandchild, child_view));
    EXPECT_TRUE(has(grandchild, local_view));
    EXPECT_FALSE(has(root, child_view));
    EXPECT_FALSE(has(child, local_view));
    EXPECT_TRUE(has(root, ElementView(root_view.index(), &grandchild)));
    EXPECT_FALSE(has(grandchild, ElementView(local_view.index(), &root)));

    const auto indices = std::array { root_view.index(), child_view.index(), local_view.index() };
    EXPECT_TRUE(std::ranges::all_of(indices, [&](const auto& value) { return has(grandchild, value); }));
    EXPECT_FALSE(std::ranges::all_of(indices, [&](const auto& value) { return has(child, value); }));
    EXPECT_TRUE(std::ranges::all_of(urd::resolve(std::span<const Handle>(indices), grandchild), [&](const auto& value) { return has(grandchild, value); }));
    EXPECT_TRUE(std::ranges::all_of(std::span<const Handle> {}, [&](const auto& value) { return has(root, value); }));
    EXPECT_TRUE(std::ranges::all_of(std::span<const ElementView> {}, [&](const auto& value) { return has(root, value); }));
    EXPECT_FALSE(std::ranges::all_of(std::array { root_view.index(), Handle() }, [&](const auto& value) { return has(grandchild, value); }));
    EXPECT_FALSE(has(grandchild, Handle(3)));
    EXPECT_FALSE(has(grandchild, ElementView(Handle(), &grandchild)));

    auto unrelated = Repository();
    EXPECT_FALSE(has(grandchild, ElementView(root_view.index(), &unrelated)));
    data.value = 7;
    const auto unrelated_view = unrelated.insert(data).first;
    ASSERT_EQ(unrelated_view.index(), root_view.index());
    EXPECT_TRUE(has(grandchild, unrelated_view.index()));
    EXPECT_FALSE(has(grandchild, unrelated_view));
    EXPECT_FALSE(formalism::contains(grandchild, unrelated_view));
    EXPECT_FALSE(std::ranges::all_of(std::array { root_view, unrelated_view }, [&](const auto& value) { return has(grandchild, value); }));
}

TEST(YggdrasilTests, CommonSymbolMembershipChecksCanonicalOwnersAcrossLayers)
{
    check_symbol_membership<ContractRepository>();
    check_symbol_membership<formalism::Repository<ContractConcurrentSymbols, ContractConcurrentRelations>>();
}

template<typename Repository>
static void check_relation_membership()
{
    using Handle = RowIndex<RepositoryTypesRelation>;
    using StoredRowView = RowView<RepositoryTypesRelation, Repository>;
    using Object = formalism::Object<RepositoryTypesObjectTag>;
    const auto relation = urd::index<RepositoryTypesRelation>(0);
    auto objects = urd::flat_vector<ygg::Value> {};
    objects.push_back(ygg::encode_value(urd::index<Object>(0)));
    auto root = Repository();
    root.declare(relation, std::array { object_column });
    const auto root_view = root.insert(urd::data<ContractBinding>(relation, objects)).first;
    auto child = Repository(&root);
    objects[0] = ygg::encode_value(urd::index<Object>(1));
    const auto child_view = child.insert(urd::data<ContractBinding>(relation, objects)).first;
    auto grandchild = Repository(&child);
    objects[0] = ygg::encode_value(urd::index<Object>(2));
    const auto local_view = grandchild.insert(urd::data<ContractBinding>(relation, objects)).first;

    EXPECT_TRUE(has(grandchild, root_view));
    EXPECT_TRUE(formalism::contains(grandchild, root_view));
    EXPECT_TRUE(has(grandchild, child_view));
    EXPECT_TRUE(has(grandchild, local_view));
    EXPECT_FALSE(has(root, child_view));
    EXPECT_FALSE(has(child, local_view));
    EXPECT_TRUE(has(root, StoredRowView(root_view.data(), &grandchild)));
    EXPECT_FALSE(has(grandchild, StoredRowView(local_view.data(), &root)));
    EXPECT_TRUE(std::ranges::all_of(std::array { root_view, child_view, local_view }, [&](const auto& value) { return has(grandchild, value); }));
    EXPECT_TRUE(
        std::ranges::all_of(std::array { root_view.data(), child_view.data(), local_view.data() }, [&](const auto& value) { return has(grandchild, value); }));
    EXPECT_TRUE(std::ranges::all_of(std::span<const Handle> {}, [&](const auto& value) { return has(root, value); }));
    EXPECT_TRUE(std::ranges::all_of(std::span<const StoredRowView> {}, [&](const auto& value) { return has(root, value); }));

    const auto missing_row = Handle { relation, urd::index<Row<RepositoryTypesRelation>>(3) };
    const auto missing_relation = Handle { urd::index<RepositoryTypesRelation>(1), urd::index<Row<RepositoryTypesRelation>>(0) };
    const auto max_row = Handle { relation, urd::index<Row<RepositoryTypesRelation>>() };
    const auto max_relation = Handle { urd::index<RepositoryTypesRelation>(), urd::index<Row<RepositoryTypesRelation>>(0) };
    for (const auto index : { missing_row, missing_relation, max_row, max_relation })
    {
        EXPECT_FALSE(has(grandchild, index));
        EXPECT_FALSE(has(grandchild, StoredRowView(index, &grandchild)));
    }
    EXPECT_FALSE(std::ranges::all_of(std::array { root_view.data(), max_row }, [&](const auto& value) { return has(grandchild, value); }));

    auto unrelated = Repository();
    unrelated.declare(relation, std::array { object_column });
    EXPECT_FALSE(has(grandchild, StoredRowView(root_view.data(), &unrelated)));
    objects[0] = ygg::encode_value(urd::index<Object>(0));
    const auto unrelated_view = unrelated.insert(urd::data<ContractBinding>(relation, objects)).first;
    EXPECT_TRUE(has(grandchild, unrelated_view.data()));
    EXPECT_FALSE(has(grandchild, unrelated_view));
    EXPECT_FALSE(formalism::contains(grandchild, unrelated_view));
}

TEST(YggdrasilTests, CommonRelationMembershipChecksCanonicalOwnersAcrossLayers)
{
    check_relation_membership<ContractRepository>();
    check_relation_membership<formalism::Repository<ContractConcurrentSymbols, ContractConcurrentRelations>>();
}

}  // namespace ygg::tests
