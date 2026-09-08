#include "dictionaries_library.hpp"

#include <gtest/gtest.h>

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

}  // namespace ygg::tests
