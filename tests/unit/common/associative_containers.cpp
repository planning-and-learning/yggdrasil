#include <yggdrasil/containers/associative_containers.hpp>
#include <yggdrasil/containers/unordered_set.hpp>

#include <yggdrasil/semantics/comparators.hpp>
#include <yggdrasil/semantics/equal_to.hpp>
#include <yggdrasil/semantics/hash.hpp>

#include <gtest/gtest.h>

#include <string>

namespace ygg::tests {

TEST(YggdrasilTests, CommonAssociativeContainerAliasesUseCommonComparators) {
  auto unordered_set = ygg::UnorderedSet<int>{};
  unordered_set.insert(3);
  EXPECT_TRUE(unordered_set.contains(3));

  auto unordered_map = ygg::UnorderedMap<int, std::string>{};
  unordered_map.emplace(1, "one");
  EXPECT_EQ(unordered_map.at(1), "one");

  auto set = ygg::Set<int>{};
  set.insert(2);
  set.insert(1);
  EXPECT_EQ(*set.begin(), 1);

  auto map = ygg::Map<int, std::string>{};
  map.emplace(2, "two");
  map.emplace(1, "one");
  EXPECT_EQ(map.begin()->first, 1);
}

TEST(YggdrasilTests, CommonUnorderedSetHelpersMutateTargetOnly) {
  auto target = ygg::UnorderedSet<int>{1, 2, 3};
  const auto other = ygg::UnorderedSet<int>{2, 3, 4};

  ygg::intersect_inplace(target, other);

  EXPECT_EQ(target.size(), 2);
  EXPECT_TRUE(target.contains(2));
  EXPECT_TRUE(target.contains(3));
  EXPECT_FALSE(target.contains(1));

  ygg::union_inplace(target, other);

  EXPECT_EQ(target.size(), 3);
  EXPECT_TRUE(target.contains(2));
  EXPECT_TRUE(target.contains(3));
  EXPECT_TRUE(target.contains(4));
}

} // namespace ygg::tests
