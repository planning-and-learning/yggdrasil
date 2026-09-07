#ifndef YGG_PYTHON_SERIALIZATION_HPP_
#define YGG_PYTHON_SERIALIZATION_HPP_

#include <boost/json.hpp>
#include <nanobind/nanobind.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>
#include <nanobind/typing.h>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
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
boost::json::value from_python(serialization::Dictionaries& dictionaries,
                              nanobind::handle value,
                              TypeList<Ts...> types,
                              std::vector<nanobind::object>& owners)
{
    if (value.is_none())
        return nullptr;
    if (nanobind::isinstance<nanobind::bool_>(value))
        return nanobind::cast<bool>(value);
    if (nanobind::isinstance<nanobind::int_>(value))
    {
        int overflow = 0;
        const auto number = PyLong_AsLongLongAndOverflow(value.ptr(), &overflow);
        if (!overflow)
            return static_cast<std::int64_t>(number);
        const auto unsigned_number = PyLong_AsUnsignedLongLong(value.ptr());
        if (PyErr_Occurred())
            throw nanobind::python_error();
        return static_cast<std::uint64_t>(unsigned_number);
    }
    if (nanobind::isinstance<nanobind::float_>(value))
        return nanobind::cast<double>(value);
    if (nanobind::isinstance<nanobind::str>(value))
        return boost::json::value(nanobind::cast<std::string>(value));

    boost::json::value result;
    // Native views may be iterable themselves. Retain yielded native objects while dictionary keys refer to them.
    if (((nanobind::isinstance<Ts>(value)
          && (owners.emplace_back(nanobind::borrow<nanobind::object>(value)),
              result = dictionaries.serialize(nanobind::cast<std::conditional_t<std::is_enum_v<Ts>, Ts, const Ts&>>(value)), true)) || ...))
        return result;
    if (nanobind::isinstance<nanobind::dict>(value))
    {
        auto& object = result.emplace_object();
        for (const auto& [key, item] : nanobind::borrow<nanobind::dict>(value))
        {
            if (!nanobind::isinstance<nanobind::str>(key))
                throw nanobind::type_error("serialization mapping keys must be strings");
            object[nanobind::cast<std::string>(key)] = from_python(dictionaries, item, types, owners);
        }
        return result;
    }
    auto& array = result.emplace_array();
    for (const auto item : nanobind::iter(value))
        array.push_back(from_python(dictionaries, item, types, owners));
    return result;
}

template<typename... Registered, typename... Serialized>
void register_table(serialization::Dictionaries& dictionaries,
                    nanobind::type_object native_type,
                    const std::string& name,
                    const std::string& prefix,
                    TypeList<Registered...>,
                    TypeList<Serialized...>,
                    std::optional<std::vector<std::string>> fields = std::nullopt,
                    nanobind::object project = nanobind::none())
{
    if (!project.is_none() && !PyCallable_Check(project.ptr()))
        throw nanobind::type_error("project must be callable");
    auto register_type = [&]<typename T>()
    {
        std::function<void(serialization::Dictionaries::Archive&, const T&)> projection;
        if (!project.is_none())
        {
            projection = [project, &dictionaries, owners = std::vector<nanobind::object> {}]
                         (serialization::Dictionaries::Archive& archive, const T& value) mutable
            {
                nanobind::gil_scoped_acquire guard;
                const auto row = project(nanobind::cast(value, nanobind::rv_policy::copy));
                if (!nanobind::isinstance<nanobind::dict>(row))
                    throw nanobind::type_error("project must return a dict");
                for (const auto& [key, item] : nanobind::borrow<nanobind::dict>(row))
                {
                    if (!nanobind::isinstance<nanobind::str>(key))
                        throw nanobind::type_error("serialization mapping keys must be strings");
                    const auto field = nanobind::cast<std::string>(key);
                    if (archive.accepts(field))
                        archive.field(field, from_python(dictionaries, item, TypeList<Serialized...> {}, owners));
                }
            };
        }
        dictionaries.register_table<T>(name, prefix, fields, std::move(projection));
        return true;
    };
    if (!((native_type.is(nanobind::type<Registered>()) && register_type.template operator()<Registered>()) || ...))
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

template<typename... Registered, typename... Serialized, typename... Projected>
void bind_serialization(nanobind::module_& module, TypeList<Registered...>, TypeList<Serialized...>, TypeList<Projected...>)
{
    namespace nb = nanobind;
    using namespace nb::literals;
    using serialization::Dictionaries;

    module.attr("NativeT") = nb::type_var("NativeT");
    module.def("register_table",
               [](Dictionaries& dictionaries, nb::type_object native_type, const std::string& name, const std::string& prefix,
                  const std::optional<std::vector<std::string>>& fields, nb::object project)
               { register_table(dictionaries, native_type, name, prefix, TypeList<Registered...> {}, TypeList<Projected...> {}, fields, project); },
               "dictionaries"_a, "native_type"_a, "name"_a, "prefix"_a, "fields"_a = nb::none(), "project"_a = nb::none(),
               nb::sig("def register_table(dictionaries: pyyggdrasil.serialization.Dictionaries, native_type: type[NativeT], "
                       "name: str, prefix: str, fields: collections.abc.Sequence[str] | None = None, "
                       "project: collections.abc.Callable[[NativeT], dict[str, object]] | None = None) -> None"));
    module.def("serialize",
               [](Dictionaries& dictionaries, nb::handle value) { return serialize(dictionaries, value, TypeList<Serialized...> {}); },
               "dictionaries"_a, "value"_a, nb::keep_alive<1, 2>(),
               nb::sig("def serialize(dictionaries: pyyggdrasil.serialization.Dictionaries, value: object) -> str"));
    module.def("table",
               [](Dictionaries& dictionaries, nb::type_object native_type) { return table(dictionaries, native_type, TypeList<Registered...> {}); },
               "dictionaries"_a, "native_type"_a,
               nb::sig("def table(dictionaries: pyyggdrasil.serialization.Dictionaries, native_type: type) -> list[pyyggdrasil.serialization.table.Row]"));
}

}  // namespace ygg::python

#endif
