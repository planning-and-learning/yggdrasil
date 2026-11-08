#ifndef PYYGGDRASIL_DATABASE_MODULE_HPP_
#define PYYGGDRASIL_DATABASE_MODULE_HPP_

#include "yggdrasil/formalism/relation_builder.hpp"
#include "yggdrasil/database/semantics/relation_view.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <nanobind/nanobind.h>
#include <nanobind/stl/variant.h>
#include <span>
#include <stdexcept>
#include <variant>

namespace nanobind::detail
{
/// An object column's index (ColumnValue's untagged object index) is a Python int.
template<>
struct type_caster<urd::index<ygg::formalism::Object<void>>>
{
    NB_TYPE_CASTER(urd::index<ygg::formalism::Object<void>>, const_name("int"))

    bool from_python(handle src, uint8_t flags, cleanup_list* cleanup) noexcept
    {
        auto index = make_caster<std::uint32_t>();
        if (!index.from_python(src, flags, cleanup))
            return false;
        value = urd::index<ygg::formalism::Object<void>>(index.value);
        return true;
    }
    static handle from_cpp(urd::index<ygg::formalism::Object<void>> index, rv_policy policy, cleanup_list* cleanup) noexcept
    { return make_caster<std::uint32_t>::from_cpp(index.value, policy, cleanup); }
};
}  // namespace nanobind::detail

namespace yggdrasil
{
namespace nb = nanobind;

/// Python's ColumnType is the closed set of storage type ids.
using ColumnType = ygg::TypeId;

/// A Python value as a value of a column of `type`; nb::type_error where it is not one.
inline ygg::ColumnValue cast_value(nb::handle value, ygg::TypeId type)
{
    auto result = ygg::decode(0, type);  // the alternative of `type`
    std::visit(
        [value](auto& alternative)
        {
            if (!nb::try_cast(value, alternative))
                throw nb::type_error("Value does not match the column type.");
        },
        result);
    return result;
}

/// Python is a trust boundary: the operations only assert (debug builds) that their relations have the plan's columns,
/// so the bindings check it, throwing ValueError.
inline void require_columns(std::span<const ygg::ColumnLayout> actual, std::span<const ygg::ColumnLayout> expected)
{
    if (!std::ranges::equal(actual, expected))
        throw std::invalid_argument("Relational operation: columns do not match the plan.");
}

void bind_database_module_definitions(nb::module_& m);
void bind_database_distance(nb::module_& m);
void bind_database_evaluation(nb::module_& m);
void bind_database_optimization(nb::module_& m);
void bind_database_queries(nb::module_& m);

}  // namespace yggdrasil

#endif
