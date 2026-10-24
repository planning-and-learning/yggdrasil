#include "yggdrasil/core/children.hpp"

#include "gtest/gtest.h"
#include <cista/containers/variant.h>
#include <cista/containers/vector.h>
#include <tuple>
#include <vector>

namespace ygg::tests::children
{
struct Leaf;
struct Node;
struct Store;

/// A plain struct holding children, like a pair of feature references.
struct LeafPair
{
    Index<Leaf> first;
    Index<Leaf> second;

    static constexpr auto cista_members() noexcept { return ygg::Members<&LeafPair::first, &LeafPair::second> {}; }
};
}  // namespace ygg::tests::children

namespace ygg
{
template<>
struct Data<tests::children::Node>
{
    Index<tests::children::Node> index;
    Index<tests::children::Leaf> single;
    IndexList<tests::children::Leaf> list;
    IndexMatrix<tests::children::Leaf> matrix;
    ::cista::offset::vector<::cista::offset::variant<Index<tests::children::Leaf>, int>> variants;
    ::cista::offset::vector<tests::children::LeafPair> pairs;
    int unrelated = 0;

    static constexpr auto cista_members() noexcept
    { return ygg::Members<&Data::index, &Data::single, &Data::list, &Data::matrix, &Data::variants, &Data::pairs, &Data::unrelated> {}; }
};
}  // namespace ygg

namespace ygg::tests::children
{
struct Store
{
    std::vector<Data<Node>> nodes;

    template<typename T>
    const Store& get_canonical_context(Index<T>) const noexcept
    { return *this; }
};
}  // namespace ygg::tests::children

namespace ygg
{
template<>
struct View<Index<tests::children::Leaf>, tests::children::Store>
{
    Index<tests::children::Leaf> handle;
    const tests::children::Store* store;

    View(Index<tests::children::Leaf> handle_, const tests::children::Store& store_) : handle(handle_), store(&store_) {}
    Index<tests::children::Leaf> get_index() const noexcept { return handle; }
    const tests::children::Store& get_context() const noexcept { return *store; }
};

template<>
struct View<Index<tests::children::Node>, tests::children::Store>
{
    Index<tests::children::Node> handle;
    const tests::children::Store* store;

    View(Index<tests::children::Node> handle_, const tests::children::Store& store_) : handle(handle_), store(&store_) {}
    Index<tests::children::Node> get_index() const noexcept { return handle; }
    const tests::children::Store& get_context() const noexcept { return *store; }
    const Data<tests::children::Node>& get_data() const { return store->nodes.at(handle.get_value()); }
};
}  // namespace ygg

namespace ygg::tests::children
{
namespace
{
using LeafIndex = Index<Leaf>;
using LeafView = View<LeafIndex, Store>;
using Children = TypeList<Leaf>;

Store make_store()
{
    auto node = Data<Node>();
    node.index = Index<Node>(0);
    node.single = LeafIndex(1);
    node.list.push_back(LeafIndex(2));
    node.list.push_back(LeafIndex(3));
    auto row = IndexList<Leaf>();
    row.push_back(LeafIndex(4));
    node.matrix.push_back(row);
    node.variants.push_back(LeafIndex(5));
    node.variants.push_back(7);
    node.pairs.push_back(LeafPair { LeafIndex(6), LeafIndex(8) });
    node.unrelated = 9;
    auto store = Store();
    store.nodes.push_back(node);
    return store;
}

TEST(YggdrasilChildren, VisitsChildrenInEveryNestedShapeInMemberOrder)
{
    const auto store = make_store();
    auto visited = std::vector<uint_t>();
    for_each_child<Children>(View<Index<Node>, Store>(Index<Node>(0), store), [&](LeafView child) { visited.push_back(child.get_index().get_value()); });
    EXPECT_EQ(visited, (std::vector<uint_t> { 1, 2, 3, 4, 5, 6, 8 }));
}

TEST(YggdrasilChildren, MapsChildrenInEveryNestedShapeAndKeepsTheRest)
{
    const auto store = make_store();
    const auto mapped = map_children<Children>(View<Index<Node>, Store>(Index<Node>(0), store),
                                               [&](LeafView child) { return LeafView(LeafIndex(child.get_index().get_value() + 100), store); });
    EXPECT_EQ(mapped.single, LeafIndex(101));
    EXPECT_EQ(mapped.list[0], LeafIndex(102));
    EXPECT_EQ(mapped.list[1], LeafIndex(103));
    EXPECT_EQ(mapped.matrix[0][0], LeafIndex(104));
    EXPECT_EQ(cista::get<LeafIndex>(mapped.variants[0]), LeafIndex(105));
    EXPECT_EQ(cista::get<int>(mapped.variants[1]), 7);
    EXPECT_EQ(mapped.pairs[0].first, LeafIndex(106));
    EXPECT_EQ(mapped.pairs[0].second, LeafIndex(108));
    EXPECT_EQ(mapped.unrelated, 9);
    EXPECT_EQ(mapped.index, Index<Node>(0));  // the record's own index is not a child
}
}  // namespace
}  // namespace ygg::tests::children
