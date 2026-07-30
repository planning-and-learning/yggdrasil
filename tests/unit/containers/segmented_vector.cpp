#include <concepts>
#include <cstddef>
#include <gtest/gtest.h>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>
#include <yggdrasil/containers/segmented_vector.hpp>

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
    using Vector = ygg::SegmentedVector<int, 2>;
    static_assert(std::random_access_iterator<Vector::iterator>);
    static_assert(std::random_access_iterator<Vector::const_iterator>);
    static_assert(std::ranges::random_access_range<Vector>);
    static_assert(std::ranges::random_access_range<const Vector>);

    Vector vector;

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
    EXPECT_EQ(vector.end() - vector.begin(), 7);
    EXPECT_EQ(vector.begin()[4], 40);
    EXPECT_EQ(*(3 + vector.begin()), 30);
    EXPECT_EQ(*vector.cbegin(), 0);
    EXPECT_EQ(vector.cend(), std::as_const(vector).end());
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

TEST(CommonSegmentedVectorTest, EmplaceBackConstructsInPlaceAndReturnsReference)
{
    ygg::SegmentedVector<std::string, 2> vector;

    auto& first = vector.emplace_back(3, char(120));
    first += "!";
    auto& second = vector.emplace_back("second");

    EXPECT_EQ(vector.size(), 2);
    EXPECT_EQ(vector.front(), "xxx!");
    EXPECT_EQ(vector.back(), "second");
    EXPECT_EQ(&second, &vector.back());
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

TEST(CommonSegmentedVectorTest, EmptyAccessThrows)
{
    ygg::SegmentedVector<int, 2> vector;
    const auto& const_vector = vector;

    EXPECT_THROW(vector.front(), std::out_of_range);
    EXPECT_THROW(const_vector.front(), std::out_of_range);
    EXPECT_THROW(vector.back(), std::out_of_range);
    EXPECT_THROW(const_vector.back(), std::out_of_range);
    EXPECT_THROW(vector.pop_back(), std::out_of_range);
}
}  // namespace ygg::tests
