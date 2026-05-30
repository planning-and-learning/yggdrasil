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

#ifndef YGG_COMMON_ADAPTERS_HPP_
#define YGG_COMMON_ADAPTERS_HPP_

#include "ygg/formatting/associative_container_formatters.hpp"
#include "ygg/containers/block_array_equal_to.hpp"
#include "ygg/containers/block_array_hash.hpp"
#include "ygg/containers/block_array_ordering.hpp"
#include "ygg/serialization/cista_equal_to.hpp"
#include "ygg/serialization/cista_hash.hpp"
#include "ygg/formatting/cista_formatters.hpp"
#include "ygg/serialization/cista_ordering.hpp"
#include "ygg/core/closed_interval.hpp"
#include "ygg/containers/dynamic_bitset_equal_to.hpp"
#include "ygg/containers/dynamic_bitset_hash.hpp"
#include "ygg/formatting/dynamic_bitset_formatters.hpp"
#include "ygg/containers/dynamic_bitset_ordering.hpp"
#include "ygg/formatting/formatter.hpp"
#include "ygg/serialization/json.hpp"
#include "ygg/core/observer_ptr_equal_to.hpp"
#include "ygg/core/observer_ptr_hash.hpp"
#include "ygg/core/observer_ptr_ordering.hpp"
#include "ygg/execution/onetbb.hpp"
#include "ygg/containers/raw_vector_equal_to.hpp"
#include "ygg/containers/raw_vector_hash.hpp"
#include "ygg/containers/raw_vector_ordering.hpp"
#include "ygg/containers/segmented_vector_equal_to.hpp"
#include "ygg/containers/segmented_vector_hash.hpp"
#include "ygg/containers/segmented_vector_ordering.hpp"

#endif
