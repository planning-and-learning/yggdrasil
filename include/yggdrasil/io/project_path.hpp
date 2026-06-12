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

#ifndef YGG_COMMON_PROJECT_PATH_HPP_
#define YGG_COMMON_PROJECT_PATH_HPP_

#include "yggdrasil/core/path.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace ygg::common
{

#ifdef ROOT_DIR
inline std::filesystem::path root_path() { return std::filesystem::path(std::string(ROOT_DIR)); }

inline std::filesystem::path data_path(std::string_view path) { return resolve_path(root_path() / "data", path); }

inline std::filesystem::path profiling_path(std::string_view path) { return resolve_path(root_path() / "profiling", path); }
#endif

}  // namespace ygg::common

#endif
