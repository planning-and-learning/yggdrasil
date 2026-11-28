#ifndef YGG_PYTHON_SERIALIZATION_HPP_
#define YGG_PYTHON_SERIALIZATION_HPP_

#include "yggdrasil/formalism/declarations.hpp"
#include "yggdrasil/serialization/dictionaries.hpp"

#include <boost/json.hpp>
#include <boost/mp11.hpp>
#include <cstdint>
#include <fmt/format.h>
#include <functional>
#include <nanobind/nanobind.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>
#include <nanobind/typing.h>
#include <optional>
#include <string>
#include <type_traits>
#include <urd/repr/sequences/sequence.hpp>
#include <urd/store.hpp>
#include <utility>
#include <vector>

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
    native_type.attr("Fields") =
        nanobind::module_::import_("enum").attr("Enum")("Fields",
                                                        members,
                                                        nanobind::arg("module") = native_type.attr("__module__"),
                                                        nanobind::arg("qualname") = nanobind::cast<std::string>(native_type.attr("__qualname__")) + ".Fields");
}

inline std::optional<urd::Store<urd::sequence<std::string>>> selected_fields(nanobind::handle native_type, nanobind::handle fields)
{
    if (fields.is_none())
        return std::nullopt;
    if (nanobind::isinstance<nanobind::str>(fields))
        throw nanobind::type_error("fields must be a sequence, not a string");
    auto selected = urd::Store<urd::sequence<std::string>> {};
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
    return boost::json::visit(
        []<typename T>(const T& item) -> nanobind::object
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
        },
        value);
}

using NativeSerializer = boost::json::value (*)(serialization::Dictionaries&, nanobind::handle);
inline constexpr const char* native_serializer_capsule_name = "ygg.native_serializer.v1";

template<typename T>
void bind_native_serializer()
{
    const auto native_type = nanobind::type<T>();
    if (nanobind::cast<bool>(native_type.attr("__dict__").attr("__contains__")("_ygg_serialize")))
        return;
    static const NativeSerializer serializer = [](serialization::Dictionaries& dictionaries, nanobind::handle value)
    { return dictionaries.serialize(nanobind::cast<std::conditional_t<std::is_enum_v<T>, T, const T&>>(value)); };
    native_type.attr("_ygg_serialize") = nanobind::capsule(static_cast<const void*>(&serializer), native_serializer_capsule_name);
}

inline std::optional<boost::json::value>
from_python_native(serialization::Dictionaries& dictionaries, nanobind::handle value, urd::Store<urd::sequence<nanobind::object>>& owners)
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

inline boost::json::value from_python(serialization::Dictionaries& dictionaries, nanobind::handle value, urd::Store<urd::sequence<nanobind::object>>& owners)
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
        RecursionGuard() = default;
        RecursionGuard(const RecursionGuard&) = delete;
        RecursionGuard& operator=(const RecursionGuard&) = delete;
        RecursionGuard(RecursionGuard&&) = delete;
        RecursionGuard& operator=(RecursionGuard&&) = delete;

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

/// Registers the table of the native type T (bound as `native_type`) under `name` and `prefix`: its fields `fields`
/// (all where none), or the row `project` gives for each value (a dict of its fields).
template<typename T>
void register_table(serialization::Dictionaries& dictionaries,
                    const nanobind::type_object& native_type,
                    const std::string& name,
                    const std::string& prefix,
                    nanobind::object fields = nanobind::none(),
                    nanobind::object project = nanobind::none())
{
    if (!project.is_none() && !PyCallable_Check(project.ptr()))
        throw nanobind::type_error("project must be callable");
    const auto selected = selected_fields(native_type, std::move(fields));
    std::function<void(serialization::Dictionaries::Archive&, const T&)> projection;
    if (!project.is_none())
    {
        projection = [project, &dictionaries, owners = urd::Store<urd::sequence<nanobind::object>> {}](serialization::Dictionaries::Archive& archive,
                                                                                                       const T& value) mutable
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
                    archive.put(field, from_python(dictionaries, item, owners));
            }
        };
    }
    dictionaries.register_table<T>(name, prefix, selected, std::move(projection));
}

/// What a type that can be a table (urd::Hashable) offers the generic table() and register_table() of
/// pyyggdrasil.serialization: its table, and its registration. Held by its Python class (bind_native_table).
struct NativeTable
{
    boost::json::array (*table)(const serialization::Dictionaries& dictionaries);
    void (*register_table)(serialization::Dictionaries& dictionaries,
                           const nanobind::type_object& native_type,
                           const std::string& name,
                           const std::string& prefix,
                           nanobind::object fields,
                           nanobind::object project);
};

inline constexpr const char* native_table_capsule_name = "ygg.native_table.v1";

template<typename T>
void bind_native_table()
{
    const auto native_type = nanobind::type<T>();
    if (nanobind::cast<bool>(native_type.attr("__dict__").attr("__contains__")("_ygg_table")))
        return;
    static const NativeTable hooks { [](const serialization::Dictionaries& dictionaries) { return dictionaries.table<T>(); },
                                     [](serialization::Dictionaries& dictionaries,
                                        const nanobind::type_object& type,
                                        const std::string& name,
                                        const std::string& prefix,
                                        nanobind::object fields,
                                        nanobind::object project)
                                     { register_table<T>(dictionaries, type, name, prefix, std::move(fields), std::move(project)); } };
    native_type.attr("_ygg_table") = nanobind::capsule(static_cast<const void*>(&hooks), native_table_capsule_name);
}

/// The table hooks of a native type (bind_native_table); a type error where it can be no table.
inline const NativeTable& native_table(const nanobind::type_object& native_type)
{
    const auto hook = nanobind::getattr(native_type, "_ygg_table", nanobind::none());
    if (hook.is_none() || !PyCapsule_IsValid(hook.ptr(), native_table_capsule_name))
        throw nanobind::type_error("this native type cannot be registered as a table");
    return *static_cast<const NativeTable*>(PyCapsule_GetPointer(hook.ptr(), native_table_capsule_name));
}

/// Keeps a serialized value alive as long as the dictionaries: their tables hold copies of its views, which
/// point into the repository the value keeps alive. Appending to a list is O(1); nb::keep_alive scans all
/// earlier patients for a duplicate on every call, so serializing n values took O(n^2) time.
inline void retain(nanobind::handle dictionaries, nanobind::handle value)
{
    namespace nb = nanobind;
    nb::object retained = nb::getattr(dictionaries, "_ygg_retained", nb::none());
    if (retained.is_none())
    {
        retained = nb::list();
        nb::setattr(dictionaries, "_ygg_retained", retained);
    }
    nb::borrow<nb::list>(retained).append(value);
}

/// serialize, table and register_table as methods of the shared Dictionaries, once for every native type: each reads
/// the hooks its type's class holds (bind_native_serializer, bind_native_table), so a library binds no function of its
/// own, only its types' hooks.
inline void bind_serialization_methods(nanobind::class_<serialization::Dictionaries>& dictionaries)
{
    namespace nb = nanobind;
    using namespace nb::literals;
    using serialization::Dictionaries;

    dictionaries.def(
        "serialize",
        [](nb::handle_t<Dictionaries> self, nb::handle value)
        {
            auto owners = urd::Store<urd::sequence<nb::object>> {};
            auto result = to_python(from_python(nb::cast<Dictionaries&>(self), value, owners));
            retain(self, value);
            return result;
        },
        "value"_a,
        nb::sig("def serialize(self, value: object) -> object"));
    dictionaries.def(
        "table",
        [](const Dictionaries& self, const nb::type_object& native_type) { return to_python(native_table(native_type).table(self)); },
        "native_type"_a,
        nb::sig("def table(self, native_type: type) -> list[dict[str, object]]"));
    dictionaries.def(
        "register_table",
        [](Dictionaries& self, const nb::type_object& native_type, const std::string& name, const std::string& prefix, nb::object fields, nb::object project)
        { native_table(native_type).register_table(self, native_type, name, prefix, std::move(fields), std::move(project)); },
        "native_type"_a,
        "name"_a,
        "prefix"_a,
        "fields"_a = nb::none(),
        "project"_a = nb::none(),
        nb::sig("def register_table(self, native_type: type, name: str, prefix: str, fields: collections.abc.Sequence[enum.Enum | str] | None = None, "
                "project: collections.abc.Callable[[typing.Any], dict[str, object]] | None = None) -> None"));
}

namespace detail
{
template<typename Context>
struct index_view_of
{
    template<typename T>
    using fn = urd::index_view<T, Context>;
};

}  // namespace detail

/// The views through Context of the records of each kind in Types (a repository's kind list): what a library binds the
/// serialization hooks of.
template<typename Context, typename Types>
using index_views = boost::mp11::mp_transform_q<detail::index_view_of<Context>, Types>;

/// The views through Context of the rows of each relation in Types.
template<typename Context, typename Types>
using row_views = index_views<Context, boost::mp11::mp_transform<ygg::Row, Types>>;

/// The serialization hooks of each of a library's types: its field enum where it is described (bind_fields), its
/// serializer (bind_native_serializer), and its table where it is described and hashable (bind_native_table); a type
/// serialized as a value (an enum by name) has its serializer only. It binds no function: those are Dictionaries'
/// methods (bind_serialization_methods). A library calls it once per module directory, with the types bound there.
template<typename T>
void bind_type_serialization()
{
    if constexpr (serialization::Described<T>)
        bind_fields<T>();
    bind_native_serializer<T>();
    if constexpr (serialization::Described<T> && urd::Hashable<T>)
        bind_native_table<T>();
}

template<typename... Ts>
void bind_serialization(boost::mp11::mp_list<Ts...> /*types*/)
{ (bind_type_serialization<Ts>(), ...); }
}  // namespace ygg::python

#endif
