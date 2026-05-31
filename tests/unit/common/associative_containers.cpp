#include <yggdrasil/containers/associative_containers.hpp>

#include <yggdrasil/semantics/comparators.hpp>
#include <yggdrasil/semantics/equal_to.hpp>
#include <yggdrasil/semantics/hash.hpp>

#include <gtest/gtest.h>

#include <string>

namespace ygg::tests
{

TEST(YggdrasilTests, CommonAssociativeContainerAliasesUseCommonComparators)
{
    auto unordered_set = ygg::UnorderedSet<int> {};
    unordered_set.insert(3);
    EXPECT_TRUE(unordered_set.contains(3));

    auto unordered_map = ygg::UnorderedMap<int, std::string> {};
    unordered_map.emplace(1, "one");
    EXPECT_EQ(unordered_map.at(1), "one");

    auto set = ygg::Set<int> {};
    set.insert(2);
    set.insert(1);
    EXPECT_EQ(*set.begin(), 1);

    auto map = ygg::Map<int, std::string> {};
    map.emplace(2, "two");
    map.emplace(1, "one");
    EXPECT_EQ(map.begin()->first, 1);
}

}  // namespace ygg::tests
