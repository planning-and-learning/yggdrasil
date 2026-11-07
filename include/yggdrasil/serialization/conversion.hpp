#ifndef YGG_SERIALIZATION_CONVERSION_HPP_
#define YGG_SERIALIZATION_CONVERSION_HPP_


#include <boost/json.hpp>
#include <urd/elements/flat.hpp>
#include <ranges>
#include <type_traits>
#include <utility>

namespace boost::json
{

template<>
struct is_string_like<urd::flat_string> : std::true_type
{
};

}

namespace ygg::serialization
{

class Dictionaries;

// Boost's sequence conversion requires legacy iterator traits, which some C++20 input views lack.
template<std::ranges::view T>
    requires std::ranges::input_range<const T> && (!boost::json::is_sequence_like<T>::value)
void tag_invoke(boost::json::value_from_tag, boost::json::value& result, const T& value, Dictionaries* dictionaries)
{
    auto& array = result.emplace_array();
    for (const auto& item : value)
        array.push_back(boost::json::value_from(item, dictionaries, array.storage()));
}

}

#endif
