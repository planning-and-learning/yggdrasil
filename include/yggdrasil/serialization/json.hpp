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

#ifndef YGG_SERIALIZATION_JSON_HPP_
#define YGG_SERIALIZATION_JSON_HPP_

#include "yggdrasil/core/config.hpp"
#include "yggdrasil/core/path.hpp"

#include <boost/json.hpp>
#include <cmath>
#include <filesystem>
#include <functional>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace ygg::common
{

inline boost::json::value load_json_file(const std::filesystem::path& path)
{
    const auto contents = read_file(path);
    try
    {
        return boost::json::parse(contents);
    }
    catch (const std::exception& e)
    {
        throw std::runtime_error("Could not parse JSON file: " + path.string() + ": " + e.what());
    }
}

inline std::string json_member_context(std::string_view context, std::string_view key)
{
    if (context.empty())
        return std::string(key);

    std::string result;
    result.reserve(context.size() + 1 + key.size());
    result.append(context).append(".").append(key);
    return result;
}

inline const boost::json::value* find_member(const boost::json::object& object, std::string_view key) { return object.if_contains(key); }

namespace detail
{

template<typename T, typename Parser>
inline std::optional<T> find_parsed_member(const boost::json::object& object, std::string_view key, std::string_view context, Parser parser)
{
    const auto* value = find_member(object, key);
    if (!value)
        return std::nullopt;
    return std::optional<T>(parser(*value, json_member_context(context, key)));
}

template<typename T, typename Parser>
inline std::optional<std::reference_wrapper<const T>>
find_parsed_member_ref(const boost::json::object& object, std::string_view key, std::string_view context, Parser parser)
{
    const auto* value = find_member(object, key);
    if (!value)
        return std::nullopt;
    return std::cref(parser(*value, json_member_context(context, key)));
}

}  // namespace detail

inline const boost::json::value& require_member(const boost::json::object& object, std::string_view key, std::string_view context)
{
    const auto* value = find_member(object, key);
    if (!value)
        throw std::runtime_error(json_member_context(context, key) + " is required.");
    return *value;
}

inline const boost::json::object& as_object(const boost::json::value& value, std::string_view context)
{
    if (!value.is_object())
        throw std::runtime_error(std::string(context) + " must be an object.");
    return value.as_object();
}

inline const boost::json::object& as_object(const boost::json::object& object, std::string_view key, std::string_view context)
{
    return as_object(require_member(object, key, context), json_member_context(context, key));
}

inline std::optional<std::reference_wrapper<const boost::json::object>>
find_object(const boost::json::object& object, std::string_view key, std::string_view context)
{
    return detail::find_parsed_member_ref<boost::json::object>(object,
                                                               key,
                                                               context,
                                                               [](const auto& value, std::string_view member_context) -> const auto&
                                                               { return as_object(value, member_context); });
}

inline const boost::json::array& as_array(const boost::json::value& value, std::string_view context)
{
    if (!value.is_array())
        throw std::runtime_error(std::string(context) + " must be an array.");
    return value.as_array();
}

inline const boost::json::array& as_array(const boost::json::object& object, std::string_view key, std::string_view context)
{
    return as_array(require_member(object, key, context), json_member_context(context, key));
}

inline std::optional<std::reference_wrapper<const boost::json::array>>
find_array(const boost::json::object& object, std::string_view key, std::string_view context)
{
    return detail::find_parsed_member_ref<boost::json::array>(object,
                                                              key,
                                                              context,
                                                              [](const auto& value, std::string_view member_context) -> const auto&
                                                              { return as_array(value, member_context); });
}

inline std::string as_string(const boost::json::value& value, std::string_view context)
{
    if (!value.is_string())
        throw std::runtime_error(std::string(context) + " must be a string.");
    return std::string(value.as_string());
}

inline std::string as_string(const boost::json::object& object, std::string_view key, std::string_view context)
{
    return as_string(require_member(object, key, context), json_member_context(context, key));
}

inline std::optional<std::string> find_string(const boost::json::object& object, std::string_view key, std::string_view context)
{
    return detail::find_parsed_member<std::string>(object,
                                                   key,
                                                   context,
                                                   [](const auto& value, std::string_view member_context) { return as_string(value, member_context); });
}

inline bool as_bool(const boost::json::value& value, std::string_view context)
{
    if (!value.is_bool())
        throw std::runtime_error(std::string(context) + " must be a boolean.");
    return value.as_bool();
}

inline bool as_bool(const boost::json::object& object, std::string_view key, std::string_view context)
{
    return as_bool(require_member(object, key, context), json_member_context(context, key));
}

inline std::optional<bool> find_bool(const boost::json::object& object, std::string_view key, std::string_view context)
{
    return detail::find_parsed_member<bool>(object,
                                            key,
                                            context,
                                            [](const auto& value, std::string_view member_context) { return as_bool(value, member_context); });
}

inline size_t as_size(const boost::json::value& value, std::string_view context)
{
    if (value.is_uint64())
    {
        const auto unsigned_value = value.as_uint64();
        if (unsigned_value > std::numeric_limits<size_t>::max())
            throw std::runtime_error(std::string(context) + " must fit into size_t.");
        return static_cast<size_t>(unsigned_value);
    }

    if (!value.is_int64() || value.as_int64() < 0)
        throw std::runtime_error(std::string(context) + " must be a non-negative integer.");

    const auto signed_value = value.as_int64();
    if (static_cast<std::uint64_t>(signed_value) > std::numeric_limits<size_t>::max())
        throw std::runtime_error(std::string(context) + " must fit into size_t.");
    return static_cast<size_t>(signed_value);
}

inline size_t as_size(const boost::json::object& object, std::string_view key, std::string_view context)
{
    return as_size(require_member(object, key, context), json_member_context(context, key));
}

inline uint_t as_uint_t(const boost::json::value& value, std::string_view context)
{
    try
    {
        return to_uint_t(as_size(value, context));
    }
    catch (const std::overflow_error&)
    {
        throw std::runtime_error(std::string(context) + " must fit into uint_t.");
    }
}

inline uint_t as_uint_t(const boost::json::object& object, std::string_view key, std::string_view context)
{
    return as_uint_t(require_member(object, key, context), json_member_context(context, key));
}

inline std::optional<size_t> find_size(const boost::json::object& object, std::string_view key, std::string_view context)
{
    return detail::find_parsed_member<size_t>(object,
                                              key,
                                              context,
                                              [](const auto& value, std::string_view member_context) { return as_size(value, member_context); });
}

inline std::optional<uint_t> find_uint_t(const boost::json::object& object, std::string_view key, std::string_view context)
{
    return detail::find_parsed_member<uint_t>(object,
                                              key,
                                              context,
                                              [](const auto& value, std::string_view member_context) { return as_uint_t(value, member_context); });
}

inline double as_double(const boost::json::value& value, std::string_view context)
{
    if (value.is_double())
        return value.as_double();
    if (value.is_int64())
        return static_cast<double>(value.as_int64());
    if (value.is_uint64())
        return static_cast<double>(value.as_uint64());
    if (value.is_string() && std::string(value.as_string()) == "NaN")
        return std::numeric_limits<double>::quiet_NaN();

    throw std::runtime_error(std::string(context) + " must be a number or \"NaN\".");
}

inline double as_double(const boost::json::object& object, std::string_view key, std::string_view context)
{
    return as_double(require_member(object, key, context), json_member_context(context, key));
}

inline std::optional<double> find_double(const boost::json::object& object, std::string_view key, std::string_view context)
{
    return detail::find_parsed_member<double>(object,
                                              key,
                                              context,
                                              [](const auto& value, std::string_view member_context) { return as_double(value, member_context); });
}

}  // namespace ygg::common

#endif
