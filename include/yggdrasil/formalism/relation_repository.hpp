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

#ifndef YGG_FORMALISM_RELATION_REPOSITORY_HPP_
#define YGG_FORMALISM_RELATION_REPOSITORY_HPP_

#include <boost/mp11.hpp>
#include <urd/elements/flat.hpp>
#include "yggdrasil/formalism/basic_relation_repository.hpp"
#include "yggdrasil/formalism/declarations.hpp"

#include <type_traits>
#include <utility>
#include <urd/config.hpp>

namespace ygg::formalism
{
template<typename ObjectTag, typename... Ts>
class RelationRepository;

template<typename ObjectTag, typename... Ts>
class ConcurrentRelationRepository;

namespace detail
{
template<typename Repository, typename ObjectTag, bool ThreadSafe, typename... Ts>
class RelationRepositoryBase : private BasicRelationRepository<ObjectTag, Ts, ThreadSafe>...
{
private:
    template<typename T, typename...>
    struct FirstType
    {
        using type = T;
    };

    size_t m_index;

public:
    using object_tag = ObjectTag;
    using RelationTypes = boost::mp11::mp_list<Ts...>;
    using container_type = typename BasicRelationRepository<ObjectTag, typename FirstType<Ts...>::type, ThreadSafe>::container_type;
    static constexpr bool thread_safe = ThreadSafe;

    template<typename T>
        requires SupportsRelation<RelationRepositoryBase, T>
    [[nodiscard]] size_t size(urd::index<T> g) const noexcept
    { return get<T>().size(g); }

    /**
     * Local methods access only the current repository layer.
     * Handle-producing methods return raw handles because the caller already knows the context.
     */

    template<typename T>
        requires SupportsRelation<RelationRepositoryBase, T>
    BasicRelationRepository<ObjectTag, T, ThreadSafe>& get() noexcept
    { return static_cast<BasicRelationRepository<ObjectTag, T, ThreadSafe>&>(*this); }

    template<typename T>
        requires SupportsRelation<RelationRepositoryBase, T>
    [[nodiscard]] const BasicRelationRepository<ObjectTag, T, ThreadSafe>& get() const noexcept
    { return static_cast<const BasicRelationRepository<ObjectTag, T, ThreadSafe>&>(*this); }

    template<typename T>
        requires SupportsRelation<RelationRepositoryBase, T>
    [[nodiscard]] auto find_local_with_hash(const urd::data<RelationBinding<T, ObjectTag>>& builder, size_t h) const
    { return get<T>().find_local_with_hash(builder, h); }

    template<typename T>
        requires SupportsRelation<RelationRepositoryBase, T>
    auto find_local(const urd::data<RelationBinding<T, ObjectTag>>& builder) const
    { return get<T>().find_local(builder); }

    template<typename T>
        requires SupportsRelation<RelationRepositoryBase, T>
    auto insert_local_with_hash(const urd::data<RelationBinding<T, ObjectTag>>& builder, size_t h)
    { return get<T>().insert_local_with_hash(builder, h); }

    template<typename T>
        requires SupportsRelation<RelationRepositoryBase, T>
    std::pair<urd::index<Row>, bool> create_local_with_hash(const urd::data<RelationBinding<T, ObjectTag>>& builder, size_t h)
    { return get<T>().create_local_with_hash(builder, h); }

    template<typename T>
        requires SupportsRelation<RelationRepositoryBase, T>
    auto insert_local(const urd::data<RelationBinding<T, ObjectTag>>& builder)
    { return get<T>().insert_local(builder); }

    template<typename T>
        requires SupportsRelation<RelationRepositoryBase, T>
    [[nodiscard]] auto at_local(ygg::formalism::RelationBindingIndex<T, ObjectTag> index) const
    { return get<T>().at_local(index); }

    template<typename T>
        requires SupportsRelation<RelationRepositoryBase, T>
    auto front_local(urd::index<T> g) const
    { return get<T>().front_local(g); }

    template<typename T>
        requires SupportsRelation<RelationRepositoryBase, T>
    size_t local_size(urd::index<T> g) const noexcept
    { return get<T>().local_size(g); }

    template<typename T>
        requires SupportsRelation<RelationRepositoryBase, T>
    size_t parent_size(urd::index<T> g) const noexcept
    { return get<T>().parent_size(g); }

    template<typename T>
        requires SupportsRelation<RelationRepositoryBase, T>
    [[nodiscard]] bool is_local(ygg::formalism::RelationBindingIndex<T, ObjectTag> index) const noexcept
    { return get<T>().is_local(index); }

    template<typename T>
        requires SupportsRelation<RelationRepositoryBase, T>
    [[nodiscard]] bool exists_parent_mutation(urd::index<T> g) const noexcept
    { return get<T>().exists_parent_mutation(g); }

    template<typename T>
        requires SupportsRelation<RelationRepositoryBase, T>
    [[nodiscard]] size_t memory_usage() const noexcept
    { return get<T>().memory_usage(); }

    /**
     * Common methods do not depend on lookup scope.
     */

    /// Parent layers must remain frozen and outlive this repository.
    explicit RelationRepositoryBase(size_t index, const Repository* parent = nullptr, urd::bit_width object_index_width = default_object_index_width) :
        BasicRelationRepository<ObjectTag, Ts, ThreadSafe>(parent ? &parent->template get<Ts>() : nullptr, object_index_width)...,
        m_index(index)
    {
    }

    ~RelationRepositoryBase() = default;
    RelationRepositoryBase(const RelationRepositoryBase&) = delete;
    RelationRepositoryBase& operator=(const RelationRepositoryBase&) = delete;
    RelationRepositoryBase(RelationRepositoryBase&&) = delete;
    RelationRepositoryBase& operator=(RelationRepositoryBase&&) = delete;

    [[nodiscard]] const auto& get_index() const noexcept { return m_index; }
    /// The repository's number, as urd's views ask of a context.
    [[nodiscard]] std::size_t layer() const noexcept { return m_index; }
    [[nodiscard]] std::uint8_t get_object_index_width() const noexcept
    { return this->template get<typename FirstType<Ts...>::type>().get_object_index_width(); }

    void clear() noexcept { (this->template get<Ts>().clear(), ...); }

    template<typename T>
        requires SupportsRelation<RelationRepositoryBase, T>
    static size_t hash(const urd::data<RelationBinding<T, ObjectTag>>& builder) noexcept
    { return BasicRelationRepository<ObjectTag, T, ThreadSafe>::hash(builder); }
};
}  // namespace detail

template<typename ObjectTag, typename... Ts>
class RelationRepository : public detail::RelationRepositoryBase<RelationRepository<ObjectTag, Ts...>, ObjectTag, false, Ts...>
{
    using Base = detail::RelationRepositoryBase<RelationRepository<ObjectTag, Ts...>, ObjectTag, false, Ts...>;

public:
    explicit RelationRepository(size_t index, const RelationRepository* parent = nullptr, urd::bit_width object_index_width = default_object_index_width) :
        Base(index, parent, object_index_width)
    {
    }
};

template<typename ObjectTag, typename... Ts>
class ConcurrentRelationRepository : public detail::RelationRepositoryBase<ConcurrentRelationRepository<ObjectTag, Ts...>, ObjectTag, true, Ts...>
{
    using Base = detail::RelationRepositoryBase<ConcurrentRelationRepository<ObjectTag, Ts...>, ObjectTag, true, Ts...>;

public:
    explicit ConcurrentRelationRepository(size_t index,
                                          const ConcurrentRelationRepository* parent = nullptr,
                                          urd::bit_width object_index_width = default_object_index_width) :
        Base(index, parent, object_index_width)
    {
    }
};
}  // namespace ygg::formalism

#endif
