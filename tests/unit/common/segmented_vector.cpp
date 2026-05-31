#include <yggdrasil/containers/segmented_vector.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <stdexcept>
#include <string>

namespace ygg::tests
{
TEST(CommonSegmentedVectorTest, EmptySizeCapacityAndMemoryUsage)
{
    ygg::SegmentedVector<int, 4> vector;

    EXPECT_TRUE(vector.empty());
    EXPECT_EQ(vector.size(), 0);
    EXPECT_EQ(vector.capacity(), 0);
    EXPECT_EQ(vector.memory_usage(), 0);
}

TEST(CommonSegmentedVectorTest, SupportsIndexedAccessAcrossSegments)
{
    ygg::SegmentedVector<int, 2> vector;

    for (int i = 0; i < 7; ++i)
        vector.push_back(i * 10);

    EXPECT_FALSE(vector.empty());
    EXPECT_EQ(vector.size(), 7);
    EXPECT_EQ(vector.capacity(), 14);
    EXPECT_EQ(vector.memory_usage(), 14 * sizeof(int));

    for (int i = 0; i < 7; ++i)
    {
        EXPECT_EQ(vector[static_cast<size_t>(i)], i * 10);
        EXPECT_EQ(vector.at(static_cast<size_t>(i)), i * 10);
    }

    EXPECT_EQ(vector.front(), 0);
    EXPECT_EQ(vector.back(), 60);
}

TEST(CommonSegmentedVectorTest, MutableAccessUpdatesStoredElements)
{
    ygg::SegmentedVector<std::string, 2> vector;
    vector.push_back("first");
    vector.push_back("second");
    vector.push_back("third");

    vector.front() = "changed-first";
    vector.back() = "changed-third";
    vector.at(1) = "changed-second";

    EXPECT_EQ(vector[0], "changed-first");
    EXPECT_EQ(vector[1], "changed-second");
    EXPECT_EQ(vector[2], "changed-third");
}

TEST(CommonSegmentedVectorTest, PopBackAndClearUpdateSize)
{
    ygg::SegmentedVector<int, 2> vector;
    vector.push_back(1);
    vector.push_back(2);
    vector.push_back(3);

    vector.pop_back();

    EXPECT_EQ(vector.size(), 2);
    EXPECT_EQ(vector.back(), 2);

    vector.clear();

    EXPECT_TRUE(vector.empty());
    EXPECT_EQ(vector.size(), 0);
    EXPECT_EQ(vector.capacity(), 6);
}

TEST(CommonSegmentedVectorTest, AtThrowsForOutOfRangeAccess)
{
    ygg::SegmentedVector<int, 2> vector;
    vector.push_back(1);

    EXPECT_THROW(vector.at(1), std::out_of_range);
}
}
