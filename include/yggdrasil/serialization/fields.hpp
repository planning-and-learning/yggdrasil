#ifndef YGG_SERIALIZATION_FIELDS_HPP_
#define YGG_SERIALIZATION_FIELDS_HPP_

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <urd/elements/concepts.hpp>
#include <urd/elements/view.hpp>
#include <urd/repr/sequences/sequence.hpp>
#include <urd/store.hpp>
#include <utility>
#include <vector>

namespace ygg::serialization
{

namespace detail
{

/// The fields of a record R (urd::Fields: a data<T>, or a builder<T, C>, whose fields are its record's), in order, each
/// under its name, read through a view of R.
template<typename R, typename Archive>
void describe_record(Archive& ar)
{
    urd::for_each_field<R>([&]<typename F>() { ar.field(F::name, [](const auto& value) -> decltype(auto) { return value.template member<F::member>(); }); });
}

}  // namespace detail

// Keep the visitors in this namespace: ADL must find describe_fields overloads
// declared by downstream libraries, without requiring a native object.
struct FieldNames
{
    urd::Store<urd::sequence<std::string>> names;

    template<typename Accessor>
    void field(std::string_view name, Accessor)
    { names.emplace_back(name); }
};

template<typename Archive, typename T>
struct FieldWriter
{
    Archive* archive;
    const T* value;

    template<typename Accessor>
    void field(std::string_view name, Accessor accessor)
    {
        if (archive->accepts(name))
            archive->field(name, std::invoke(accessor, *value));
    }
};

/// A record by its fields (urd::Fields), in order: each under its name, read through its view (an interned one's
/// resolved through its context, whatever its key: a row's of several parts too; a borrowed, an owning or a shared
/// view's through the record or the builder it holds, a builder's fields under its record's names). A type serialized
/// otherwise declares describe_fields beside it.
template<typename Archive, urd::View V>
    requires urd::HasFields<urd::data<urd::view_kind_t<V>>>
void describe_fields(Archive& ar, std::type_identity<V> /*type*/)
{ detail::describe_record<urd::record_of<V>>(ar); }

/// A record read without a context (a data<T> held as it is: a graph's label, a column's layout) by its fields, each as
/// it holds it.
template<typename Archive, typename T>
    requires urd::HasFields<urd::data<T>>
void describe_fields(Archive& ar, std::type_identity<urd::data<T>> /*type*/)
{
    urd::for_each_field<urd::data<T>>([&]<typename F>() { ar.field(F::name, [](const auto& value) -> decltype(auto) { return (value.*F::member); }); });
}

/// Default serialized field names, in declaration order. Accessors are never evaluated.
template<typename T>
urd::Store<urd::sequence<std::string>> fields()
{
    FieldNames archive;
    describe_fields(archive, std::type_identity<T> {});
    return std::move(archive.names);
}

}

#endif
