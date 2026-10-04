/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_CONTAINERS_SPAN_HPP_
#define YGG_CONTAINERS_SPAN_HPP_

#include "yggdrasil/core/types.hpp"

#include <cassert>
#include <compare>
#include <cstddef>
#include <iterator>
#include <ranges>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace ygg
{
/// Read-only element access. The span is copied; its elements and context remain borrowed.
template<typename T, size_t Extent, typename C>
class View<std::span<T, Extent>, C>
{
    using Element = std::remove_const_t<T>;
    using Span = std::span<T, Extent>;
    Span m_handle;
    const C* m_context;

public:
    View(Span handle, const C& context) noexcept : m_handle(handle), m_context(&context) {}
    const Span& get_handle() const noexcept { return m_handle; }
    std::span<const Element, Extent> get_data() const noexcept { return m_handle; }
    const C& get_context() const noexcept { return *m_context; }
    size_t size() const noexcept { return m_handle.size(); }
    bool empty() const noexcept { return m_handle.empty(); }
    const Element* data() const noexcept { return m_handle.data(); }

    decltype(auto) operator[](size_t index) const
    {
        assert(index < size());
        if constexpr (ViewConcept<Element, C>)
            return make_view(m_handle[index], *m_context);
        else
            return std::as_const(m_handle[index]);
    }
    decltype(auto) at(size_t index) const
    {
        if (index >= size())
            throw std::out_of_range("Span view: index out of range.");
        return (*this)[index];
    }
    decltype(auto) front() const { return (*this)[0]; }
    decltype(auto) back() const { return (*this)[size() - 1]; }

    class const_iterator
    {
        std::span<const Element> m_elements;
        const C* m_context = nullptr;
        size_t m_position = 0;

        bool same_range(const const_iterator& other) const noexcept
        {
            return m_elements.data() == other.m_elements.data() && m_elements.size() == other.m_elements.size() && m_context == other.m_context;
        }

    public:
        using difference_type = std::ptrdiff_t;
        using value_type = std::conditional_t<ViewConcept<Element, C>, ygg::View<Element, C>, Element>;
        using iterator_category = std::random_access_iterator_tag;
        using iterator_concept = std::random_access_iterator_tag;

        const_iterator() noexcept = default;
        const_iterator(std::span<const Element> elements, const C& context, size_t position) noexcept :
            m_elements(elements),
            m_context(&context),
            m_position(position)
        {
        }
        decltype(auto) operator*() const
        {
            assert(m_position < m_elements.size());
            if constexpr (ViewConcept<Element, C>)
                return make_view(m_elements[m_position], *m_context);
            else
                return m_elements[m_position];
        }
        decltype(auto) operator[](difference_type n) const { return *(*this + n); }
        const_iterator& operator++() noexcept
        {
            ++m_position;
            return *this;
        }
        const_iterator operator++(int) noexcept
        {
            auto old = *this;
            ++*this;
            return old;
        }
        const_iterator& operator--() noexcept
        {
            --m_position;
            return *this;
        }
        const_iterator operator--(int) noexcept
        {
            auto old = *this;
            --*this;
            return old;
        }
        const_iterator& operator+=(difference_type n) noexcept
        {
            m_position += n;
            return *this;
        }
        const_iterator& operator-=(difference_type n) noexcept
        {
            m_position -= n;
            return *this;
        }
        friend const_iterator operator+(const_iterator it, difference_type n) noexcept
        {
            it += n;
            return it;
        }
        friend const_iterator operator+(difference_type n, const_iterator it) noexcept { return it + n; }
        friend const_iterator operator-(const_iterator it, difference_type n) noexcept
        {
            it -= n;
            return it;
        }
        friend difference_type operator-(const const_iterator& lhs, const const_iterator& rhs) noexcept
        {
            assert(lhs.same_range(rhs));
            return static_cast<difference_type>(lhs.m_position) - static_cast<difference_type>(rhs.m_position);
        }
        friend bool operator==(const const_iterator& lhs, const const_iterator& rhs) noexcept
        {
            return lhs.same_range(rhs) && lhs.m_position == rhs.m_position;
        }
        friend auto operator<=>(const const_iterator& lhs, const const_iterator& rhs) noexcept
        {
            assert(lhs.same_range(rhs));
            return lhs.m_position <=> rhs.m_position;
        }
    };

    const_iterator begin() const noexcept { return { m_handle, *m_context, 0 }; }
    const_iterator end() const noexcept { return { m_handle, *m_context, size() }; }
    const_iterator cbegin() const noexcept { return begin(); }
    const_iterator cend() const noexcept { return end(); }
};
}  // namespace ygg

namespace std::ranges
{
template<typename T, size_t Extent, typename C>
inline constexpr bool enable_borrowed_range<ygg::View<std::span<T, Extent>, C>> = true;
}

#endif
