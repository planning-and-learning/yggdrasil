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

#ifndef YGG_FORMALISM_BINDING_VIEW_HPP_
#define YGG_FORMALISM_BINDING_VIEW_HPP_

#include "yggdrasil/containers/span.hpp"
#include "yggdrasil/containers/vector.hpp"
#include "yggdrasil/core/types.hpp"
#include "yggdrasil/formalism/binding_data.hpp"
#include "yggdrasil/formalism/binding_index.hpp"
#include "yggdrasil/formalism/declarations.hpp"
#include "yggdrasil/formalism/object_index.hpp"
#include "yggdrasil/semantics/containers/block_array_ordering.hpp"

#include <iterator>
#include <ranges>
#include <span>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

namespace ygg::formalism::detail
{
/// Borrows the row sequence and context, independently of its binding-range wrapper.
/// Comparing or subtracting iterators requires that they belong to the same sequence.
template<typename RelationTag, typename ObjectTag, std::forward_iterator BaseIterator, typename C>
class RelationBindingIterator
{
    const C* m_context = nullptr;
    BaseIterator m_it {};
    Index<RelationTag> m_relation {};

public:
    using difference_type = std::iter_difference_t<BaseIterator>;
    using value_type = ygg::View<Index<RelationBinding<RelationTag, ObjectTag>>, C>;
    using reference = value_type;
    using pointer = void;
    using iterator_concept =
        std::conditional_t<std::random_access_iterator<BaseIterator>,
                           std::random_access_iterator_tag,
                           std::conditional_t<std::bidirectional_iterator<BaseIterator>, std::bidirectional_iterator_tag, std::forward_iterator_tag>>;
    using iterator_category = iterator_concept;

    RelationBindingIterator() = default;
    RelationBindingIterator(Index<RelationTag> relation, BaseIterator it, const C& context) : m_context(&context), m_it(std::move(it)), m_relation(relation) {}

    reference operator*() const { return ygg::make_view(Index<RelationBinding<RelationTag, ObjectTag>> { m_relation, *m_it }, *m_context); }
    RelationBindingIterator& operator++()
    {
        ++m_it;
        return *this;
    }
    RelationBindingIterator operator++(int)
    {
        auto old = *this;
        ++*this;
        return old;
    }
    RelationBindingIterator& operator--()
        requires std::bidirectional_iterator<BaseIterator>
    {
        --m_it;
        return *this;
    }
    RelationBindingIterator operator--(int)
        requires std::bidirectional_iterator<BaseIterator>
    {
        auto old = *this;
        --*this;
        return old;
    }
    RelationBindingIterator& operator+=(difference_type n)
        requires std::random_access_iterator<BaseIterator>
    {
        m_it += n;
        return *this;
    }
    RelationBindingIterator& operator-=(difference_type n)
        requires std::random_access_iterator<BaseIterator>
    {
        m_it -= n;
        return *this;
    }
    friend RelationBindingIterator operator+(RelationBindingIterator it, difference_type n)
        requires std::random_access_iterator<BaseIterator>
    {
        return it += n;
    }
    friend RelationBindingIterator operator+(difference_type n, RelationBindingIterator it)
        requires std::random_access_iterator<BaseIterator>
    {
        return it += n;
    }
    friend RelationBindingIterator operator-(RelationBindingIterator it, difference_type n)
        requires std::random_access_iterator<BaseIterator>
    {
        return it -= n;
    }
    friend difference_type operator-(const RelationBindingIterator& lhs, const RelationBindingIterator& rhs)
        requires std::random_access_iterator<BaseIterator>
    {
        return lhs.m_it - rhs.m_it;
    }
    reference operator[](difference_type n) const
        requires std::random_access_iterator<BaseIterator>
    {
        return *(*this + n);
    }
    friend bool operator==(const RelationBindingIterator& lhs, const RelationBindingIterator& rhs) { return lhs.m_it == rhs.m_it; }
    friend bool operator<(const RelationBindingIterator& lhs, const RelationBindingIterator& rhs)
        requires std::random_access_iterator<BaseIterator>
    {
        return lhs.m_it < rhs.m_it;
    }
    friend bool operator>(const RelationBindingIterator& lhs, const RelationBindingIterator& rhs)
        requires std::random_access_iterator<BaseIterator>
    {
        return rhs < lhs;
    }
    friend bool operator<=(const RelationBindingIterator& lhs, const RelationBindingIterator& rhs)
        requires std::random_access_iterator<BaseIterator>
    {
        return !(rhs < lhs);
    }
    friend bool operator>=(const RelationBindingIterator& lhs, const RelationBindingIterator& rhs)
        requires std::random_access_iterator<BaseIterator>
    {
        return !(lhs < rhs);
    }
};
}  // namespace ygg::formalism::detail

namespace ygg
{

template<typename RelationTag, typename ObjectTag, typename C>
class View<Index<ygg::formalism::RelationBinding<RelationTag, ObjectTag>>, C>
{
private:
    const C* m_context;
    Index<ygg::formalism::RelationBinding<RelationTag, ObjectTag>> m_handle;

public:
    View(Index<ygg::formalism::RelationBinding<RelationTag, ObjectTag>> handle, const C& context) noexcept : m_context(&context), m_handle(handle) {}

    // This will return an ArrayView already
    auto get_data() const noexcept
        requires formalism::RelationContextFor<C, RelationTag, ObjectTag>
    {
        return get_repository(*m_context)[m_handle];
    }
    const auto& get_context() const noexcept { return *m_context; }
    const auto& get_handle() const noexcept { return m_handle; }

    auto get_index() const noexcept { return m_handle; }
    auto get_relation() const noexcept { return ygg::make_view(m_handle.relation, *m_context); }
    auto get_objects() const noexcept { return ygg::make_view(get_data(), *m_context); }
    // Use the relation index rather than its view: view identity includes the repository,
    // while this key represents the logical binding across repositories.
    auto get_key() const noexcept { return std::make_pair(m_handle.relation, get_data()); }

    auto identifying_members() const noexcept { return std::make_tuple(m_handle, m_context->get_index()); }
};

/// Borrows a complete binding without publishing a row identity. Data and context
/// must outlive the view and all object ranges; keep the data unchanged while used.
template<typename RelationTag, typename ObjectTag, typename C>
class View<Data<formalism::RelationBinding<RelationTag, ObjectTag>>, C>
{
    using BindingData = Data<formalism::RelationBinding<RelationTag, ObjectTag>>;
    const BindingData* m_handle;
    const C* m_context;

    auto objects() const noexcept { return std::span<const Index<formalism::Object<ObjectTag>>>(m_handle->objects.data(), m_handle->objects.size()); }

public:
    View(const BindingData& handle, const C& context) noexcept : m_handle(&handle), m_context(&context) {}

    const BindingData& get_data() const noexcept { return *m_handle; }
    const BindingData& get_handle() const noexcept { return *m_handle; }
    const C& get_context() const noexcept { return *m_context; }
    auto get_relation() const noexcept { return make_view(m_handle->relation, *m_context); }
    auto get_objects() const noexcept { return make_view(objects(), *m_context); }
    auto get_key() const noexcept { return std::make_pair(m_handle->relation, objects()); }
};

template<typename RelationTag, typename ObjectTag, typename BindingRange, typename C>
class View<ygg::formalism::RelationBindingsForwardRange<RelationTag, ObjectTag, BindingRange>, C>
{
public:
    using Container = ygg::formalism::RelationBindingsForwardRange<RelationTag, ObjectTag, BindingRange>;
    using T = Index<ygg::formalism::RelationBinding<RelationTag, ObjectTag>>;
    using I1 = Index<RelationTag>;

    View(Container handle, const C& context) noexcept : m_context(&context), m_handle(handle) {}

    bool empty() const { return std::ranges::begin(get_data().rows) == std::ranges::end(get_data().rows); }

    size_t size() const
        requires std::ranges::sized_range<const std::remove_reference_t<BindingRange>>
    {
        return std::ranges::size(get_data().rows);
    }

    decltype(auto) front() const
    {
        ensure_not_empty();
        auto it = std::ranges::begin(get_data().rows);
        return ygg::make_view(T { get_data().relation, *it }, get_context());
    }

    using const_iterator =
        formalism::detail::RelationBindingIterator<RelationTag, ObjectTag, std::ranges::iterator_t<const std::remove_reference_t<BindingRange>>, C>;

    const_iterator begin() const { return const_iterator { get_data().relation, std::ranges::begin(get_data().rows), get_context() }; }

    const_iterator end() const { return const_iterator { get_data().relation, std::ranges::end(get_data().rows), get_context() }; }

    const auto& get_data() const noexcept { return m_handle; }
    const auto& get_context() const noexcept { return *m_context; }
    const auto& get_handle() const noexcept { return m_handle; }

private:
    void ensure_not_empty() const
    {
        if (empty())
            throw std::out_of_range("RelationBindingsForwardRange: range is empty.");
    }

    const C* m_context;
    Container m_handle;
};

template<typename RelationTag, typename ObjectTag, typename BindingRange, typename C>
class View<ygg::formalism::RelationBindingsRandomAccessRange<RelationTag, ObjectTag, BindingRange>, C>
{
public:
    using Container = ygg::formalism::RelationBindingsRandomAccessRange<RelationTag, ObjectTag, BindingRange>;
    using T = Index<ygg::formalism::RelationBinding<RelationTag, ObjectTag>>;
    using I1 = Index<RelationTag>;

    View(Container handle, const C& context) noexcept : m_context(&context), m_handle(handle) {}

    bool empty() const { return std::ranges::begin(get_data().rows) == std::ranges::end(get_data().rows); }

    size_t size() const { return std::ranges::size(get_data().rows); }

    decltype(auto) front() const
    {
        ensure_not_empty();
        auto it = std::ranges::begin(get_data().rows);
        return ygg::make_view(T { get_data().relation, *it }, get_context());
    }

    decltype(auto) back() const
    {
        ensure_not_empty();
        auto it = std::ranges::begin(get_data().rows) + (std::ranges::ssize(get_data().rows) - 1);
        return ygg::make_view(T { get_data().relation, *it }, get_context());
    }

    decltype(auto) operator[](size_t i) const
    {
        auto it = std::ranges::begin(get_data().rows) + static_cast<std::ptrdiff_t>(i);
        return ygg::make_view(T { get_data().relation, *it }, get_context());
    }

    using const_iterator =
        formalism::detail::RelationBindingIterator<RelationTag, ObjectTag, std::ranges::iterator_t<const std::remove_reference_t<BindingRange>>, C>;

    const_iterator begin() const { return const_iterator { get_data().relation, std::ranges::begin(get_data().rows), get_context() }; }

    const_iterator end() const { return const_iterator { get_data().relation, std::ranges::end(get_data().rows), get_context() }; }

    const auto& get_data() const noexcept { return m_handle; }
    const auto& get_context() const noexcept { return *m_context; }
    const auto& get_handle() const noexcept { return m_handle; }

private:
    void ensure_not_empty() const
    {
        if (empty())
            throw std::out_of_range("RelationBindingsRandomAccessRange: range is empty.");
    }

    const C* m_context;
    Container m_handle;
};

}  // namespace ygg

namespace ygg::formalism
{
/// Shared read interface for published and borrowed bindings. Publication identity
/// is deliberately absent; a logical key consists of the relation and object row.
template<typename V, typename RelationTag, typename ObjectTag>
concept RelationBindingViewConcept = requires(const std::remove_reference_t<V>& view) {
    view.get_data();
    view.get_handle();
    view.get_context();
    { view.get_relation().get_index() } -> std::same_as<Index<RelationTag>>;
    { view.get_objects()[size_t {}].get_index() } -> std::same_as<Index<Object<ObjectTag>>>;
    { view.get_objects().get_data() } -> SizedForwardRangeOf<Index<Object<ObjectTag>>>;
    { view.get_key().first } -> SameAsIgnoringCvref<Index<RelationTag>>;
    { view.get_key().second } -> SizedForwardRangeOf<Index<Object<ObjectTag>>>;
};
}  // namespace ygg::formalism

#endif
