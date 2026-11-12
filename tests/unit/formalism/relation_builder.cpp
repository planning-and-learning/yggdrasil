#include "yggdrasil/formalism/relation_builder.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <gtest/gtest.h>
#include <vector>

namespace ygg::tests
{
namespace
{
using Builder = urd::builder<ygg::Relation<ygg::Schema>>;

std::vector<ygg::uint_t> positions(const Builder& rows, std::size_t index, std::span<const ygg::Value> key)
{
    const auto [first, last] = rows.index(index).equal_range(key);
    auto result = std::vector<ygg::uint_t>();
    for (auto it = first; it != last; ++it)
        result.push_back((*it).value);
    std::ranges::sort(result);
    return result;
}
}  // namespace

/// An index on some columns gives the positions of the rows with a key, also after a row moves into an erased one's.
TEST(RelationBuilder, IndexesFollowInsertAndErase)
{
    const auto columns =
        std::array { ColumnLayout { urd::index<Column>(0), type_id<ygg::uint_t> }, ColumnLayout { urd::index<Column>(1), type_id<ygg::uint_t> } };
    auto rows = Builder(columns);
    const auto first = std::array<std::size_t, 1> { 0 };
    const auto by_first = rows.add_index(first);
    for (const auto [a, b] :
         { std::pair<ygg::uint_t, ygg::uint_t> { 1, 2 }, std::pair<ygg::uint_t, ygg::uint_t> { 1, 3 }, std::pair<ygg::uint_t, ygg::uint_t> { 2, 3 } })
        rows.insert(std::array { encode_value(a), encode_value(b) });
    const auto one = std::array { encode_value(ygg::uint_t { 1 }) };
    EXPECT_EQ(rows.index(by_first).count(one), 2U);
    EXPECT_EQ(positions(rows, by_first, one), (std::vector<ygg::uint_t> { 0, 1 }));
    rows.erase(0);  // (2, 3) moves into position 0
    EXPECT_EQ(positions(rows, by_first, one), std::vector<ygg::uint_t> { 1 });
    EXPECT_EQ(positions(rows, by_first, std::array { encode_value(ygg::uint_t { 2 }) }), std::vector<ygg::uint_t> { 0 });
    EXPECT_EQ(rows.add_index(first), by_first);
    rows.initialize(columns);  // a builder handed out again keeps no index
    EXPECT_EQ(rows.storage().index_count(), 0U);
}
}  // namespace ygg::tests
