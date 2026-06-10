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

#include <yggdrasil/containers/repository_types.hpp>
#include <yggdrasil/formalism/binding_data.hpp>
#include <yggdrasil/formalism/binding_view.hpp>
#include <yggdrasil/formalism/builder.hpp>
#include <yggdrasil/formalism/relation_repository.hpp>
#include <yggdrasil/formalism/repository.hpp>
#include <yggdrasil/formalism/repository_factory.hpp>
#include <yggdrasil/formalism/symbol_repository.hpp>
#include <yggdrasil/ids/index_mixins.hpp>

#include <stdexcept>
#include <tuple>
#include <vector>

namespace ygg::tests {

struct RepositoryTypesElement;
struct RepositoryTypesRelation;
struct RepositoryTypesObjectTag;
struct RepositoryTypesBuilderOther;

struct RepositoryTypesContext {
  const RepositoryTypesContext &
  get_canonical_context(const RepositoryTypesElement &) const noexcept;
  size_t get_index() const noexcept { return 0; }
};

} // namespace ygg::tests

namespace ygg {

template <>
struct Index<tests::RepositoryTypesElement>
    : IndexMixin<Index<tests::RepositoryTypesElement>> {
  using Base = IndexMixin<Index<tests::RepositoryTypesElement>>;
  using Base::Base;
};

template <> struct Data<tests::RepositoryTypesElement> {
  Index<tests::RepositoryTypesElement> index;
  int value = 0;

  auto identifying_members() const noexcept { return std::tie(value); }
};

template <>
struct Index<tests::RepositoryTypesRelation>
    : IndexMixin<Index<tests::RepositoryTypesRelation>> {
  using Base = IndexMixin<Index<tests::RepositoryTypesRelation>>;
  using Base::Base;
};

template <>
struct Index<tests::RepositoryTypesBuilderOther>
    : IndexMixin<Index<tests::RepositoryTypesBuilderOther>> {
  using Base = IndexMixin<Index<tests::RepositoryTypesBuilderOther>>;
  using Base::Base;
};

template <> struct Data<tests::RepositoryTypesBuilderOther> {
  int value = 0;

  auto identifying_members() const noexcept { return std::tie(value); }
};

} // namespace ygg

namespace ygg::tests {

struct RepositoryTypesRepository {
  using Binding = ygg::formalism::RelationBinding<RepositoryTypesRelation,
                                                  RepositoryTypesObjectTag>;
  using Object = ygg::formalism::Object<RepositoryTypesObjectTag>;

  std::vector<ygg::Index<Object>> operator[](ygg::Index<Binding>) const {
    return {};
  }
};

inline const RepositoryTypesRepository &
get_repository(const RepositoryTypesContext &) noexcept {
  static const auto repository = RepositoryTypesRepository();
  return repository;
}

TEST(YggdrasilTests, CommonRepositoryTypesUmbrellaHeaderCompiles) {
  static_assert(
      CanonicalizableContext<RepositoryTypesElement, RepositoryTypesContext>);
  static_assert(CanonicalizableContextFor<RepositoryTypesContext,
                                          RepositoryTypesElement>);

  SUCCEED();
}

TEST(YggdrasilTests, CommonBuilderStorageReusesReleasedBuildersByType) {
  auto storage = ygg::formalism::BuilderStorage<RepositoryTypesElement,
                                                RepositoryTypesBuilderOther>();

  auto element_builder = storage.get_builder<RepositoryTypesElement>();
  element_builder->value = 7;
  const auto *first_element_address = element_builder.get();
  element_builder = {};

  auto reused_element_builder = storage.get_builder<RepositoryTypesElement>();
  EXPECT_EQ(reused_element_builder.get(), first_element_address);
  EXPECT_EQ(reused_element_builder->value, 7);

  auto other_builder = storage.get_builder<RepositoryTypesBuilderOther>();
  other_builder->value = 3;
  EXPECT_NE(static_cast<const void *>(other_builder.get()),
            static_cast<const void *>(reused_element_builder.get()));
}

TEST(YggdrasilTests,
     CommonRepositoryFactoryAssignsIncreasingIndicesAndParents) {
  using SymbolRepo = ygg::formalism::SymbolRepository<RepositoryTypesElement>;
  using RelationRepo =
      ygg::formalism::RelationRepository<RepositoryTypesObjectTag,
                                         RepositoryTypesRelation>;
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

TEST(YggdrasilTests, CommonRelationBindingDataValidatesArity) {
  using Object = ygg::formalism::Object<RepositoryTypesObjectTag>;
  using Binding = ygg::formalism::RelationBinding<RepositoryTypesRelation,
                                                  RepositoryTypesObjectTag>;

  auto objects = ygg::IndexList<Object>{};
  objects.push_back(ygg::Index<Object>(0));
  objects.push_back(ygg::Index<Object>(1));

  EXPECT_NO_THROW(
      (ygg::Data<Binding>(ygg::Index<RepositoryTypesRelation>(0), 2, objects)));
  EXPECT_THROW(
      (ygg::Data<Binding>(ygg::Index<RepositoryTypesRelation>(0), 1, objects)),
      std::invalid_argument);
}

TEST(YggdrasilTests, CommonBasicSymbolRepositoryFrontLocalIsChecked) {
  auto repository =
      ygg::formalism::BasicSymbolRepository<RepositoryTypesElement>();

  EXPECT_THROW(repository.front_local(), std::out_of_range);

  auto data = ygg::Data<RepositoryTypesElement>{};
  data.value = 7;
  const auto [index, created] = repository.get_or_create_local(data);

  EXPECT_TRUE(created);
  EXPECT_EQ(index, ygg::Index<RepositoryTypesElement>(0));
  EXPECT_EQ(repository.front_local().value, 7);

  repository.clear();
  EXPECT_THROW(repository.front_local(), std::out_of_range);
}

TEST(YggdrasilTests, CommonSymbolRepositoryTracksParentAndLocalSize) {
  using Repository = ygg::formalism::SymbolRepository<RepositoryTypesElement>;

  auto root = Repository();
  auto child = Repository(&root);

  EXPECT_EQ(root.size<RepositoryTypesElement>(), 0);
  EXPECT_EQ(child.size<RepositoryTypesElement>(), 0);
  EXPECT_EQ(child.parent_size<RepositoryTypesElement>(), 0);
  EXPECT_EQ(child.local_size<RepositoryTypesElement>(), 0);

  auto root_data = ygg::Data<RepositoryTypesElement>{};
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

  auto child_data = ygg::Data<RepositoryTypesElement>{};
  child_data.value = 11;
  const auto [child_index, child_created] =
      child.get_or_create_local(child_data);

  EXPECT_TRUE(child_created);
  EXPECT_EQ(child_index, ygg::Index<RepositoryTypesElement>(1));
  EXPECT_EQ(child.local_size<RepositoryTypesElement>(), 1);
  EXPECT_EQ(child.size<RepositoryTypesElement>(), 2);
}

TEST(YggdrasilTests, CommonBasicRelationRepositoryFrontLocalIsChecked) {
  using Object = ygg::formalism::Object<RepositoryTypesObjectTag>;
  using Binding = ygg::formalism::RelationBinding<RepositoryTypesRelation,
                                                  RepositoryTypesObjectTag>;
  using Repository =
      ygg::formalism::BasicRelationRepository<RepositoryTypesObjectTag,
                                              RepositoryTypesRelation>;

  auto repository = Repository();
  const auto relation = ygg::Index<RepositoryTypesRelation>(0);

  EXPECT_THROW(repository.front_local(relation), std::out_of_range);

  auto objects = ygg::IndexList<Object>{};
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

TEST(YggdrasilTests, CommonRelationRepositoryForwardsAcrossParents) {
  using Object = ygg::formalism::Object<RepositoryTypesObjectTag>;
  using Binding = ygg::formalism::RelationBinding<RepositoryTypesRelation,
                                                  RepositoryTypesObjectTag>;
  using Repository =
      ygg::formalism::RelationRepository<RepositoryTypesObjectTag,
                                         RepositoryTypesRelation>;

  auto root = Repository();
  auto child = Repository(&root);
  const auto relation = ygg::Index<RepositoryTypesRelation>(0);
  const auto missing =
      ygg::Index<Binding>{relation, ygg::Index<ygg::formalism::Row>(0)};

  auto objects = ygg::IndexList<Object>{};
  objects.push_back(ygg::Index<Object>(0));
  objects.push_back(ygg::Index<Object>(1));
  const auto data = ygg::Data<Binding>(relation, 2, objects);

  EXPECT_EQ(root.find(data), std::nullopt);
  EXPECT_THROW(root[missing], std::out_of_range);
  EXPECT_THROW(root.get_canonical_context(missing), std::out_of_range);

  const auto [index, created] = root.get_or_create(data);

  EXPECT_TRUE(created);
  const auto root_found = root.find(data);
  ASSERT_TRUE(root_found.has_value());
  EXPECT_EQ(root_found->relation, index.relation);
  EXPECT_EQ(root_found->row, index.row);
  EXPECT_EQ(root[index].size(), 2);
  EXPECT_EQ(&root.get_canonical_context(index),
            &root.get<RepositoryTypesRelation>());

  const auto [child_index, child_created] = child.get_or_create(data);

  EXPECT_FALSE(child_created);
  EXPECT_EQ(child_index.relation, index.relation);
  EXPECT_EQ(child_index.row, index.row);
  const auto child_found = child.find(data);
  ASSERT_TRUE(child_found.has_value());
  EXPECT_EQ(child_found->relation, index.relation);
  EXPECT_EQ(child_found->row, index.row);
  EXPECT_EQ(child[index].size(), 2);
  EXPECT_EQ(&child.get_canonical_context(index),
            &root.get<RepositoryTypesRelation>());
}

TEST(YggdrasilTests, CommonRelationBindingRangeViewsExposeRows) {
  using Binding = ygg::formalism::RelationBinding<RepositoryTypesRelation,
                                                  RepositoryTypesObjectTag>;

  const auto relation = ygg::Index<RepositoryTypesRelation>(4);
  using Rows = std::vector<ygg::Index<ygg::formalism::Row>>;

  const auto rows = Rows{ygg::Index<ygg::formalism::Row>(2),
                         ygg::Index<ygg::formalism::Row>(3)};
  const auto context = RepositoryTypesContext();

  using ForwardRange = ygg::formalism::RelationBindingsForwardRange<
      RepositoryTypesRelation, RepositoryTypesObjectTag, Rows>;
  const auto forward_range = ForwardRange{relation, rows};
  const auto forward_view =
      ygg::View<ForwardRange, RepositoryTypesContext>(forward_range, context);
  EXPECT_FALSE(forward_view.empty());
  EXPECT_EQ(forward_view.size(), 2);
  static_assert(!noexcept(forward_view.front()));
  EXPECT_EQ(forward_view.front().get_index().relation, relation);
  EXPECT_EQ(forward_view.front().get_index().row, rows.front());

  const auto empty_rows = Rows{};
  const auto empty_forward_range = ForwardRange{relation, empty_rows};
  const auto empty_forward_view =
      ygg::View<ForwardRange, RepositoryTypesContext>(empty_forward_range,
                                                      context);
  EXPECT_TRUE(empty_forward_view.empty());
  EXPECT_EQ(empty_forward_view.size(), 0);
  EXPECT_THROW(empty_forward_view.front(), std::out_of_range);

  using RandomAccessRange = ygg::formalism::RelationBindingsRandomAccessRange<
      RepositoryTypesRelation, RepositoryTypesObjectTag, Rows>;
  const auto random_access_range = RandomAccessRange{relation, rows};
  const auto random_access_view =
      ygg::View<RandomAccessRange, RepositoryTypesContext>(random_access_range,
                                                           context);
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

  const auto empty_random_access_range =
      RandomAccessRange{relation, empty_rows};
  const auto empty_random_access_view =
      ygg::View<RandomAccessRange, RepositoryTypesContext>(
          empty_random_access_range, context);
  EXPECT_TRUE(empty_random_access_view.empty());
  EXPECT_EQ(empty_random_access_view.size(), 0);
  EXPECT_THROW(empty_random_access_view.front(), std::out_of_range);
  EXPECT_THROW(empty_random_access_view.back(), std::out_of_range);
}

TEST(YggdrasilTests, CommonRepositoryThrowsForMissingSymbolIndices) {
  using SymbolRepo = ygg::formalism::SymbolRepository<RepositoryTypesElement>;
  using RelationRepo =
      ygg::formalism::RelationRepository<RepositoryTypesObjectTag,
                                         RepositoryTypesRelation>;
  using Repository = ygg::formalism::Repository<SymbolRepo, RelationRepo>;

  auto repository = Repository(0);

  EXPECT_THROW(repository[ygg::Index<RepositoryTypesElement>(0)],
               std::out_of_range);
  EXPECT_THROW(
      repository.get_canonical_context(ygg::Index<RepositoryTypesElement>(0)),
      std::out_of_range);
  EXPECT_THROW(
      ygg::make_view(ygg::Index<RepositoryTypesElement>(0), repository),
      std::out_of_range);

  auto data = ygg::Data<RepositoryTypesElement>{};
  data.value = 7;
  const auto [view, created] = repository.get_or_create(data);

  EXPECT_TRUE(created);
  EXPECT_EQ(repository[view.get_index()].value, 7);
  EXPECT_EQ(&repository.get_canonical_context(view.get_index()), &repository);
  EXPECT_THROW(repository[ygg::Index<RepositoryTypesElement>(1)],
               std::out_of_range);
  EXPECT_THROW(
      ygg::make_view(ygg::Index<RepositoryTypesElement>(1), repository),
      std::out_of_range);
}

TEST(YggdrasilTests, CommonRepositoryThrowsForMissingRelationBindingIndices) {
  using Binding = ygg::formalism::RelationBinding<RepositoryTypesRelation,
                                                  RepositoryTypesObjectTag>;
  using Object = ygg::formalism::Object<RepositoryTypesObjectTag>;
  using SymbolRepo = ygg::formalism::SymbolRepository<RepositoryTypesElement>;
  using RelationRepo =
      ygg::formalism::RelationRepository<RepositoryTypesObjectTag,
                                         RepositoryTypesRelation>;
  using Repository = ygg::formalism::Repository<SymbolRepo, RelationRepo>;

  auto repository = Repository(0);
  const auto relation = ygg::Index<RepositoryTypesRelation>(0);
  const auto missing =
      ygg::Index<Binding>{relation, ygg::Index<ygg::formalism::Row>(0)};

  EXPECT_THROW(repository[missing], std::out_of_range);
  EXPECT_THROW(repository.front(relation), std::out_of_range);
  EXPECT_THROW(repository.get_canonical_context(missing), std::out_of_range);
  EXPECT_THROW(ygg::make_view(missing, repository), std::out_of_range);

  auto objects = ygg::IndexList<Object>{};
  objects.push_back(ygg::Index<Object>(0));
  objects.push_back(ygg::Index<Object>(1));
  const auto data = ygg::Data<Binding>(relation, 2, objects);
  const auto [view, created] = repository.get_or_create(data);

  EXPECT_TRUE(created);
  EXPECT_EQ(repository[view.get_index()].size(), 2);
  EXPECT_EQ(repository.front(relation).size(), 2);
  EXPECT_EQ(&repository.get_canonical_context(view.get_index()), &repository);

  const auto missing_row =
      ygg::Index<Binding>{relation, ygg::Index<ygg::formalism::Row>(1)};
  const auto missing_relation =
      ygg::Index<Binding>{ygg::Index<RepositoryTypesRelation>(1),
                          ygg::Index<ygg::formalism::Row>(0)};
  EXPECT_THROW(repository[missing_row], std::out_of_range);
  EXPECT_THROW(ygg::make_view(missing_row, repository), std::out_of_range);
  EXPECT_THROW(repository[missing_relation], std::out_of_range);
  EXPECT_THROW(ygg::make_view(missing_relation, repository), std::out_of_range);
}

} // namespace ygg::tests
