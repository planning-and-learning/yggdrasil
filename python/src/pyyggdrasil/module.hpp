#ifndef PYYGGDRASIL_MODULE_HPP_
#define PYYGGDRASIL_MODULE_HPP_

#include <nanobind/nanobind.h>
// The type casters of every TU of this module (each TU binding Stores includes this header): one set, so that all
// TUs see the same casters.
#include <urd/python/flat.hpp>
#include <urd/python/maps/flat_map.hpp>
#include <urd/python/maps/index_map.hpp>
#include <urd/python/maps/ordered_map.hpp>
#include <urd/python/maps/parallel_hash_map.hpp>
#include <urd/python/maps/unordered_flat_map.hpp>
#include <urd/python/ranges.hpp>
#include <urd/python/sequences/segmented.hpp>
#include <urd/python/sequences/sequence.hpp>
#include <urd/python/values/dense_set.hpp>
#include <urd/python/values/flat_set.hpp>
#include <urd/python/values/indexed.hpp>
#include <urd/python/values/parallel_hash_set.hpp>
#include <urd/python/values/sparse_set.hpp>
#include <urd/python/values/unordered_flat_set.hpp>
#include <yggdrasil/python/type_casters/closed_interval.hpp>

namespace yggdrasil
{
namespace nb = nanobind;

void bind_module_definitions(nb::module_& m);

}  // namespace yggdrasil

#endif
