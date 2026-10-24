/*
 * Copyright (C) 2026 Dominik Drexler
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef YGG_CORE_DETAILS_TYPES_HPP_
#define YGG_CORE_DETAILS_TYPES_HPP_

#include "yggdrasil/core/types.hpp"

#include <memory>
#include <tuple>
#include <type_traits>

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
    const C& get() const noexcept { return *m_context; }
    const C& deref() const noexcept { return *m_context; }
};

template<typename R>
class ContextStorage<std::shared_ptr<R>>
{
    std::shared_ptr<R> m_context;

public:
    explicit ContextStorage(std::shared_ptr<R> context) noexcept : m_context(std::move(context)) {}
    const std::shared_ptr<R>& get() const noexcept { return m_context; }
    const R& deref() const noexcept { return *m_context; }
};

/// Views borrow their record; records with stores_view_by_value_v are copied, so views of
/// records computed on the fly stay valid.
template<typename T, bool = stores_view_by_value_v<T>>
class DataStorage
{
    const Data<T>* m_data;

public:
    explicit DataStorage(const Data<T>& data) noexcept : m_data(&data) {}
    const Data<T>& get() const noexcept { return *m_data; }
    const Data<T>& record() const noexcept { return *m_data; }
};

template<typename T>
class DataStorage<T, true>
{
    Data<T> m_data;

public:
    explicit DataStorage(const Data<T>& data) noexcept(std::is_nothrow_copy_constructible_v<Data<T>>) : m_data(data) {}
    const Data<T>& get() const noexcept { return m_data; }
    const Data<T>& record() const noexcept { return m_data; }
};

/// Views borrow the mutable builder; a builder that owns a record exposes it as the view's record.
template<typename T>
class BuilderStorage
{
    const Builder<T>* m_builder;

public:
    explicit BuilderStorage(const Builder<T>& builder) noexcept : m_builder(&builder) {}
    const Builder<T>& get() const noexcept { return *m_builder; }
    decltype(auto) record() const noexcept
        requires requires(const Builder<T>& builder) { builder.get_data(); }
    {
        return m_builder->get_data();
    }
};

template<typename C>
inline constexpr bool is_shared_ptr_v = false;
template<typename R>
inline constexpr bool is_shared_ptr_v<std::shared_ptr<R>> = true;

/// The repository owning the records viewed in a context, found by ADL. Views call it from
/// here, since their own get_repository member would hide the customization point.
template<typename R>
decltype(auto) owning_repository(const R& context) noexcept
{
    return get_repository(context);
}

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

    const C& get_context() const noexcept { return m_context.get(); }
    /// The repository that owns the viewed records, whether the context is borrowed or shared.
    decltype(auto) get_repository() const noexcept
        requires detail::HasRepository<C>
    {
        return detail::owning_repository(m_context.deref());
    }
    const Index<T>& get_handle() const noexcept { return m_handle; }
    /// The record, resolved through the repository that owns it.
    decltype(auto) get_data() const
        requires requires(const IndexViewBase& view) { view.get_repository()[view.m_handle]; }
    {
        return get_repository()[m_handle];
    }
    /// The record, shared with the data and builder views of the same type.
    decltype(auto) get_record() const
        requires requires(const IndexViewBase& view) { view.get_data(); }
    {
        return get_data();
    }
    Index<T> get_index() const noexcept { return m_handle; }
    /// Records compare by index within their owning repository.
    auto identifying_members() const noexcept
        requires requires(const IndexViewBase& view) { view.get_repository().get_index(); }
    {
        return std::make_tuple(m_handle, get_repository().get_index());
    }
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

    const Data<T>& get_data() const noexcept { return m_handle.get(); }
    const Data<T>& get_record() const noexcept { return m_handle.record(); }
    const C& get_context() const noexcept { return m_context.get(); }
    /// The repository that owns the viewed records, whether the context is borrowed or shared.
    decltype(auto) get_repository() const noexcept
        requires detail::HasRepository<C>
    {
        return detail::owning_repository(m_context.deref());
    }
    const Data<T>& get_handle() const noexcept { return get_data(); }
    /// The record's own index, for records that store one.
    auto get_index() const noexcept
        requires requires(const Data<T>& data) { data.index; }
    {
        return get_data().index;
    }
    /// Records compare structurally within their owning repository.
    auto identifying_members() const noexcept
        requires FunctionIdentified<Data<T>> && requires(const DataViewBase& view) {
            view.get_data().identifying_members();
            view.get_repository().get_index();
        }
    {
        return std::tuple_cat(get_data().identifying_members(), std::make_tuple(get_repository().get_index()));
    }
    auto identifying_members() const noexcept
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

    const Builder<T>& get_data() const noexcept { return m_handle.get(); }
    /// The builder's record, for builders that own one.
    decltype(auto) get_record() const noexcept
        requires requires(const detail::BuilderStorage<T>& storage) { storage.record(); }
    {
        return m_handle.record();
    }
    const C& get_context() const noexcept { return m_context.get(); }
    /// The repository that owns the viewed records, whether the context is borrowed or shared.
    decltype(auto) get_repository() const noexcept
        requires detail::HasRepository<C>
    {
        return detail::owning_repository(m_context.deref());
    }
    const Builder<T>& get_handle() const noexcept { return m_handle.get(); }
    /// The record's own index, for builders whose record stores one.
    auto get_index() const noexcept
        requires requires(const BuilderViewBase& view) { view.get_record().index; }
    {
        return get_record().index;
    }
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
