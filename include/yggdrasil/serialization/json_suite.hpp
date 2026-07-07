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

#ifndef YGG_SERIALIZATION_JSON_SUITE_HPP_
#define YGG_SERIALIZATION_JSON_SUITE_HPP_

#include "yggdrasil/io/project_path.hpp"
#include "yggdrasil/serialization/json.hpp"

#include <filesystem>
#include <string_view>

namespace ygg::common
{

#ifdef ROOT_DIR
inline std::filesystem::path suite_prefix_path(const boost::json::object& suite)
{
    const auto prefix = find_string(suite, "prefix", "suite");
    return prefix ? resolve_path(root_path(), *prefix) : root_path();
}

inline std::filesystem::path suite_path(const boost::json::object& suite, std::string_view path) { return resolve_path(suite_prefix_path(suite), path); }

inline std::filesystem::path suite_member_path(const boost::json::object& suite, std::string_view key)
{
    return suite_path(suite, as_string(suite, key, "suite"));
}
#endif

}  // namespace ygg::common

#endif
