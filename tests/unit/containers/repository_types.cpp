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

#include "gtest/gtest.h"
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <tuple>
#include <vector>
#include <yggdrasil/containers/repository_types.hpp>
#include <yggdrasil/formalism/binding_data.hpp>
#include <yggdrasil/formalism/binding_view.hpp>
#include <yggdrasil/formalism/builder.hpp>
#include <yggdrasil/formalism/relation_repository.hpp>
#include <yggdrasil/formalism/repository.hpp>
#include <yggdrasil/formalism/repository_factory.hpp>
#include <yggdrasil/formalism/symbol_repository.hpp>
#include <yggdrasil/ids/index_mixins.hpp>
#include <yggdrasil/semantics/equal_to.hpp>
#include <yggdrasil/semantics/hash.hpp>

namespace ygg::tests
{

struct RepositoryTypesElement;
struct RepositoryTypesSerializedElement;
struct RepositoryTypesRelation;
struct RepositoryTypesObjectTag;
struct RepositoryTypesPackedObjectTag;
struct RepositoryTypesBuilderOther;

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

    const auto [first, first_created] = first_repository.get_or_create(data);
    const auto [second, second_created] = second_repository.get_or_create(data);
    const auto [independent, independent_created] = independent_repository.get_or_create(data);

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
    EXPECT_EQ(ygg::Hash<BindingView> {}(first), ygg::Hash<BindingView> {}(independent));
    EXPECT_FALSE(ygg::Less<> {}(first.get_key(), second.get_key()));
    EXPECT_FALSE(ygg::Less<> {}(second.get_key(), first.get_key()));

    objects[0] = ygg::Index<Object>(1);
    const auto [larger, larger_created] = second_repository.get_or_create(ygg::Data<Binding>(ygg::Index<RepositoryTypesRelation>(0), 1, objects));
    ASSERT_TRUE(larger_created);
    EXPECT_TRUE(ygg::Less<> {}(first.get_key(), larger.get_key()));
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

TEST(YggdrasilTests, CommonBasicSymbolRepositoryFrontLocalIsChecked)
{
    auto repository = ygg::formalism::BasicSymbolRepository<RepositoryTypesElement>();

    EXPECT_THROW(repository.front_local(), std::out_of_range);
    EXPECT_EQ(repository.memory_usage(), 0);

    auto data = ygg::Data<RepositoryTypesElement> {};
    data.value = 7;
    const auto [index, created] = repository.get_or_create_local(data);

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

    const auto [index, created] = repository.get_or_create_local(data);
    EXPECT_TRUE(created);
    EXPECT_EQ(index, ygg::Index<Tag>(0));
    EXPECT_EQ(repository.front_local().values[0], ygg::Index<RepositoryTypesElement>(7));
    EXPECT_GT(repository.memory_usage(), 0);

    const auto [duplicate_index, duplicate_created] = repository.get_or_create_local(data);
    EXPECT_FALSE(duplicate_created);
    EXPECT_EQ(duplicate_index, index);

    auto moved = std::move(repository);
    EXPECT_EQ(moved.find_local(data), index);
    EXPECT_EQ(moved.front_local().values[0], ygg::Index<RepositoryTypesElement>(7));

    auto assigned = ygg::formalism::BasicSymbolRepository<Tag>();
    assigned = std::move(moved);
    EXPECT_EQ(assigned.find_local(data), index);

    assigned.clear();
    EXPECT_EQ(assigned.local_size(), 0);
    const auto [reused_index, reused_created] = assigned.get_or_create_local(data);
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
    const auto [root_index, root_created] = root.get_or_create_local(root_data);

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
    const auto [child_index, child_created] = child.get_or_create_local(child_data);

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

    const auto [view, created] = root.get_or_create(data);

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

    auto child = Repository(&root);
    const auto [child_view, child_created] = child.get_or_create(data);

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
    const auto [local_view, local_created] = child.get_or_create(child_data);

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
    const auto [row, created] = repository.get_or_create_local(data);

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

    const auto [view, created] = root.get_or_create(data);

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
    const auto [child_view, child_created] = child.get_or_create(data);

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
    const auto [local_view, local_created] = child.get_or_create(child_data);

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
    const auto [view, created] = repository.get_or_create(data);
    ASSERT_TRUE(created);

    auto wrong_objects = ygg::IndexList<Object> {};
    wrong_objects.push_back(ygg::Index<Object>(0));
    const auto wrong = ygg::Data<Binding>(relation, 1, std::move(wrong_objects));
    static_assert(!noexcept(repository.find_with_hash(wrong, Repository::hash(wrong))));
    static_assert(!noexcept(repository.find(wrong)));

    EXPECT_THROW(repository.find_with_hash(wrong, Repository::hash(wrong)), std::invalid_argument);
    EXPECT_THROW(repository.find(wrong), std::invalid_argument);
    EXPECT_THROW(repository.get_or_create(wrong), std::invalid_argument);
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
    repository.get_or_create(element);

    auto objects = ygg::IndexList<Object> {};
    objects.push_back(ygg::Index<Object>(0));
    objects.push_back(ygg::Index<Object>(1));
    const auto data = ygg::Data<Binding>(ygg::Index<RepositoryTypesRelation>(0), 2, objects);
    repository.get_or_create(data);

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

    const auto [root_view, root_created] = root.get_or_create(data);
    const auto [duplicate_view, duplicate_created] = root.get_or_create(data);

    EXPECT_TRUE(root_created);
    EXPECT_FALSE(duplicate_created);
    EXPECT_GT(root.memory_usage<Binding>(), 0);
    EXPECT_EQ(duplicate_view.get_index().row, root_view.get_index().row);
    EXPECT_EQ(root[root_view.get_index()][0], ygg::Index<Object>(0));
    EXPECT_EQ(root[root_view.get_index()][1], ygg::Index<Object>(3));

    objects[0] = ygg::Index<Object>(4);
    objects[1] = ygg::Index<Object>(0);
    const auto wider_data = ygg::Data<Binding>(relation, 2, objects);

    EXPECT_THROW(root.get_or_create(wider_data), std::out_of_range);
    EXPECT_EQ(root.size(relation), 1);

    const auto [root_duplicate_after_failure, created_after_failure] = root.get_or_create(data);
    EXPECT_FALSE(created_after_failure);
    EXPECT_EQ(root_duplicate_after_failure.get_index().row, root_view.get_index().row);

    auto inherited_width_child = factory.create(2, &root);
    objects[0] = ygg::Index<Object>(3);
    const auto inherited_width_data = ygg::Data<Binding>(relation, 2, objects);
    const auto [inherited_width_view, inherited_width_created] = inherited_width_child.get_or_create(inherited_width_data);
    EXPECT_TRUE(inherited_width_created);
    EXPECT_EQ(inherited_width_child[inherited_width_view.get_index()][0], ygg::Index<Object>(3));

    auto child = factory.create_shared(5, &root);
    const auto [inherited_view, inherited_created] = child->get_or_create(data);
    const auto [child_view, child_created] = child->get_or_create(wider_data);

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
    const auto [view, created] = repository.get_or_create(data);

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
    const auto [view, created] = repository.get_or_create(data);

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

}  // namespace ygg::tests
