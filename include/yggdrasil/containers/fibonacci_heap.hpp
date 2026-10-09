/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_CONTAINERS_FIBONACCI_HEAP_HPP_
#define YGG_CONTAINERS_FIBONACCI_HEAP_HPP_

#include "yggdrasil/core/concepts.hpp"
#include "yggdrasil/semantics/comparators.hpp"

#include <boost/heap/fibonacci_heap.hpp>
#include <memory_resource>

namespace ygg
{
/// Boost's mutable heap with PMR allocation; max-heap by default. The resource must outlive the heap.
/// Merge requires equal resources. Boost's heap assignment and swap are unavailable with PMR.
template<typename T, LessFor<T> Compare = Less<T>>
using FibonacciHeap = boost::heap::fibonacci_heap<T, boost::heap::compare<Compare>, boost::heap::allocator<std::pmr::polymorphic_allocator<T>>>;
}  // namespace ygg

#endif
