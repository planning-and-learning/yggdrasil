/*
 * Copyright (C) 2025 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef YGG_COMMON_RAW_ARRAY_SET_HPP_
#define YGG_COMMON_RAW_ARRAY_SET_HPP_

#include "yggdrasil/containers/raw_array_pool.hpp"
#include "yggdrasil/core/concepts.hpp"
#include "yggdrasil/core/config.hpp"
#include "yggdrasil/semantics/equal_to.hpp"
#include "yggdrasil/semantics/hash.hpp"

#include <cassert>
#include <cstddef>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>

#include <gtl/phmap.hpp>

namespace ygg {

template <TriviallyCopyable T, size_t ArraysPerSegment = 1024>
class RawArraySet {
private:
  void ensure_fits(std::span<const T> value) const {
    if (value.size() != array_size())
      throw std::invalid_argument("RawArraySet: wrong number of elements.");
  }

public:
  explicit RawArraySet(size_t array_size)
      : m_pool(std::make_shared<RawArrayPool<T, ArraysPerSegment>>(array_size)),
        m_set(0, IndexableHash(m_pool, array_size),
              IndexableEqualTo(m_pool, array_size)) {}

  RawArraySet(const RawArraySet &) = delete;
  RawArraySet &operator=(const RawArraySet &) = delete;
  RawArraySet(RawArraySet &&) = default;
  RawArraySet &operator=(RawArraySet &&) = default;

  std::optional<uint_t> find(std::span<const T> value) const {
    ensure_fits(value);

    if (auto it = m_set.find(value); it != m_set.end())
      return *it;

    return std::nullopt;
  }

  bool contains(std::span<const T> value) const {
    ensure_fits(value);
    return m_set.contains(value);
  }

  uint_t insert(std::span<const T> value) {
    ensure_fits(value);

    if (auto it = m_set.find(value); it != m_set.end())
      return *it;

    const uint_t idx = to_uint_t(m_pool->size());
    auto *arr = m_pool->allocate();
    const auto size = array_size();
    if (size > 0)
      std::memcpy(arr, value.data(), size * sizeof(T));

    m_set.emplace(idx);
    return idx;
  }

  T *operator[](uint_t idx) noexcept { return (*m_pool)[idx]; }

  const T *operator[](uint_t idx) const noexcept { return (*m_pool)[idx]; }

  T *at(uint_t idx) { return m_pool->at(idx); }

  const T *at(uint_t idx) const { return m_pool->at(idx); }

  T *front() { return m_pool->front(); }

  const T *front() const { return m_pool->front(); }

  T *back() { return m_pool->back(); }

  const T *back() const { return m_pool->back(); }

  size_t memory_usage() const noexcept {
    size_t bytes = 0;
    bytes += m_pool ? m_pool->memory_usage() : 0;
    bytes += m_set.capacity() * (sizeof(uint_t) + sizeof(gtl::priv::ctrl_t));
    return bytes;
  }

  size_t size() const noexcept { return m_pool->size(); }
  bool empty() const noexcept { return m_pool->size() == 0; }
  size_t array_size() const noexcept { return m_pool->array_size(); }

  void clear() noexcept {
    m_pool->clear();
    m_set.clear();
  }

private:
  struct IndexableHash {
    using is_transparent = void;

    std::shared_ptr<RawArrayPool<T, ArraysPerSegment>> pool;
    size_t array_size;

    IndexableHash() noexcept : pool(nullptr), array_size(0) {}
    explicit IndexableHash(
        std::shared_ptr<RawArrayPool<T, ArraysPerSegment>> pool,
        size_t array_size) noexcept
        : pool(std::move(pool)), array_size(array_size) {}

    static size_t hash(const T *arr, size_t len) noexcept {
      return hash_range(std::span<const T>(arr, len));
    }

    size_t operator()(uint_t el) const noexcept {
      return hash((*pool)[el], array_size);
    }

    size_t operator()(std::span<const T> el) const noexcept {
      assert(el.size() == array_size);
      return hash(el.data(), array_size);
    }
  };

  struct IndexableEqualTo {
    using is_transparent = void;

    std::shared_ptr<RawArrayPool<T, ArraysPerSegment>> pool;
    size_t array_size;

    IndexableEqualTo() noexcept : pool(nullptr), array_size(0) {}
    explicit IndexableEqualTo(
        std::shared_ptr<RawArrayPool<T, ArraysPerSegment>> pool,
        size_t array_size) noexcept
        : pool(std::move(pool)), array_size(array_size) {}

    static bool equal_to(const T *lhs, const T *rhs, size_t len) {
      return equal_range(std::span<const T>(lhs, len),
                         std::span<const T>(rhs, len));
    }

    bool operator()(uint_t lhs, uint_t rhs) const noexcept {
      return equal_to((*pool)[lhs], (*pool)[rhs], array_size);
    }

    bool operator()(std::span<const T> lhs, uint_t rhs) const noexcept {
      assert(lhs.size() == array_size);
      return equal_to(lhs.data(), (*pool)[rhs], array_size);
    }

    bool operator()(uint_t lhs, std::span<const T> rhs) const noexcept {
      assert(rhs.size() == array_size);
      return equal_to((*pool)[lhs], rhs.data(), array_size);
    }

    bool operator()(std::span<const T> lhs,
                    std::span<const T> rhs) const noexcept {
      assert(lhs.size() == array_size);
      assert(rhs.size() == array_size);
      return equal_to(lhs.data(), rhs.data(), array_size);
    }
  };

  std::shared_ptr<RawArrayPool<T, ArraysPerSegment>> m_pool;
  gtl::flat_hash_set<uint_t, IndexableHash, IndexableEqualTo> m_set;
};

} // namespace ygg

#endif