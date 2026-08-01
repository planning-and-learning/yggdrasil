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

#include "yggdrasil/python/type_casters.hpp"

#include <cista/containers/array.h>
#include <cista/containers/pair.h>
#include <nanobind/nanobind.h>
#include <yggdrasil/containers/bit_packed_array_pool.hpp>

namespace nb = nanobind;

NB_MODULE(yggdrasil_type_casters_test, m)
{
    using Interval = ygg::ClosedInterval<ygg::float_t>;

    m.def("array_view",
          []
          {
              using Array = ::cista::array<int, 3>;
              static constexpr auto data = Array { 1, 2, 3 };
              static constexpr auto context = 0;
              return ygg::View<Array, int>(data, context);
          });

    m.def("pair_view",
          []
          {
              using Pair = ::cista::pair<int, int>;
              static constexpr auto data = Pair { 4, 5 };
              static constexpr auto context = 0;
              return ygg::View<Pair, int>(data, context);
          });

    m.def("nested_view",
          []
          {
              using Pair = ::cista::pair<int, int>;
              using Array = ::cista::array<Pair, 2>;
              static constexpr auto data = Array { Pair { 6, 7 }, Pair { 8, 9 } };
              static constexpr auto context = 0;
              return ygg::View<Array, int>(data, context);
          });

    m.def("concurrent_bit_packed_view",
          []
          {
              using Block = unsigned;
              using Coder = ygg::bit::ForwardingBlockCoder<Block>;
              using Array = ygg::BasicBitPackedArrayView<const Block, Coder, true>;
              static auto data = Block { 0b00111001 };
              static const auto view = Array(&data, 3, 2, 0);
              static constexpr auto context = 0;
              return ygg::View<Array, int>(view, context);
          });

    m.def("empty_interval", [] { return Interval {}; });
    m.def("singleton_interval", [] { return Interval { 2.5, 2.5 }; });
    m.def("bounded_interval", [] { return Interval { 1.25, 3.5 }; });
    m.def("roundtrip_interval", [](Interval interval) { return interval; });
}
