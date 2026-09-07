#ifndef YGG_PYTHON_SERIALIZATION_HPP_
#define YGG_PYTHON_SERIALIZATION_HPP_

#include <boost/json.hpp>
#include <fmt/format.h>
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

template<typename T>
void bind_fields()
{
    // Tyr and Runir share native types, so they must also share their field enums.
    const auto native_type = nanobind::type<T>();
    if (nanobind::cast<bool>(native_type.attr("__dict__").attr("__contains__")("Fields")))
        return;
    auto members = nanobind::dict();
    for (const auto& name : serialization::fields<T>())
        members[nanobind::str(name.c_str())] = nanobind::str(name.c_str());
    // The complete Enum factory handles fields called "name" or "value" correctly.
    // Keep plain Enum: inheriting str would let foreign fields match string-selector overloads.
    native_type.attr("Fields") = nanobind::module_::import_("enum").attr("Enum")(
        "Fields", members,
        nanobind::arg("module") = native_type.attr("__module__"),
        nanobind::arg("qualname") = nanobind::cast<std::string>(native_type.attr("__qualname__")) + ".Fields");
}

inline std::optional<std::vector<std::string>> selected_fields(nanobind::handle native_type, nanobind::handle fields)
{
    if (fields.is_none())
        return std::nullopt;
    if (nanobind::isinstance<nanobind::str>(fields))
        throw nanobind::type_error("fields must be a sequence, not a string");
    auto selected = std::vector<std::string> {};
    for (const auto field : nanobind::iter(fields))
    {
        if (nanobind::isinstance<nanobind::str>(field))
            selected.push_back(nanobind::cast<std::string>(field));
        else if (nanobind::isinstance(field, native_type.attr("Fields")))
            selected.push_back(nanobind::cast<std::string>(field.attr("value")));
        else
            throw nanobind::type_error("fields must contain strings or members of native_type.Fields");
    }
    return selected;
}

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
                    nanobind::object fields = nanobind::none(),
                    nanobind::object project = nanobind::none())
{
    if (!project.is_none() && !PyCallable_Check(project.ptr()))
        throw nanobind::type_error("project must be callable");
    auto register_type = [&]<typename T>()
    {
        const auto selected = selected_fields(native_type, fields);
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
        dictionaries.register_table<T>(name, prefix, selected, std::move(projection));
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

template<typename... Ts>
std::vector<std::string> fields(nanobind::type_object native_type, TypeList<Ts...>)
{
    std::vector<std::string> result;
    if (!((native_type.is(nanobind::type<Ts>()) && (result = serialization::fields<Ts>(), true)) || ...))
        throw nanobind::type_error("this native type does not support serialization");
    return result;
}

template<typename... Serialized>
void bind_field_discovery(nanobind::module_& module, TypeList<Serialized...>)
{
    namespace nb = nanobind;
    using namespace nb::literals;

    module.def("fields",
               [](nb::type_object native_type) { return fields(native_type, TypeList<Serialized...> {}); },
               "native_type"_a,
               "Return default serialized field names in declaration order, without evaluating accessors. "
               "Table field selection and custom projections do not change this declaration.");
}

inline std::string registration_signature(nanobind::handle native_type)
{
    const auto type_name = fmt::format("{0}.{1}", nanobind::cast<std::string>(native_type.attr("__module__")),
                                      nanobind::cast<std::string>(native_type.attr("__qualname__")));
    return fmt::format("def register_table(dictionaries: pyyggdrasil.serialization.Dictionaries, native_type: type[{0}], "
                       "name: str, prefix: str, fields: collections.abc.Sequence[{0}.Fields | str] | None = None, "
                       "project: collections.abc.Callable[[{0}], dict[str, object]] | None = None) -> None", type_name);
}

template<typename Function>
void bind_registration_overload(nanobind::module_& module, const Function& function, const std::string& signature)
{
    namespace nb = nanobind;
    using namespace nb::literals;

    module.def("register_table", function,
               "dictionaries"_a, "native_type"_a, "name"_a, "prefix"_a, "fields"_a = nb::none(), "project"_a = nb::none(),
               nb::sig(signature.c_str()));
}

template<typename... Registered, typename... Projected>
void bind_registration_overloads(nanobind::module_& module, TypeList<Registered...>, TypeList<Projected...>)
{
    namespace nb = nanobind;
    using serialization::Dictionaries;

    const auto register_function = [](Dictionaries& dictionaries, nb::type_object native_type, const std::string& name, const std::string& prefix,
                                      nb::object fields, nb::object project)
    { register_table(dictionaries, native_type, name, prefix, TypeList<Registered...> {}, TypeList<Projected...> {}, fields, project); };
    (bind_registration_overload(module, register_function, registration_signature(nb::type<Registered>())), ...);
    bind_registration_overload(module, register_function,
                               "def register_table(dictionaries: pyyggdrasil.serialization.Dictionaries, native_type: type[NativeT], "
                               "name: str, prefix: str, fields: collections.abc.Sequence[str] | None = None, "
                               "project: collections.abc.Callable[[NativeT], dict[str, object]] | None = None) -> None");
}

template<typename... Serialized>
void bind_serialize(nanobind::module_& module, TypeList<Serialized...>)
{
    namespace nb = nanobind;
    using namespace nb::literals;
    using serialization::Dictionaries;

    module.def("serialize",
               [](Dictionaries& dictionaries, nb::handle value) { return serialize(dictionaries, value, TypeList<Serialized...> {}); },
               "dictionaries"_a, "value"_a, nb::keep_alive<1, 2>(),
               nb::sig("def serialize(dictionaries: pyyggdrasil.serialization.Dictionaries, value: object) -> str"));
}

template<typename... Registered>
void bind_table(nanobind::module_& module, TypeList<Registered...>)
{
    namespace nb = nanobind;
    using namespace nb::literals;
    using serialization::Dictionaries;

    module.def("table",
               [](Dictionaries& dictionaries, nb::type_object native_type) { return table(dictionaries, native_type, TypeList<Registered...> {}); },
               "dictionaries"_a, "native_type"_a,
               nb::sig("def table(dictionaries: pyyggdrasil.serialization.Dictionaries, native_type: type) -> list[pyyggdrasil.serialization.table.Row]"));
}

template<typename... Registered, typename... Serialized, typename... Projected>
void bind_serialization(nanobind::module_& module, TypeList<Registered...>, TypeList<Serialized...>, TypeList<Projected...>)
{
    (bind_fields<Serialized>(), ...);
    module.attr("NativeT") = nanobind::type_var("NativeT");
    bind_field_discovery(module, TypeList<Serialized...> {});
    bind_registration_overloads(module, TypeList<Registered...> {}, TypeList<Projected...> {});
    bind_serialize(module, TypeList<Serialized...> {});
    bind_table(module, TypeList<Registered...> {});
}

}  // namespace ygg::python

#endif
