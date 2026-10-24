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

#ifndef YGG_CONTAINERS_ARRAY_HPP_
#define YGG_CONTAINERS_ARRAY_HPP_

#include "yggdrasil/containers/bit_packed_array_pool.hpp"
#include "yggdrasil/containers/block_array_pool.hpp"
#include "yggdrasil/core/types_utils.hpp"

#include <array>
#include <cista/containers/array.h>
#include <compare>
#include <concepts>
#include <cstddef>
#include <iterator>
#include <span>
#include <type_traits>

namespace ygg
{

/// The read-only iterator of a container.
template<typename Container>
using ContainerConstIterator = typename Container::const_iterator;

template<typename T, size_t N, typename C>
class View<std::array<T, N>, C>
{
private:
    using Container = std::array<T, N>;

public:
    View(const Container& handle, const C& context) noexcept : m_context(&context), m_handle(&handle) {}

    [[nodiscard]] size_t size() const noexcept { return get_data().size(); }
    [[nodiscard]] bool empty() const noexcept { return get_data().empty(); }

    decltype(auto) operator[](size_t i) const
    {
        if constexpr (ViewConcept<T, C>)
            return make_view(get_data()[i], get_context());
        else
            return get_data()[i];
    }

    [[nodiscard]] decltype(auto) at(size_t i) const
    {
        if constexpr (ViewConcept<T, C>)
            return make_view(get_data().at(i), get_context());
        else
            return get_data().at(i);
    }

    [[nodiscard]] decltype(auto) front() const
    {
        if constexpr (ViewConcept<T, C>)
            return make_view(get_data().front(), get_context());
        else
            return get_data().front();
    }

    [[nodiscard]] decltype(auto) back() const
    {
        if constexpr (ViewConcept<T, C>)
            return make_view(get_data().back(), get_context());
        else
            return get_data().back();
    }

    struct const_iterator
    {
        const C* ctx;
        typename std::span<const T>::iterator it;

        using difference_type = std::ptrdiff_t;
        using value_type = ViewOrValue<T, C>;
        using iterator_category = std::random_access_iterator_tag;
        using iterator_concept = std::random_access_iterator_tag;

        const_iterator() noexcept : ctx(nullptr), it() {}
        const_iterator(typename std::span<const T>::iterator it, const C& ctx) noexcept : ctx(&ctx), it(it) {}

        decltype(auto) operator*() const
        {
            if constexpr (ViewConcept<T, C>)
                return make_view(*it, *ctx);
            else
                return *it;
        }

        const_iterator& operator++() noexcept
        {
            ++it;
            return *this;
        }
        const_iterator operator++(int) noexcept
        {
            auto tmp = *this;
            ++(*this);
            return tmp;
        }

        const_iterator& operator--() noexcept
        {
            --it;
            return *this;
        }
        const_iterator operator--(int) noexcept
        {
            auto tmp = *this;
            --(*this);
            return tmp;
        }

        const_iterator& operator+=(difference_type n) noexcept
        {
            it += n;
            return *this;
        }
        const_iterator& operator-=(difference_type n) noexcept
        {
            it -= n;
            return *this;
        }

        friend const_iterator operator+(const_iterator it, difference_type n) noexcept
        {
            it += n;
            return it;
        }

        friend const_iterator operator+(difference_type n, const_iterator it) noexcept
        {
            it += n;
            return it;
        }

        friend const_iterator operator-(const_iterator it, difference_type n) noexcept
        {
            it -= n;
            return it;
        }

        friend difference_type operator-(const_iterator lhs, const_iterator rhs) noexcept { return lhs.it - rhs.it; }

        decltype(auto) operator[](difference_type n) const
        {
            if constexpr (ViewConcept<T, C>)
                return make_view(it[n], *ctx);
            else
                return it[n];
        }

        friend bool operator==(const const_iterator& lhs, const const_iterator& rhs) noexcept { return lhs.it == rhs.it; }
        friend std::strong_ordering operator<=>(const const_iterator& lhs, const const_iterator& rhs) noexcept { return lhs.it <=> rhs.it; }
    };

    [[nodiscard]] const_iterator begin() const noexcept { return const_iterator { std::span<const T>(get_data()).begin(), get_context() }; }

    [[nodiscard]] const_iterator end() const noexcept { return const_iterator { std::span<const T>(get_data()).end(), get_context() }; }

    [[nodiscard]] const_iterator cbegin() const noexcept { return begin(); }

    [[nodiscard]] const_iterator cend() const noexcept { return end(); }

    [[nodiscard]] const auto& get_data() const noexcept { return *m_handle; }
    [[nodiscard]] const auto& get_context() const noexcept { return *m_context; }
    [[nodiscard]] const auto& get_handle() const noexcept { return *m_handle; }

private:
    const C* m_context;
    const Container* m_handle;
};

template<std::unsigned_integral Block, bit::BlockCoder<std::remove_const_t<Block>> Coder, bool ThreadSafe, typename C>
class View<BasicBitPackedArrayView<Block, Coder, ThreadSafe>, C>
{
private:
    using Container = BasicBitPackedArrayView<Block, Coder, ThreadSafe>;
    using T = bit::CoderValue<Coder>;

public:
    View(const Container& handle, const C& context) noexcept : m_context(&context), m_handle(handle) {}

    [[nodiscard]] size_t size() const noexcept { return get_data().size(); }
    [[nodiscard]] bool empty() const noexcept { return get_data().empty(); }

    decltype(auto) operator[](size_t i) const
    {
        if constexpr (ViewConcept<T, C>)
            return make_view(get_data()[i], get_context());
        else
            return get_data()[i];
    }

    [[nodiscard]] decltype(auto) at(size_t i) const
    {
        if constexpr (ViewConcept<T, C>)
            return make_view(get_data().at(i), get_context());
        else
            return get_data().at(i);
    }

    [[nodiscard]] decltype(auto) front() const
    {
        if constexpr (ViewConcept<T, C>)
            return make_view(get_data().front(), get_context());
        else
            return get_data().front();
    }

    [[nodiscard]] decltype(auto) back() const
    {
        if constexpr (ViewConcept<T, C>)
            return make_view(get_data().back(), get_context());
        else
            return get_data().back();
    }

    struct const_iterator
    {
        const C* ctx;
        ContainerConstIterator<Container> it;

        using difference_type = std::ptrdiff_t;
        using value_type = ViewOrValue<T, C>;
        using iterator_category = std::random_access_iterator_tag;
        using iterator_concept = std::random_access_iterator_tag;

        const_iterator() noexcept : ctx(nullptr), it() {}
        const_iterator(ContainerConstIterator<Container> it, const C& ctx) noexcept : ctx(&ctx), it(it) {}

        decltype(auto) operator*() const
        {
            if constexpr (ViewConcept<T, C>)
                return make_view(*it, *ctx);
            else
                return *it;
        }

        // ++
        const_iterator& operator++() noexcept
        {
            ++it;
            return *this;
        }
        const_iterator operator++(int) noexcept
        {
            auto tmp = *this;
            ++(*this);
            return tmp;
        }

        // --
        const_iterator& operator--() noexcept
        {
            --it;
            return *this;
        }
        const_iterator operator--(int) noexcept
        {
            auto tmp = *this;
            --(*this);
            return tmp;
        }

        // += / -=
        const_iterator& operator+=(difference_type n) noexcept
        {
            it += n;
            return *this;
        }
        const_iterator& operator-=(difference_type n) noexcept
        {
            it -= n;
            return *this;
        }

        // + / -
        friend const_iterator operator+(const_iterator it, difference_type n) noexcept
        {
            it += n;
            return it;
        }

        friend const_iterator operator+(difference_type n, const_iterator it) noexcept
        {
            it += n;
            return it;
        }

        friend const_iterator operator-(const_iterator it, difference_type n) noexcept
        {
            it -= n;
            return it;
        }

        // iterator - iterator
        friend difference_type operator-(const_iterator lhs, const_iterator rhs) noexcept { return lhs.it - rhs.it; }

        // []
        auto operator[](difference_type n) const { return *(*this + n); }

        friend bool operator==(const const_iterator& lhs, const const_iterator& rhs) noexcept { return lhs.it == rhs.it; }
        friend std::strong_ordering operator<=>(const const_iterator& lhs, const const_iterator& rhs) noexcept { return lhs.it <=> rhs.it; }
    };

    [[nodiscard]] const_iterator begin() const noexcept { return const_iterator { get_data().begin(), get_context() }; }

    [[nodiscard]] const_iterator end() const noexcept { return const_iterator { get_data().end(), get_context() }; }

    [[nodiscard]] const_iterator cbegin() const noexcept { return begin(); }

    [[nodiscard]] const_iterator cend() const noexcept { return end(); }

    [[nodiscard]] const auto& get_data() const noexcept { return m_handle; }
    [[nodiscard]] const auto& get_context() const noexcept { return *m_context; }
    [[nodiscard]] const auto& get_handle() const noexcept { return m_handle; }

private:
    const C* m_context;
    Container m_handle;
};

template<std::unsigned_integral Block, bit::BlockCoder<std::remove_const_t<Block>> Coder, typename C>
class View<BasicBlockArrayView<Block, Coder>, C>
{
private:
    using Container = BasicBlockArrayView<Block, Coder>;
    using T = bit::CoderValue<Coder>;

public:
    View(const Container& handle, const C& context) noexcept : m_context(&context), m_handle(handle) {}

    [[nodiscard]] size_t size() const noexcept { return get_data().size(); }
    [[nodiscard]] bool empty() const noexcept { return get_data().empty(); }

    decltype(auto) operator[](size_t i) const
    {
        if constexpr (ViewConcept<T, C>)
            return make_view(get_data()[i], get_context());
        else
            return get_data()[i];
    }

    [[nodiscard]] decltype(auto) at(size_t i) const
    {
        if constexpr (ViewConcept<T, C>)
            return make_view(get_data().at(i), get_context());
        else
            return get_data().at(i);
    }

    [[nodiscard]] decltype(auto) front() const
    {
        if constexpr (ViewConcept<T, C>)
            return make_view(get_data().front(), get_context());
        else
            return get_data().front();
    }

    [[nodiscard]] decltype(auto) back() const
    {
        if constexpr (ViewConcept<T, C>)
            return make_view(get_data().back(), get_context());
        else
            return get_data().back();
    }

    struct const_iterator
    {
        const C* ctx;
        ContainerConstIterator<Container> it;

        using difference_type = std::ptrdiff_t;
        using value_type = ViewOrValue<T, C>;
        using iterator_category = std::random_access_iterator_tag;
        using iterator_concept = std::random_access_iterator_tag;

        const_iterator() noexcept : ctx(nullptr), it() {}
        const_iterator(ContainerConstIterator<Container> it, const C& ctx) noexcept : ctx(&ctx), it(it) {}

        decltype(auto) operator*() const
        {
            if constexpr (ViewConcept<T, C>)
                return make_view(*it, *ctx);
            else
                return *it;
        }

        // ++
        const_iterator& operator++() noexcept
        {
            ++it;
            return *this;
        }
        const_iterator operator++(int) noexcept
        {
            auto tmp = *this;
            ++(*this);
            return tmp;
        }

        // --
        const_iterator& operator--() noexcept
        {
            --it;
            return *this;
        }
        const_iterator operator--(int) noexcept
        {
            auto tmp = *this;
            --(*this);
            return tmp;
        }

        // += / -=
        const_iterator& operator+=(difference_type n) noexcept
        {
            it += n;
            return *this;
        }
        const_iterator& operator-=(difference_type n) noexcept
        {
            it -= n;
            return *this;
        }

        // + / -
        friend const_iterator operator+(const_iterator it, difference_type n) noexcept
        {
            it += n;
            return it;
        }

        friend const_iterator operator+(difference_type n, const_iterator it) noexcept
        {
            it += n;
            return it;
        }

        friend const_iterator operator-(const_iterator it, difference_type n) noexcept
        {
            it -= n;
            return it;
        }

        // iterator - iterator
        friend difference_type operator-(const_iterator lhs, const_iterator rhs) noexcept { return lhs.it - rhs.it; }

        // []
        auto operator[](difference_type n) const { return *(*this + n); }

        friend bool operator==(const const_iterator& lhs, const const_iterator& rhs) noexcept { return lhs.it == rhs.it; }
        friend std::strong_ordering operator<=>(const const_iterator& lhs, const const_iterator& rhs) noexcept { return lhs.it <=> rhs.it; }
    };

    [[nodiscard]] const_iterator begin() const noexcept { return const_iterator { get_data().begin(), get_context() }; }

    [[nodiscard]] const_iterator end() const noexcept { return const_iterator { get_data().end(), get_context() }; }

    [[nodiscard]] const_iterator cbegin() const noexcept { return begin(); }

    [[nodiscard]] const_iterator cend() const noexcept { return end(); }

    [[nodiscard]] const auto& get_data() const noexcept { return m_handle; }
    [[nodiscard]] const auto& get_context() const noexcept { return *m_context; }
    [[nodiscard]] const auto& get_handle() const noexcept { return m_handle; }

private:
    const C* m_context;
    Container m_handle;
};

}  // namespace ygg

#endif
