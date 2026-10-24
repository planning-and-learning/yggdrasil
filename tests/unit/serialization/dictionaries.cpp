#include "dictionaries_library.hpp"

#include <gtest/gtest.h>
#include <optional>
#include <tuple>
#include <vector>

namespace ygg::tests
{

TEST(YggdrasilTests, DictionariesRegisteredInSharedLibraryWorkAcrossTheBoundary)
{
    ASSERT_NE(&library_entity_type(), &typeid(DictionaryEntity));

    auto dictionaries = serialization::Dictionaries {};
    auto projections = 0;
    register_in_library(dictionaries, projections);
    EXPECT_EQ(dictionaries.serialize(DictionaryEntity { 7 }), "e0");
    EXPECT_EQ(serialize_in_library(dictionaries, 7), "e0");
    EXPECT_EQ(projections, 1);
    EXPECT_EQ(dictionaries.table<DictionaryEntity>(), boost::json::parse(R"([{"scaled":70}])").as_array());
    EXPECT_EQ(table_in_library(dictionaries), dictionaries.table<DictionaryEntity>());

    auto copy = dictionaries;
    EXPECT_EQ(serialize_in_library(copy, 8), "e1");
    EXPECT_EQ(dictionaries.table<DictionaryEntity>().size(), 1);
    EXPECT_EQ(dictionaries.serialize(DictionaryEntity { 9 }), "e1");
    EXPECT_EQ(copy.table<DictionaryEntity>(), boost::json::parse(R"([{"scaled":70},{"scaled":80}])").as_array());
    EXPECT_EQ(table_in_library(dictionaries), boost::json::parse(R"([{"scaled":70},{"scaled":90}])").as_array());
}

TEST(YggdrasilTests, DictionariesRegisteredInExecutableWorkInSharedLibrary)
{
    auto dictionaries = serialization::Dictionaries {};
    dictionaries.register_table<DictionaryEntity>("entities", "e");

    EXPECT_EQ(serialize_in_library(dictionaries, 4), "e0");
    EXPECT_EQ(dictionaries.serialize(DictionaryEntity { 4 }), "e0");
    EXPECT_EQ(table_in_library(dictionaries), boost::json::parse(R"([{"value":4}])").as_array());
}

namespace
{
struct NestedEntity
{
    int id;
    DictionaryEntity child;
    std::vector<DictionaryEntity> children;
    std::optional<DictionaryEntity> present;
    std::optional<DictionaryEntity> absent;
    std::vector<int> numbers;

    static constexpr auto identifying_members() noexcept { return ygg::Members<&NestedEntity::id> {}; }
};

template<typename Archive>
void describe_fields(Archive& archive, std::type_identity<NestedEntity>)
{
    archive.field("child", [](const NestedEntity& entity) -> decltype(auto) { return (entity.child); });
    archive.field("children", [](const NestedEntity& entity) -> decltype(auto) { return (entity.children); });
    archive.field("present", [](const NestedEntity& entity) -> decltype(auto) { return (entity.present); });
    archive.field("absent", [](const NestedEntity& entity) -> decltype(auto) { return (entity.absent); });
    archive.field("numbers", [](const NestedEntity& entity) -> decltype(auto) { return (entity.numbers); });
}
}  // namespace

TEST(YggdrasilTests, DictionariesSerializeNestedRangesAndOptionals)
{
    auto dictionaries = serialization::Dictionaries {};
    dictionaries.register_table<DictionaryEntity>("entities", "e");
    dictionaries.register_table<NestedEntity>("nested", "n");

    const auto entity = NestedEntity { 0, { 1 }, { { 2 }, { 1 } }, DictionaryEntity { 3 }, std::nullopt, { 4, 5 } };
    EXPECT_EQ(dictionaries.serialize(entity), "n0");
    EXPECT_EQ(dictionaries.serialize(std::vector<NestedEntity> { entity }), boost::json::parse(R"(["n0"])"));
    EXPECT_EQ(dictionaries.table<NestedEntity>(),
              boost::json::parse(R"([{"child":"e0","children":["e1","e0"],"present":"e2","absent":null,"numbers":[4,5]}])").as_array());
    EXPECT_EQ(dictionaries.table<DictionaryEntity>(), boost::json::parse(R"([{"value":1},{"value":2},{"value":3}])").as_array());
}

namespace
{
/// Serialized as its entity, like a bidirectional graph as its forward graph.
struct EntityWrapper
{
    DictionaryEntity entity;
};

template<typename Archive>
void describe_fields(Archive& archive, std::type_identity<EntityWrapper>)
{ describe_fields(archive, std::type_identity<DictionaryEntity> {}); }

const DictionaryEntity& serialize_as(serialization::Dictionaries&, const EntityWrapper& wrapper) { return wrapper.entity; }
}  // namespace

TEST(YggdrasilTests, DictionariesSerializeTypesAsTheirReplacement)
{
    auto dictionaries = serialization::Dictionaries {};
    dictionaries.register_table<DictionaryEntity>("entities", "e");

    EXPECT_EQ(dictionaries.serialize(EntityWrapper { { 6 } }), "e0");
    EXPECT_EQ(dictionaries.serialize(std::vector<EntityWrapper> { { { 6 } }, { { 7 } } }), boost::json::parse(R"(["e0","e1"])"));
    EXPECT_EQ(serialization::fields<EntityWrapper>(), std::vector<std::string> { "value" });
    EXPECT_EQ(dictionaries.table<DictionaryEntity>(), boost::json::parse(R"([{"value":6},{"value":7}])").as_array());
}

}  // namespace ygg::tests
