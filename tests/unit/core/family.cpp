// ygg::Family: one Store per type, reached by type; read by any member's handle, and layered where its members
// layer.

#include "urd/elements/data.hpp"
#include "urd/repr/records/arena.hpp"
#include "urd/repr/records/records.hpp"
#include "urd/repr/wrappers/layered_pool.hpp"
#include "urd/store.hpp"
#include "yggdrasil/core/family.hpp"

#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <vector>

namespace
{
struct Object;
struct Predicate;
}  // namespace

template<>
struct urd::data<Object>
{
    std::uint32_t id = 0;
    static constexpr auto cista_members() noexcept { return urd::Members<&data::id> {}; }
    static constexpr auto identifying_members() noexcept { return urd::Members<&data::id> {}; }
};
template<>
struct urd::data<Predicate>
{
    std::uint32_t arity = 0;
    static constexpr auto cista_members() noexcept { return urd::Members<&data::arity> {}; }
    static constexpr auto identifying_members() noexcept { return urd::Members<&data::arity> {}; }
};

namespace
{

/// A repository's symbols: per type, the records of T handing out index<T> (urd::pool<V, H>), layered.
template<typename T>
using Symbols = urd::Store<urd::pool<urd::data<T>, urd::index<T>>, urd::flat, urd::stable_refs, urd::layered>;
using Repository = ygg::Family<Symbols, Object, Predicate>;

}  // namespace

TEST(Family, ReadsEachMemberByItsHandle)
{
    auto root = Repository(nullptr);
    const auto a = root.get<Object>().insert(urd::data<Object> { 1 }).first;
    const auto p = root.get<Predicate>().insert(urd::data<Predicate> { 2 }).first;
    EXPECT_EQ(root[a].id, 1U);
    EXPECT_EQ(root[p].arity, 2U);
    EXPECT_TRUE(root.contains(a) && root.contains(p));
    EXPECT_FALSE(root.contains(urd::index<Object>(1)));
    auto sizes = std::vector<std::size_t>();
    root.for_each([&](const auto& member) { sizes.push_back(member.size()); });
    EXPECT_EQ(sizes, (std::vector<std::size_t> { 1, 1 }));
}

TEST(Family, LayersEachMemberOverItsParents)
{
    auto root = Repository(nullptr);
    const auto a = root.get<Object>().insert(urd::data<Object> { 1 }).first;
    auto child = Repository(&root);
    EXPECT_EQ(child.layer(), 1U);
    EXPECT_EQ(child.parent(), &root);
    EXPECT_EQ(child.get<Object>().insert(urd::data<Object> { 1 }).first, a);  // found in the parent
    const auto b = child.get<Object>().insert(urd::data<Object> { 3 }).first;
    EXPECT_EQ(b, urd::index<Object>(1));  // after the parent's
    EXPECT_EQ(child[a].id, 1U);           // the parent's, through the chain
    EXPECT_EQ(child[b].id, 3U);
    EXPECT_FALSE(root.contains(b));
    EXPECT_TRUE(child.contains(b));
    child.clear();
    EXPECT_FALSE(child.contains(b));
    EXPECT_TRUE(child.contains(a));
}
