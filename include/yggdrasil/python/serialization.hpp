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
    return boost::json::visit([]<typename T>(const T& item) -> nanobind::object
    {
        if constexpr (std::is_same_v<T, boost::json::array>)
        {
            auto result = nanobind::list();
            for (const auto& element : item)
                result.append(to_python(element));
            return result;
        }
        else if constexpr (std::is_same_v<T, boost::json::object>)
        {
            auto result = nanobind::dict();
            for (const auto& entry : item)
                result[nanobind::str(entry.key().data(), entry.key().size())] = to_python(entry.value());
            return result;
        }
        else if constexpr (std::is_same_v<T, boost::json::string>)
            return nanobind::str(item.data(), item.size());
        else
            return nanobind::cast(item);
    }, value);
}

using NativeSerializer = boost::json::value (*)(serialization::Dictionaries&, nanobind::handle);
inline constexpr char native_serializer_capsule_name[] = "ygg.native_serializer.v1";

template<typename T>
void bind_native_serializer()
{
    const auto native_type = nanobind::type<T>();
    if (nanobind::cast<bool>(native_type.attr("__dict__").attr("__contains__")("_ygg_serialize")))
        return;
    static const NativeSerializer serializer = [](serialization::Dictionaries& dictionaries, nanobind::handle value)
    { return dictionaries.serialize(nanobind::cast<std::conditional_t<std::is_enum_v<T>, T, const T&>>(value)); };
    native_type.attr("_ygg_serialize") = nanobind::capsule(&serializer, native_serializer_capsule_name);
}

inline std::optional<boost::json::value> from_python_native(serialization::Dictionaries& dictionaries,
                                                           nanobind::handle value,
                                                           std::vector<nanobind::object>& owners)
{
    const auto hook = nanobind::getattr(value.type(), "_ygg_serialize", nanobind::none());
    if (hook.is_none())
        return std::nullopt;
    if (!PyCapsule_IsValid(hook.ptr(), native_serializer_capsule_name))
        throw nanobind::type_error("invalid native serialization hook");
    const auto serializer = static_cast<const NativeSerializer*>(PyCapsule_GetPointer(hook.ptr(), native_serializer_capsule_name));
    // Retain yielded native objects while dictionary keys refer to them.
    owners.emplace_back(nanobind::borrow<nanobind::object>(value));
    return (*serializer)(dictionaries, value);
}

inline boost::json::value from_python(serialization::Dictionaries& dictionaries,
                                     nanobind::handle value,
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

    // Native views may be iterable themselves, so handle them before containers.
    if (auto native = from_python_native(dictionaries, value, owners))
        return std::move(*native);

    if (Py_EnterRecursiveCall(" while serializing a projection"))
        throw nanobind::python_error();
    struct RecursionGuard
    {
        ~RecursionGuard() { Py_LeaveRecursiveCall(); }
    } guard;

    boost::json::value result;
    if (nanobind::isinstance<nanobind::dict>(value))
    {
        auto& object = result.emplace_object();
        for (const auto& [key, item] : nanobind::borrow<nanobind::dict>(value))
        {
            if (!nanobind::isinstance<nanobind::str>(key))
                throw nanobind::type_error("serialization mapping keys must be strings");
            object[nanobind::cast<std::string>(key)] = from_python(dictionaries, item, owners);
        }
        return result;
    }
    auto& array = result.emplace_array();
    for (const auto item : nanobind::iter(value))
        array.push_back(from_python(dictionaries, item, owners));
    return result;
}

template<typename... Registered>
void register_table(serialization::Dictionaries& dictionaries,
                    nanobind::type_object native_type,
                    const std::string& name,
                    const std::string& prefix,
                    TypeList<Registered...>,
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
                        archive.fields[field] = from_python(dictionaries, item, owners);
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

template<typename T>
void bind_registration(nanobind::module_& module)
{
    namespace nb = nanobind;
    using serialization::Dictionaries;

    bind_registration_overload(module,
                               [](Dictionaries& dictionaries, nb::type_object_t<T> native_type,
                                  const std::string& name, const std::string& prefix, nb::object fields, nb::object project)
                               { register_table(dictionaries, native_type, name, prefix, TypeList<T> {}, fields, project); },
                               registration_signature(nb::type<T>()));
}

template<typename... Registered>
void bind_registration_overloads(nanobind::module_& module, TypeList<Registered...>)
{
    (bind_registration<Registered>(module), ...);
}

template<typename T>
void bind_serialize(nanobind::module_& module)
{
    namespace nb = nanobind;
    using namespace nb::literals;
    using serialization::Dictionaries;

    module.def("serialize",
               [](Dictionaries& dictionaries, const T& value) { return to_python(dictionaries.serialize(value)); },
               "dictionaries"_a, "value"_a.noconvert(), nb::keep_alive<1, 2>());
}

template<typename... Serialized>
void bind_serialize(nanobind::module_& module, TypeList<Serialized...>)
{
    (bind_serialize<Serialized>(module), ...);
}

template<typename T>
void bind_table(nanobind::module_& module)
{
    namespace nb = nanobind;
    using namespace nb::literals;
    using serialization::Dictionaries;

    module.def("table",
               [](Dictionaries& dictionaries, nb::type_object_t<T> native_type)
               { return table(dictionaries, native_type, TypeList<T> {}); },
               "dictionaries"_a, "native_type"_a);
}

template<typename... Registered>
void bind_table(nanobind::module_& module, TypeList<Registered...>)
{
    (bind_table<Registered>(module), ...);
}

template<typename... Registered, typename... Serialized, typename... Projected>
void bind_serialization(nanobind::module_& module, TypeList<Registered...>, TypeList<Serialized...>, TypeList<Projected...>)
{
    (bind_fields<Serialized>(), ...);
    (bind_native_serializer<Projected>(), ...);
    bind_registration_overloads(module, TypeList<Registered...> {});
    bind_serialize(module, TypeList<Serialized...> {});
    bind_table(module, TypeList<Registered...> {});
}

}  // namespace ygg::python

#endif
