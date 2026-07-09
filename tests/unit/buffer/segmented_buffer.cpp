/*
 * Copyright (C) 2026 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include <array>
#include <cstdint>
#include <gtest/gtest.h>
#include <limits>
#include <stdexcept>
#include <yggdrasil/buffer/segmented_buffer.hpp>

namespace ygg::tests
{

TEST(YggdrasilTests, BufferSegmentedBufferRejectsInvalidConstructionAndWriteArguments)
{
    EXPECT_THROW(buffer::SegmentedBuffer(0), std::invalid_argument);
    EXPECT_THROW(buffer::SegmentedBuffer(3), std::invalid_argument);

    auto arena = buffer::SegmentedBuffer();
    EXPECT_TRUE(arena.empty());
    const auto value = std::array<uint8_t, 1> { 1 };

    EXPECT_THROW(arena.write(nullptr, 1), std::invalid_argument);
    EXPECT_THROW(arena.write(value.data(), value.size(), 0), std::invalid_argument);
    EXPECT_THROW(arena.write(value.data(), value.size(), 3), std::invalid_argument);
    EXPECT_THROW(arena.write(value.data(), std::numeric_limits<size_t>::max(), 8), std::length_error);
    EXPECT_THROW(arena.write(value.data(), std::numeric_limits<size_t>::max() / 2), std::length_error);
}

TEST(YggdrasilTests, BufferSegmentedBufferTracksSizeCapacityAndRemainingSpace)
{
    auto arena = buffer::SegmentedBuffer(8);
    const auto first = std::array<uint8_t, 3> { 1, 2, 3 };
    const auto first_position = arena.write(first.data(), first.size());

    EXPECT_NE(first_position, nullptr);
    EXPECT_EQ(first_position[0], 1);
    EXPECT_EQ(first_position[1], 2);
    EXPECT_EQ(first_position[2], 3);
    EXPECT_EQ(arena.size(), first.size());
    EXPECT_FALSE(arena.empty());
    EXPECT_EQ(arena.num_segments(), 1);
    EXPECT_GE(arena.capacity(), arena.size());
    EXPECT_EQ(arena.remaining_in_current_segment(), arena.capacity() - arena.size());

    const auto capacity = arena.capacity();
    arena.clear();

    EXPECT_EQ(arena.size(), 0);
    EXPECT_TRUE(arena.empty());
    EXPECT_EQ(arena.capacity(), capacity);
    EXPECT_EQ(arena.num_segments(), 1);
    EXPECT_EQ(arena.remaining_in_current_segment(), capacity);
}

TEST(YggdrasilTests, BufferSegmentedBufferHonorsAlignmentAndMovesToNewSegments)
{
    auto arena = buffer::SegmentedBuffer(4);
    const auto byte = std::array<uint8_t, 1> { 1 };
    const auto aligned = std::array<uint8_t, 4> { 2, 3, 4, 5 };

    static_cast<void>(arena.write(byte.data(), byte.size()));
    const auto aligned_position = arena.write(aligned.data(), aligned.size(), 4);

    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(aligned_position) % 4, 0);
    EXPECT_EQ(aligned_position[0], 2);
    EXPECT_EQ(aligned_position[3], 5);
    EXPECT_EQ(arena.size(), 8);
    EXPECT_EQ(arena.remaining_in_current_segment(), 0);

    const auto second_segment_position = arena.write(byte.data(), byte.size());

    EXPECT_NE(second_segment_position, nullptr);
    EXPECT_EQ(arena.num_segments(), 2);
    EXPECT_EQ(arena.size(), 9);
}

}  // namespace ygg::tests
