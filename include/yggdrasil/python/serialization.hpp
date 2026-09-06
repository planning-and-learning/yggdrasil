#ifndef YGG_PYTHON_SERIALIZATION_HPP_
#define YGG_PYTHON_SERIALIZATION_HPP_

#include <boost/json.hpp>
#include <nanobind/nanobind.h>
#include <string>
#include <yggdrasil/core/type_list.hpp>
#include <yggdrasil/serialization/dictionaries.hpp>

namespace ygg::python
{

inline nanobind::object to_python(const boost::json::value& value)
{
    switch (value.kind())
    {
        case boost::json::kind::null:
            return nanobind::none();
        case boost::json::kind::bool_:
            return nanobind::bool_(value.as_bool());
        case boost::json::kind::int64:
            return nanobind::int_(value.as_int64());
        case boost::json::kind::uint64:
            return nanobind::int_(value.as_uint64());
        case boost::json::kind::double_:
            return nanobind::float_(value.as_double());
        case boost::json::kind::string:
        {
            const auto& text = value.as_string();
            return nanobind::str(text.data(), text.size());
        }
        case boost::json::kind::array:
        {
            auto result = nanobind::list();
            for (const auto& item : value.as_array())
                result.append(to_python(item));
            return result;
        }
        case boost::json::kind::object:
        {
            auto result = nanobind::dict();
            for (const auto& item : value.as_object())
                result[nanobind::str(item.key().data(), item.key().size())] = to_python(item.value());
            return result;
        }
    }
    throw nanobind::type_error("unsupported JSON value");
}

template<typename... Ts>
void register_table(serialization::Dictionaries& dictionaries,
                    nanobind::type_object native_type,
                    const std::string& name,
                    const std::string& prefix,
                    TypeList<Ts...>)
{
    if (!((native_type.is(nanobind::type<Ts>()) && (dictionaries.register_table<Ts>(name, prefix), true)) || ...))
        throw nanobind::type_error("this native type cannot be registered as a table");
}

template<typename... Ts>
nanobind::object serialize(serialization::Dictionaries& dictionaries, nanobind::handle value, TypeList<Ts...>)
{
    nanobind::object result;
    if (!((nanobind::isinstance<Ts>(value) && (result = to_python(dictionaries.serialize(nanobind::cast<const Ts&>(value))), true)) || ...))
        throw nanobind::type_error("this native type does not support serialization");
    return result;
}

template<typename... Ts>
nanobind::object table(serialization::Dictionaries& dictionaries, nanobind::type_object native_type, TypeList<Ts...>)
{
    nanobind::object result;
    if (!((native_type.is(nanobind::type<Ts>()) && (result = to_python(dictionaries.table<Ts>()), true)) || ...))
        throw nanobind::type_error("this native type cannot be registered as a table");
    return result;
}

}  // namespace ygg::python

#endif
