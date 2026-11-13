#ifndef YGG_SERIALIZATION_FIELDS_HPP_
#define YGG_SERIALIZATION_FIELDS_HPP_

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
inline constexpr std::string_view variant_field = "variant";
}

// Keep the visitors in this namespace: ADL must find describe_fields overloads
// declared by downstream libraries, without requiring a native object.
struct FieldNames
{
    urd::Store<urd::sequence<std::string>> names;

    template<typename Accessor>
    void field(std::string_view name, Accessor)
    { names.emplace_back(name); }

    template<typename Accessor>
    void variant(Accessor)
    { names.emplace_back(detail::variant_field); }
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

    template<typename Accessor>
    void variant(Accessor accessor)
    {
        if (archive->accepts(detail::variant_field))
            archive->variant(std::invoke(accessor, *value));
    }
};

/// A record by its fields (urd::Fields), in order: each under its name, read through the view (resolved through its
/// context). A record whose serialization differs (a variant record) has an overload of its own, which is more
/// specialized.
template<typename Archive, typename T, typename C>
    requires urd::HasFields<urd::data<T>>
void describe_fields(Archive& ar, std::type_identity<urd::index_view<T, C>> /*type*/)
{
    urd::for_each_field<urd::data<T>>([&]<typename F>()
                                      { ar.field(F::name, [](const auto& value) -> decltype(auto) { return value.template member<F::member>(); }); });
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
