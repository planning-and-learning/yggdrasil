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

#ifndef YGG_CONTAINERS_PACKED_OBJECT_ROW_HPP_
#define YGG_CONTAINERS_PACKED_OBJECT_ROW_HPP_

#include "yggdrasil/containers/array.hpp"
#include "yggdrasil/core/bit.hpp"
#include "yggdrasil/core/types.hpp"

#include <algorithm>
#include <cassert>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace ygg
{

/// Layout of a packed object row. Each argument is represented by the position of its object in the argument's domain;
/// an identity field holds the object index itself, i.e., its domain is all indices of the object index width.
/// The row is the mixed-radix rank of the positions, so the rows of a layout are exactly the products of its domains.
template<typename ObjectIndex>
class PackedRowLayout
{
private:
    static constexpr ygg::uint_t kAbsent = std::numeric_limits<ygg::uint_t>::max();
    static constexpr size_t kMaxWidth = std::numeric_limits<ygg::uint_t>::digits;
    static constexpr std::uint64_t kMaxCapacity = std::uint64_t(1) << kMaxWidth;

    struct Field
    {
        std::uint64_t radix;                   ///< Domain size.
        std::uint64_t stride;                  ///< Product of the radixes of the preceding fields.
        bool identity;                         ///< Whether the field holds the object index itself instead of a domain position.
        std::vector<ygg::uint_t> position_of;  ///< Indexed by object.
        std::vector<ObjectIndex> object_at;    ///< Indexed by position.
    };

    /// Appends a field; a capacity beyond kMaxCapacity marks the overflow, which the constructors report.
    /// An empty domain admits no row, so the capacity becomes 0.
    void append_field(std::uint64_t radix, bool identity)
    {
        m_fields.push_back(Field { radix, m_capacity, identity, {}, {} });
        if (radix == 0)
            m_capacity = 0;
        else if (m_capacity > kMaxCapacity / radix)
            m_capacity = kMaxCapacity + 1;
        else
            m_capacity *= radix;
    }

    void check_capacity() const
    {
        if (m_capacity > kMaxCapacity && std::ranges::none_of(m_fields, [](const auto& field) { return field.radix == 0; }))
        {
            auto bits = size_t(0);
            for (const auto& field : m_fields)
                bits += bit::bits_needed(field.radix);
            throw std::length_error("PackedRowLayout: a row of arity " + std::to_string(arity()) + " needs about " + std::to_string(bits)
                                    + " bits, but at most " + std::to_string(kMaxWidth) + " are available.");
        }
    }

public:
    /// Identity fields of the given object index width.
    PackedRowLayout(size_t arity, std::uint8_t object_index_width) : m_fields(), m_capacity(1)
    {
        const auto radix = object_index_width >= kMaxWidth ? kMaxCapacity : std::uint64_t(1) << object_index_width;
        for (size_t j = 0; j < arity; ++j)
            append_field(radix, true);
        check_capacity();
    }

    /// One field per sorted, duplicate-free argument domain.
    explicit PackedRowLayout(std::span<const std::vector<ObjectIndex>> domains) : m_fields(), m_capacity(1)
    {
        for (const auto& domain : domains)
        {
            append_field(domain.size(), false);
            auto& field = m_fields.back();
            field.object_at = domain;
            for (size_t position = 0; position < domain.size(); ++position)
            {
                const auto object = ygg::uint_t(domain[position]);
                if (object >= field.position_of.size())
                    field.position_of.resize(object + 1, kAbsent);
                field.position_of[object] = static_cast<ygg::uint_t>(position);
            }
        }
        check_capacity();
    }

    size_t arity() const noexcept { return m_fields.size(); }

    /// The packed row of the objects, or nullopt if an object lies outside its argument's domain.
    template<typename Objects>
    std::optional<ygg::uint_t> encode(const Objects& objects) const noexcept
    {
        assert(objects.size() == arity());
        auto row = std::uint64_t(0);
        auto j = size_t(0);
        for (const auto object_index : objects)
        {
            const auto& field = m_fields[j++];
            const auto object = ygg::uint_t(object_index);
            const auto position = field.identity ? object : object < field.position_of.size() ? field.position_of[object] : kAbsent;
            if (position == kAbsent || position >= field.radix)
                return std::nullopt;
            row += position * field.stride;
        }
        return static_cast<ygg::uint_t>(row);
    }

    ObjectIndex decode(ygg::uint_t row, size_t j) const noexcept
    {
        const auto& field = m_fields[j];
        const auto position = static_cast<ygg::uint_t>((row / field.stride) % field.radix);
        return field.identity ? ObjectIndex(position) : field.object_at[position];
    }

    /// Number of distinct rows the layout can represent.
    size_t capacity() const noexcept { return m_capacity; }

    size_t memory_usage() const noexcept
    {
        auto bytes = m_fields.capacity() * sizeof(Field);
        for (const auto& field : m_fields)
            bytes += field.position_of.capacity() * sizeof(ygg::uint_t) + field.object_at.capacity() * sizeof(ObjectIndex);
        return bytes;
    }

private:
    std::vector<Field> m_fields;
    std::uint64_t m_capacity;
};

/// The objects of a packed row, decoded on access.
template<typename ObjectIndex>
class PackedObjectRow
{
public:
    using value_type = ObjectIndex;

    PackedObjectRow(ygg::uint_t row, const PackedRowLayout<ObjectIndex>& layout) noexcept : m_row(row), m_layout(&layout) {}

    size_t size() const noexcept { return m_layout->arity(); }
    bool empty() const noexcept { return size() == 0; }

    ObjectIndex operator[](size_t j) const noexcept { return m_layout->decode(m_row, j); }

    ObjectIndex at(size_t j) const
    {
        if (j >= size())
            throw std::out_of_range("PackedObjectRow::at: index out of range.");
        return (*this)[j];
    }

    ObjectIndex front() const noexcept { return (*this)[0]; }
    ObjectIndex back() const noexcept { return (*this)[size() - 1]; }

    struct const_iterator
    {
        ygg::uint_t row;
        const PackedRowLayout<ObjectIndex>* layout;
        std::ptrdiff_t j;

        using difference_type = std::ptrdiff_t;
        using value_type = ObjectIndex;
        using iterator_category = std::random_access_iterator_tag;
        using iterator_concept = std::random_access_iterator_tag;

        const_iterator() noexcept : row(0), layout(nullptr), j(0) {}
        const_iterator(ygg::uint_t row, const PackedRowLayout<ObjectIndex>& layout, std::ptrdiff_t j) noexcept : row(row), layout(&layout), j(j) {}

        ObjectIndex operator*() const noexcept { return layout->decode(row, size_t(j)); }
        ObjectIndex operator[](difference_type n) const noexcept { return layout->decode(row, size_t(j + n)); }

        const_iterator& operator++() noexcept
        {
            ++j;
            return *this;
        }
        const_iterator operator++(int) noexcept
        {
            auto tmp = *this;
            ++j;
            return tmp;
        }
        const_iterator& operator--() noexcept
        {
            --j;
            return *this;
        }
        const_iterator operator--(int) noexcept
        {
            auto tmp = *this;
            --j;
            return tmp;
        }
        const_iterator& operator+=(difference_type n) noexcept
        {
            j += n;
            return *this;
        }
        const_iterator& operator-=(difference_type n) noexcept
        {
            j -= n;
            return *this;
        }

        friend const_iterator operator+(const_iterator it, difference_type n) noexcept { return it += n; }
        friend const_iterator operator+(difference_type n, const_iterator it) noexcept { return it += n; }
        friend const_iterator operator-(const_iterator it, difference_type n) noexcept { return it -= n; }
        friend difference_type operator-(const_iterator lhs, const_iterator rhs) noexcept { return lhs.j - rhs.j; }

        friend bool operator==(const const_iterator& lhs, const const_iterator& rhs) noexcept { return lhs.j == rhs.j; }
        friend std::strong_ordering operator<=>(const const_iterator& lhs, const const_iterator& rhs) noexcept { return lhs.j <=> rhs.j; }
    };

    const_iterator begin() const noexcept { return const_iterator(m_row, *m_layout, 0); }
    const_iterator end() const noexcept { return const_iterator(m_row, *m_layout, std::ptrdiff_t(size())); }

    ygg::uint_t get_row() const noexcept { return m_row; }

private:
    ygg::uint_t m_row;
    const PackedRowLayout<ObjectIndex>* m_layout;
};

template<typename ObjectIndex, typename C>
class View<PackedObjectRow<ObjectIndex>, C> : public detail::OwningRangeView<PackedObjectRow<ObjectIndex>, ObjectIndex, C>
{
public:
    using detail::OwningRangeView<PackedObjectRow<ObjectIndex>, ObjectIndex, C>::OwningRangeView;
};

}  // namespace ygg

#endif
