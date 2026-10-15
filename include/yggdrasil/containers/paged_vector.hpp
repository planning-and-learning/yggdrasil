/*
 * Copyright (C) 2025-2026 Dominik Drexler
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

#ifndef YGG_CONTAINERS_PAGED_VECTOR_HPP_
#define YGG_CONTAINERS_PAGED_VECTOR_HPP_

#include "yggdrasil/containers/detail/slot_directory.hpp"
#include "yggdrasil/core/types.hpp"

#include <concepts>
#include <cstddef>
#include <memory>

namespace ygg
{

/// Sparse vector over a large index space: fixed-size pages of value-initialized elements are allocated on
/// first access, so memory follows the touched indices. ThreadSafe permits concurrent get_or_create() and find();
/// elements keep their addresses.
template<std::default_initializable T, size_t PageBits = 12, bool ThreadSafe = false>
class PagedVector
{
private:
    static constexpr size_t kPageSize = size_t(1) << PageBits;

    struct Page
    {
        std::unique_ptr<T[]> elements;

        Page() : elements(std::make_unique<T[]>(kPageSize)) {}
    };

    static ygg::uint_t page_index(size_t i) noexcept { return static_cast<ygg::uint_t>(i >> PageBits); }
    static size_t page_offset(size_t i) noexcept { return i & (kPageSize - 1); }

public:
    const T* find(size_t i) const noexcept
    {
        const auto* page = m_pages.find(page_index(i));
        return page ? &page->elements[page_offset(i)] : nullptr;
    }

    T* find(size_t i) noexcept
    {
        auto* page = m_pages.find(page_index(i));
        return page ? &page->elements[page_offset(i)] : nullptr;
    }

    T& get_or_create(size_t i) { return m_pages.get_or_create(page_index(i)).elements[page_offset(i)]; }

    size_t memory_usage() const noexcept
    {
        size_t bytes = m_pages.memory_usage();
        m_pages.for_each([&](const Page&) { bytes += kPageSize * sizeof(T); });
        return bytes;
    }

private:
    detail::SlotDirectory<Page, ThreadSafe> m_pages;
};

}  // namespace ygg

#endif
