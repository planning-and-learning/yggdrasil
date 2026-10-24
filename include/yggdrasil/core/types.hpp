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

#ifndef YGG_CORE_TYPES_HPP_
#define YGG_CORE_TYPES_HPP_

#include "yggdrasil/core/config.hpp"
#include "yggdrasil/core/dependent_false.hpp"
#include "yggdrasil/semantics/comparison.hpp"

#include <cista/containers/vector.h>
#include <concepts>
#include <limits>
#include <memory>
#include <tuple>
#include <type_traits>

namespace ygg
{

template<typename T>
struct Data;

template<typename T>
using DataList = ::cista::offset::vector<Data<T>>;

template<typename T>
using DataMatrix = ::cista::offset::vector<DataList<T>>;

template<typename T>
struct Builder;

/// The index of a record of type T in its owning repository. Specialize it only for
/// indices that are not a single integer, e.g. formalism::RelationBinding.
template<typename T>
struct Index
{
    using value_type = uint_t;

    value_type value {};

    static constexpr value_type MAX = std::numeric_limits<value_type>::max();

    constexpr Index() noexcept : value(MAX) {}
    constexpr explicit Index(value_type value) noexcept : value(value) {}

    static constexpr Index max() noexcept { return Index(MAX); }

    [[nodiscard]] constexpr bool is_max() const noexcept { return value == MAX; }

    explicit constexpr operator value_type() const noexcept { return value; }

    [[nodiscard]] constexpr value_type get_value() const noexcept { return value; }

    static constexpr auto cista_members() noexcept { return ygg::Members<&Index::value> {}; }
    static constexpr auto identifying_members() noexcept { return ygg::Members<&Index::value> {}; }
};

template<typename T>
using IndexList = ::cista::offset::vector<Index<T>>;

template<typename T>
using IndexMatrix = ::cista::offset::vector<IndexList<T>>;

template<typename T, typename C>
struct View
{
};

template<typename T, typename C>
class IndexViewBase;
template<typename T, typename C>
class DataViewBase;
template<typename T, typename C>
class BuilderViewBase;

/// Whether a context exposes a canonical owner for a handle.
template<typename C, typename T>
concept CanonicalizableContext = requires(const C& context, const T& handle) {
    { context.get_canonical_context(handle) } -> std::same_as<const C&>;
};

/// Domain overloads may select the owner of an embedded handle.
template<typename T, typename C>
const C& get_canonical_context(const T&, const C& context YGG_LIFETIMEBOUND) noexcept
{ return context; }

template<typename T, CanonicalizableContext<T> C>
const C& get_canonical_context(const T& handle, const C& context) noexcept(noexcept(context.get_canonical_context(handle)))
{ return context.get_canonical_context(handle); }

/// Construct the representation using its canonical owner, discovered by ADL.
template<typename T, typename C>
    requires std::constructible_from<View<T, C>, const T&, const C&> && requires(const T& handle, const C& context) {
        { get_canonical_context(handle, context) } -> std::same_as<const C&>;
    }
View<T, C> make_view(const T& handle, const C& context) noexcept(noexcept(get_canonical_context(handle, context))
                                                                 && std::is_nothrow_constructible_v<View<T, C>, const T&, const C&>)
{ return View<T, C>(handle, get_canonical_context(handle, context)); }

/// True if V is a view of handle H in any context.
template<typename V, typename H>
inline constexpr bool is_view_of_v = false;

template<typename H, typename C>
inline constexpr bool is_view_of_v<View<H, C>, H> = true;

template<typename T, typename C>
concept ViewConcept = requires(const T& type, const C& context, const View<T, C>& view) {
    // Constructor
    View<T, C>(type, context);
    // Helper
    { make_view(type, context) } -> std::same_as<View<T, C>>;
    // Method to retrieve the underlying Data.
    view.get_data();
    // Method to retrieve the underlying context.
    { view.get_context() } -> std::same_as<const C&>;
    // Method to retrieve the underlying lightweight handle or data.
    { view.get_handle() } -> std::same_as<const T&>;
};

/// The type a container view yields for a stored T: View<T, C> if T is viewable in C, else T itself.
template<typename T, typename C>
using ViewOrValue = std::conditional_t<ViewConcept<T, C>, View<T, C>, T>;

// Storage decision: trivially copyable data types are treated as flat in-memory
// values.
template<typename T>
inline constexpr bool uses_trivial_storage_v = std::is_trivially_copyable_v<Data<T>> && std::is_default_constructible_v<Data<T>>;

/// Whether views of a record hold it by value rather than borrowing it. Defaults to
/// trivially copyable records; small records built on the fly may opt in by specialization.
template<typename T>
inline constexpr bool stores_view_by_value_v = uses_trivial_storage_v<T>;
}  // namespace ygg

namespace ygg
{
namespace detail
{
/// Views borrow their context; a shared_ptr context is retained by value.
template<typename C>
class ContextStorage
{
    const C* m_context;

public:
    explicit ContextStorage(const C& context) noexcept : m_context(&context) {}
    [[nodiscard]] const C& get() const noexcept { return *m_context; }
    [[nodiscard]] const C& deref() const noexcept { return *m_context; }
};

template<typename R>
class ContextStorage<std::shared_ptr<R>>
{
    std::shared_ptr<R> m_context;

public:
    explicit ContextStorage(std::shared_ptr<R> context) noexcept : m_context(std::move(context)) {}
    [[nodiscard]] const std::shared_ptr<R>& get() const noexcept { return m_context; }
    [[nodiscard]] const R& deref() const noexcept { return *m_context; }
};

/// Views borrow their record; records with stores_view_by_value_v are copied, so views of
/// records computed on the fly stay valid.
template<typename T, bool = stores_view_by_value_v<T>>
class DataStorage
{
    const Data<T>* m_data;

public:
    explicit DataStorage(const Data<T>& data) noexcept : m_data(&data) {}
    [[nodiscard]] const Data<T>& get() const noexcept { return *m_data; }
    [[nodiscard]] const Data<T>& record() const noexcept { return *m_data; }
};

template<typename T>
class DataStorage<T, true>
{
    Data<T> m_data;

public:
    explicit DataStorage(const Data<T>& data) noexcept(std::is_nothrow_copy_constructible_v<Data<T>>) : m_data(data) {}
    [[nodiscard]] const Data<T>& get() const noexcept { return m_data; }
    [[nodiscard]] const Data<T>& record() const noexcept { return m_data; }
};

/// Views borrow the mutable builder; a builder that owns a record exposes it as the view's record.
template<typename T>
class BuilderStorage
{
    const Builder<T>* m_builder;

public:
    explicit BuilderStorage(const Builder<T>& builder) noexcept : m_builder(&builder) {}
    [[nodiscard]] const Builder<T>& get() const noexcept { return *m_builder; }
    [[nodiscard]] decltype(auto) record() const noexcept
        requires requires(const Builder<T>& builder) { builder.get_data(); }
    { return m_builder->get_data(); }
};

template<typename C>
inline constexpr bool is_shared_ptr_v = false;
template<typename R>
inline constexpr bool is_shared_ptr_v<std::shared_ptr<R>> = true;

/// The repository owning the records viewed in a context, found by ADL. Views call it from
/// here, since their own get_repository member would hide the customization point.
template<typename R>
decltype(auto) owning_repository(const R& context) noexcept
{ return get_repository(context); }

/// Contexts whose owning repository is known through get_repository.
template<typename C>
concept HasRepository = requires(const ContextStorage<C>& context) { get_repository(context.deref()); };

/// Shared contexts are taken by value, borrowed ones by reference.
template<typename C>
using ContextArg = std::conditional_t<is_shared_ptr_v<C>, C, const C&>;
}  // namespace detail

/// Storage of a view of an interned record: its index and context.
template<typename T, typename C>
class IndexViewBase
{
private:
    Index<T> m_handle;
    detail::ContextStorage<C> m_context;

public:
    IndexViewBase(Index<T> handle, detail::ContextArg<C> context) noexcept : m_handle(handle), m_context(std::move(context)) {}

    [[nodiscard]] const C& get_context() const noexcept { return m_context.get(); }
    /// The repository that owns the viewed records, whether the context is borrowed or shared.
    [[nodiscard]] decltype(auto) get_repository() const noexcept
        requires detail::HasRepository<C>
    { return detail::owning_repository(m_context.deref()); }
    [[nodiscard]] const Index<T>& get_handle() const noexcept { return m_handle; }
    /// The record, resolved through the repository that owns it.
    [[nodiscard]] decltype(auto) get_data() const
        requires requires(const IndexViewBase& view) { view.get_repository()[view.m_handle]; }
    { return get_repository()[m_handle]; }
    /// The record, shared with the data and builder views of the same type.
    [[nodiscard]] decltype(auto) get_record() const
        requires requires(const IndexViewBase& view) { view.get_data(); }
    { return get_data(); }
    [[nodiscard]] Index<T> get_index() const noexcept { return m_handle; }
    /// Records compare by index within their owning repository.
    [[nodiscard]] auto identifying_members() const noexcept
        requires requires(const IndexViewBase& view) { view.get_repository().get_index(); }
    { return std::make_tuple(m_handle, get_repository().get_index()); }
};

/// Storage of a view of a record held elsewhere. Records with stores_view_by_value_v are
/// copied, so views of records computed on the fly stay valid; others are borrowed.
template<typename T, typename C>
class DataViewBase
{
private:
    detail::DataStorage<T> m_handle;
    detail::ContextStorage<C> m_context;

public:
    DataViewBase(const Data<T>& handle, detail::ContextArg<C> context) noexcept(std::is_nothrow_constructible_v<detail::DataStorage<T>, const Data<T>&>) :
        m_handle(handle),
        m_context(std::move(context))
    {
    }

    [[nodiscard]] const Data<T>& get_data() const noexcept { return m_handle.get(); }
    [[nodiscard]] const Data<T>& get_record() const noexcept { return m_handle.record(); }
    [[nodiscard]] const C& get_context() const noexcept { return m_context.get(); }
    /// The repository that owns the viewed records, whether the context is borrowed or shared.
    [[nodiscard]] decltype(auto) get_repository() const noexcept
        requires detail::HasRepository<C>
    { return detail::owning_repository(m_context.deref()); }
    [[nodiscard]] const Data<T>& get_handle() const noexcept { return get_data(); }
    /// The record's own index, for records that store one.
    [[nodiscard]] auto get_index() const noexcept
        requires requires(const Data<T>& data) { data.index; }
    { return get_data().index; }
    /// Records compare structurally within their owning repository.
    [[nodiscard]] auto identifying_members() const noexcept
        requires FunctionIdentified<Data<T>> && requires(const DataViewBase& view) {
            view.get_data().identifying_members();
            view.get_repository().get_index();
        }
    { return std::tuple_cat(get_data().identifying_members(), std::make_tuple(get_repository().get_index())); }
    [[nodiscard]] auto identifying_members() const noexcept
        requires MemberIdentified<Data<T>> && requires(const DataViewBase& view) { view.get_repository().get_index(); }
    {
        return []<auto... Ms>(const auto& data, auto index, Members<Ms...>)
        { return std::tuple_cat(std::tie(data.*Ms...), std::make_tuple(index)); }(get_data(), get_repository().get_index(), Data<T>::identifying_members());
    }
};

/// Storage of a borrowed view of a mutable builder; builders have no identity.
template<typename T, typename C>
class BuilderViewBase
{
private:
    detail::BuilderStorage<T> m_handle;
    detail::ContextStorage<C> m_context;

public:
    BuilderViewBase(const Builder<T>& handle, detail::ContextArg<C> context) noexcept : m_handle(handle), m_context(std::move(context)) {}

    [[nodiscard]] const Builder<T>& get_data() const noexcept { return m_handle.get(); }
    /// The builder's record, for builders that own one.
    [[nodiscard]] decltype(auto) get_record() const noexcept
        requires requires(const detail::BuilderStorage<T>& storage) { storage.record(); }
    { return m_handle.record(); }
    [[nodiscard]] const C& get_context() const noexcept { return m_context.get(); }
    /// The repository that owns the viewed records, whether the context is borrowed or shared.
    [[nodiscard]] decltype(auto) get_repository() const noexcept
        requires detail::HasRepository<C>
    { return detail::owning_repository(m_context.deref()); }
    [[nodiscard]] const Builder<T>& get_handle() const noexcept { return m_handle.get(); }
    /// The record's own index, for builders whose record stores one.
    [[nodiscard]] auto get_index() const noexcept
        requires requires(const BuilderViewBase& view) { view.get_record().index; }
    { return get_record().index; }
};

/// Accessors shared by the index, data, and builder views of T. Specialize it to give
/// every default view of T the same domain getters; V is the final view type.
template<typename T, typename V>
struct ViewMixin
{
};

template<typename T, typename C>
struct View<Index<T>, C> : IndexViewBase<T, C>, ViewMixin<T, View<Index<T>, C>>
{
    using IndexViewBase<T, C>::IndexViewBase;
};

template<typename T, typename C>
struct View<Data<T>, C> : DataViewBase<T, C>, ViewMixin<T, View<Data<T>, C>>
{
    using DataViewBase<T, C>::DataViewBase;
};

template<typename T, typename C>
struct View<Builder<T>, C> : BuilderViewBase<T, C>, ViewMixin<T, View<Builder<T>, C>>
{
    using BuilderViewBase<T, C>::BuilderViewBase;
};
}  // namespace ygg

#endif
