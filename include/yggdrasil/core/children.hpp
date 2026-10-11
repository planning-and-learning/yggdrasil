#ifndef YGGDRASIL_CORE_CHILDREN_HPP_
#define YGGDRASIL_CORE_CHILDREN_HPP_

#include "yggdrasil/core/type_list.hpp"
#include "yggdrasil/core/types.hpp"

#include <cista/containers/variant.h>
#include <cista/containers/vector.h>
#include <concepts>
#include <tuple>
#include <utility>
#include <variant>

/// View-based traversal of the records a record refers to.
///
/// A child is an Index<U> whose U is in Types and is not the record's own type, found anywhere in the record's cista
/// members: directly, in (nested) vectors, as a variant alternative, or inside a struct with cista members. Children
/// are passed to callbacks as views in the record's context, so callers never handle raw indices.
namespace ygg
{
namespace detail
{
template<typename Types, typename T, typename U>
inline constexpr bool is_child_type_v = !std::same_as<U, T> && InTypeList<U, Types>;

// Declared up front: the overloads recurse into each other for nested shapes.
template<typename Types, typename T, typename C, typename F, typename U>
void visit_child(const Index<U>& member, const C& context, F& f);
template<typename Types, typename T, typename C, typename F, typename E>
void visit_child(const ::cista::offset::vector<E>& member, const C& context, F& f);
template<typename Types, typename T, typename C, typename F, typename... Us>
void visit_child(const ::cista::offset::variant<Us...>& member, const C& context, F& f);
template<typename Types, typename T, typename C, typename F, typename M>
void visit_child(const M& member, const C& context, F& f);
template<typename Types, typename T, typename C, typename F, typename U>
void map_child(Index<U>& member, const C& context, F& f);
template<typename Types, typename T, typename C, typename F, typename E>
void map_child(::cista::offset::vector<E>& member, const C& context, F& f);
template<typename Types, typename T, typename C, typename F, typename... Us>
void map_child(::cista::offset::variant<Us...>& member, const C& context, F& f);
template<typename Types, typename T, typename C, typename F, typename M>
void map_child(M& member, const C& context, F& f);

template<typename Types, typename T, typename C, typename F, typename U>
void visit_child(const Index<U>& member, const C& context, F& f)
{
    if constexpr (is_child_type_v<Types, T, U>)
        f(make_view(member, context));
}

template<typename Types, typename T, typename C, typename F, typename E>
void visit_child(const ::cista::offset::vector<E>& member, const C& context, F& f)
{
    for (const auto& element : member)
        visit_child<Types, T>(element, context, f);
}

template<typename Types, typename T, typename C, typename F, typename... Us>
void visit_child(const ::cista::offset::variant<Us...>& member, const C& context, F& f)
{
    std::visit([&](const auto& alternative) { visit_child<Types, T>(alternative, context, f); }, member);
}

/// Structs with cista members (pairs of indices, plans, ...) are searched member by member; anything else holds no child.
template<typename Types, typename T, typename C, typename F, typename M>
void visit_child(const M& member, const C& context, F& f)
{
    if constexpr (requires { member.cista_members(); })
        std::apply([&](const auto&... nested) { (visit_child<Types, T>(nested, context, f), ...); }, member.cista_members());
}

template<typename Types, typename T, typename C, typename F, typename U>
void map_child(Index<U>& member, const C& context, F& f)
{
    if constexpr (is_child_type_v<Types, T, U>)
        member = f(make_view(member, context)).get_index();
}

template<typename Types, typename T, typename C, typename F, typename E>
void map_child(::cista::offset::vector<E>& member, const C& context, F& f)
{
    for (auto& element : member)
        map_child<Types, T>(element, context, f);
}

template<typename Types, typename T, typename C, typename F, typename... Us>
void map_child(::cista::offset::variant<Us...>& member, const C& context, F& f)
{
    std::visit(
        [&](auto alternative)
        {
            map_child<Types, T>(alternative, context, f);
            // Move-assign the active alternative in place: cista before drexlerd/cista@65a0d1e silently drops lvalue assignments of it.
            member = std::move(alternative);
        },
        member);
}

template<typename Types, typename T, typename C, typename F, typename M>
void map_child(M& member, const C& context, F& f)
{
    if constexpr (requires { member.cista_members(); })
        std::apply([&](auto&... nested) { (map_child<Types, T>(nested, context, f), ...); }, member.cista_members());
}
}  // namespace detail

/// Calls f(View<Index<U>, C>) for every child of view's record, in member order.
template<typename Types, typename T, typename C, typename F>
void for_each_child(View<Index<T>, C> view, F&& f)
{
    std::apply([&](const auto&... member) { (detail::visit_child<Types, T>(member, view.get_context(), f), ...); }, view.get_data().cista_members());
}

/// A copy of view's record with every child c replaced by f(c), a view of the same record type, in member order.
/// The copy keeps derived members as they are; inserting it recomputes them.
template<typename Types, typename T, typename C, typename F>
Data<T> map_children(View<Index<T>, C> view, F&& f)
{
    auto data = Data<T>(view.get_data());
    std::apply([&](auto&... member) { (detail::map_child<Types, T>(member, view.get_context(), f), ...); }, data.cista_members());
    return data;
}

}  // namespace ygg

#endif
