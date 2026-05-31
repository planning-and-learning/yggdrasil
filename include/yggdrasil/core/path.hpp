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

#ifndef YGG_COMMON_PATH_HPP_
#define YGG_COMMON_PATH_HPP_

#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

namespace ygg::common
{

inline std::filesystem::path resolve_path(const std::filesystem::path& prefix, std::string_view path)
{
    const auto result = std::filesystem::path(std::string(path));
    return result.is_absolute() ? result : prefix / result;
}

inline std::string read_file(const std::filesystem::path& path)
{
    auto stream = std::ifstream(path);
    if (!stream)
        throw std::runtime_error("Could not open file: " + path.string());

    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

}

#endif
