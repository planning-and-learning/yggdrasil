#include "yggdrasil/formalism/row_key.hpp"


#include <cstddef>
#include <gtest/gtest.h>
#include <gtl/phmap.hpp>
#include <ranges>
#include <span>
#include <urd/repr/maps/unordered_flat_map.hpp>
#include <urd/repr/values/unordered_flat_set.hpp>
#include <urd/repr/sequences/sequence.hpp>
#include <urd/store.hpp>
#include <utility>
#include <vector>

namespace ygg::tests
{
namespace
{

struct StoredRow
{
    int relation;
    urd::Store<urd::sequence<int>> objects;

    [[nodiscard]] auto get_key() const noexcept { return std::make_pair(relation, std::span<const int>(objects)); }
};

/// A row whose first arity values spell a stored row, like a rule row that extends an action row.
struct PrefixRow
{
    int relation;
    urd::Store<urd::sequence<int>> row;
    size_t arity;

    [[nodiscard]] auto get_key() const noexcept
    { return std::make_pair(relation, std::views::take(row, static_cast<std::ranges::range_difference_t<decltype(row)>>(arity))); }
};

}

TEST(YggdrasilTests, RowKeyLooksUpStoredRowsByAnyKeyOfTheSameRow)
{
    auto costs = gtl::flat_hash_map<StoredRow, int, RowKeyHash, RowKeyEqualTo> {};
    costs.emplace(StoredRow { 1, { 4, 5 } }, 7);

    const auto it = costs.find(PrefixRow { 1, { 4, 5, 9 }, 2 });
    ASSERT_NE(it, costs.end());
    EXPECT_EQ(it->second, 7);

    EXPECT_EQ(costs.find(PrefixRow { 1, { 4, 6, 9 }, 2 }), costs.end());
    EXPECT_EQ(costs.find(PrefixRow { 2, { 4, 5, 9 }, 2 }), costs.end());
    EXPECT_EQ(costs.find(PrefixRow { 1, { 4, 5, 9 }, 3 }), costs.end());
}

}  // namespace ygg::tests
