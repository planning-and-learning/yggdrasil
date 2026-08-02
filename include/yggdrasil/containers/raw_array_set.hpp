/*
 * Copyright (C) 2025 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef YGG_CONTAINERS_RAW_ARRAY_SET_HPP_
#define YGG_CONTAINERS_RAW_ARRAY_SET_HPP_

#include "yggdrasil/containers/detail/basic_raw_set.hpp"
#include "yggdrasil/containers/raw_array_pool.hpp"
#include "yggdrasil/core/concepts.hpp"

#include <cstddef>
#include <memory>

namespace ygg
{

/// ThreadSafe permits concurrent lookup, insertion, size queries, and reads
/// after publication was observed through size() or external synchronization.
/// Clear, memory inspection, move, and destruction require quiescence.
template<TriviallyCopyable T, size_t ArraysPerSegment = 1024, bool ThreadSafe = false>
class RawArraySet : public detail::BasicRawSet<RawArrayPool<T, ArraysPerSegment, ThreadSafe>>
{
private:
    using pool_type = RawArrayPool<T, ArraysPerSegment, ThreadSafe>;
    using Base = detail::BasicRawSet<pool_type>;

public:
    explicit RawArraySet(size_t array_size) : Base(std::make_unique<pool_type>(array_size)) {}

    size_t array_size() const noexcept { return Base::storage().array_size(); }
};

}  // namespace ygg

#endif
