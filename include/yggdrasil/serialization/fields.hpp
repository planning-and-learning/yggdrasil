#ifndef YGG_SERIALIZATION_FIELDS_HPP_
#define YGG_SERIALIZATION_FIELDS_HPP_

#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
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
    std::vector<std::string> names;

    template<typename Accessor>
    void field(std::string_view name, Accessor) { names.emplace_back(name); }

    template<typename Accessor>
    void variant(Accessor) { names.emplace_back(detail::variant_field); }
};

template<typename Archive, typename T>
struct FieldWriter
{
    Archive& archive;
    const T& value;

    template<typename Accessor>
    void field(std::string_view name, Accessor accessor)
    {
        if (archive.accepts(name))
            archive.field(name, std::invoke(accessor, value));
    }

    template<typename Accessor>
    void variant(Accessor accessor)
    {
        if (archive.accepts(detail::variant_field))
            archive.variant(std::invoke(accessor, value));
    }
};

/// Default serialized field names, in declaration order. Accessors are never evaluated.
template<typename T>
std::vector<std::string> fields()
{
    FieldNames archive;
    describe_fields(archive, std::type_identity<T> {});
    return std::move(archive.names);
}

}

#endif
