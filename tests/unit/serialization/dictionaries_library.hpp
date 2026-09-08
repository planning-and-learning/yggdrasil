#ifndef YGG_TESTS_SERIALIZATION_DICTIONARIES_LIBRARY_HPP_
#define YGG_TESTS_SERIALIZATION_DICTIONARIES_LIBRARY_HPP_

#include "serialization_dictionaries_library_export.h"

#include <tuple>
#include <typeinfo>
#include <yggdrasil/serialization/dictionaries.hpp>

namespace ygg::tests
{

struct DictionaryEntity
{
    int value;

    auto identifying_members() const noexcept { return std::tie(value); }
};

template<typename Archive>
void describe_fields(Archive& archive, std::type_identity<DictionaryEntity>)
{
    archive.field("value", [](const DictionaryEntity& entity) { return entity.value; });
}

SERIALIZATION_DICTIONARIES_LIBRARY_EXPORT const std::type_info& library_entity_type();
SERIALIZATION_DICTIONARIES_LIBRARY_EXPORT void register_in_library(serialization::Dictionaries& dictionaries, int& projections);
SERIALIZATION_DICTIONARIES_LIBRARY_EXPORT boost::json::value serialize_in_library(serialization::Dictionaries& dictionaries, int value);
SERIALIZATION_DICTIONARIES_LIBRARY_EXPORT boost::json::array table_in_library(const serialization::Dictionaries& dictionaries);

}  // namespace ygg::tests

#endif
