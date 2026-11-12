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

#ifndef YGG_CORE_INDEX_SEQUENCE_HPP_
#define YGG_CORE_INDEX_SEQUENCE_HPP_

#include <cstddef>
#include <memory>

namespace ygg
{

/// The numbers a factory gives the objects it makes (their identity, such as a repository's layer). Copies of a
/// sequence draw from the same numbers, so objects made by copies of one factory stay distinct. Not thread-safe.
class IndexSequence
{
    std::shared_ptr<std::size_t> m_next = std::make_shared<std::size_t>(0);

public:
    // ponytail: no overflow check, 2^64 objects are never made.
    std::size_t next() noexcept { return (*m_next)++; }
};

}  // namespace ygg

#endif
