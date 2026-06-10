/*
 * Copyright (C) 2025 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef YGG_COMMON_RAW_ARRAY_POOL_HPP_
#define YGG_COMMON_RAW_ARRAY_POOL_HPP_

#include "yggdrasil/core/bit.hpp"
#include "yggdrasil/core/concepts.hpp"

#include <bit>
#include <cassert>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vector>

namespace ygg {

template <TriviallyCopyable T, size_t ArraysPerSegment = 1024>
class RawArrayPool {
  static_assert(bit::is_power_of_two(ArraysPerSegment));

  static constexpr size_t seg_shift = std::countr_zero(ArraysPerSegment);
  static constexpr size_t seg_mask = ArraysPerSegment - 1;

private:
  static constexpr size_t max_array_size() noexcept {
    return std::numeric_limits<size_t>::max() / ArraysPerSegment;
  }

  static size_t segment_size_for(size_t array_size) {
    if (array_size > max_array_size())
      throw std::length_error(
          "RawArrayPool: array segment size exceeds addressable memory.");
    return ArraysPerSegment * array_size;
  }

  void increase_capacity() {
    if (m_cur_seg < m_segments.size() &&
        m_cur_pos + m_array_size <= m_segment_size)
      return;

    if (m_cur_seg + 1 < m_segments.size()) {
      m_cur_seg = m_cur_seg + 1;
      m_cur_pos = 0;
      return;
    }

    m_segments.emplace_back(m_segment_size);

    m_cur_seg = m_segments.size() - 1;
    m_cur_pos = 0;
  }

private:
  void ensure_index(size_t array_index) const {
    if (array_index >= m_size)
      throw std::out_of_range("RawArrayPool: index out of range.");
  }

  void ensure_not_empty() const {
    if (empty())
      throw std::out_of_range("RawArrayPool: container is empty.");
  }

public:
  explicit RawArrayPool(size_t array_size)
      : m_array_size(array_size), m_segment_size(segment_size_for(array_size)),
        m_cur_seg(0), m_cur_pos(0), m_size(0) {}

  T *allocate() {
    if (m_array_size == 0) {
      ++m_size;
      return nullptr;
    }

    increase_capacity();

    T *result = &m_segments[m_cur_seg][m_cur_pos];

    m_cur_pos += m_array_size;
    ++m_size;

    return result;
  }

  const T *operator[](size_t array_index) const noexcept {
    assert(array_index < m_size);
    if (m_array_size == 0)
      return nullptr;

    const size_t seg = array_index >> seg_shift;
    const size_t idx = array_index & seg_mask;
    return &m_segments[seg][idx * m_array_size];
  }

  T *operator[](size_t array_index) noexcept {
    assert(array_index < m_size);
    if (m_array_size == 0)
      return nullptr;

    const size_t seg = array_index >> seg_shift;
    const size_t idx = array_index & seg_mask;
    return &m_segments[seg][idx * m_array_size];
  }

  const T *at(size_t array_index) const {
    ensure_index(array_index);
    return (*this)[array_index];
  }

  T *at(size_t array_index) {
    ensure_index(array_index);
    return (*this)[array_index];
  }

  const T *front() const {
    ensure_not_empty();
    return (*this)[0];
  }

  T *front() {
    ensure_not_empty();
    return (*this)[0];
  }

  const T *back() const {
    ensure_not_empty();
    return (*this)[m_size - 1];
  }

  T *back() {
    ensure_not_empty();
    return (*this)[m_size - 1];
  }

  void clear() noexcept {
    m_cur_seg = 0;
    m_cur_pos = 0;
    m_size = 0;
  }

  size_t memory_usage() const noexcept {
    size_t bytes = 0;
    for (const auto &seg : m_segments)
      bytes += seg.capacity() * sizeof(T);
    return bytes;
  }

  size_t size() const noexcept { return m_size; }
  bool empty() const noexcept { return m_size == 0; }
  size_t array_size() const noexcept { return m_array_size; }

private:
  std::vector<std::vector<T>> m_segments;

  size_t m_array_size;
  size_t m_segment_size;

  size_t m_cur_seg;
  size_t m_cur_pos;
  size_t m_size;
};

} // namespace ygg

#endif