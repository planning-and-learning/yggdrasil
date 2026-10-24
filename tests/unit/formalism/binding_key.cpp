#include "yggdrasil/formalism/binding_key.hpp"

#include "yggdrasil/containers/associative_containers.hpp"

#include <cstddef>
#include <gtest/gtest.h>
#include <ranges>
#include <span>
#include <utility>
#include <vector>

namespace ygg::tests
{
namespace
{

struct StoredBinding
{
    int relation;
    std::vector<int> objects;

    auto get_key() const noexcept { return std::make_pair(relation, std::span<const int>(objects)); }
};

/// A row whose first arity objects spell a stored binding, like a rule binding that extends an action binding.
struct PrefixBinding
{
    int relation;
    std::vector<int> row;
    size_t arity;

    auto get_key() const noexcept
    {
        return std::make_pair(relation, std::views::take(row, static_cast<std::ranges::range_difference_t<decltype(row)>>(arity)));
    }
};

}

TEST(YggdrasilTests, BindingKeyLooksUpStoredBindingsByAnyKeyOfTheSameBinding)
{
    auto costs = ygg::UnorderedMap<StoredBinding, int, formalism::BindingKeyHash, formalism::BindingKeyEqualTo> {};
    costs.emplace(StoredBinding { 1, { 4, 5 } }, 7);

    const auto it = costs.find(PrefixBinding { 1, { 4, 5, 9 }, 2 });
    ASSERT_NE(it, costs.end());
    EXPECT_EQ(it->second, 7);

    EXPECT_EQ(costs.find(PrefixBinding { 1, { 4, 6, 9 }, 2 }), costs.end());
    EXPECT_EQ(costs.find(PrefixBinding { 2, { 4, 5, 9 }, 2 }), costs.end());
    EXPECT_EQ(costs.find(PrefixBinding { 1, { 4, 5, 9 }, 3 }), costs.end());
}

}  // namespace ygg::tests
