/*
 * Copyright (C) 2025-2026 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include "type_casters.hpp"

#include "yggdrasil/core/config.hpp"
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

#include <cista/containers/array.h>
#include <urd/elements/flat.hpp>
#include <nanobind/nanobind.h>
#include <ranges>
#include <span>

void bind_type_casters_test(nanobind::module_& m)
{
    using Interval = ygg::ClosedInterval<ygg::float_t>;

    m.def("array_view",
          []
          {
              using Array = ::cista::array<int, 3>;
              static constexpr auto data = Array { 1, 2, 3 };
              return std::views::all(data);
          });

    m.def("pair_view",
          []
          {
              using Pair = urd::flat_pair<int, int>;
              static constexpr auto data = Pair { 4, 5 };
              return data;
          });

    m.def("nested_view",
          []
          {
              using Pair = urd::flat_pair<int, int>;
              using Array = ::cista::array<Pair, 2>;
              static constexpr auto data = Array { Pair { 6, 7 }, Pair { 8, 9 } };
              return std::views::all(data);
          });

    m.def("roundtrip_pair", [](urd::flat_pair<int, int> value) { return value; });
    m.def("roundtrip_optional_pairs", [](urd::flat_vector<urd::flat_optional<urd::flat_pair<int, int>>> values) { return values; });

    m.def("roundtrip_variant", [](urd::flat_variant<int, double> value) { return value; });

    m.def("empty_interval", [] { return Interval {}; });
    m.def("singleton_interval", [] { return Interval { 2.5, 2.5 }; });
    m.def("bounded_interval", [] { return Interval { 1.25, 3.5 }; });
    m.def("roundtrip_interval", [](Interval interval) { return interval; });
}
