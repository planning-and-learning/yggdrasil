#include "dictionaries_library.hpp"

namespace ygg::tests
{

const std::type_info& library_entity_type() { return typeid(DictionaryEntity); }

void register_in_library(serialization::Dictionaries& dictionaries, int& projections)
{
    dictionaries.register_table<DictionaryEntity>("entities",
                                                  "e",
                                                  std::nullopt,
                                                  [&](serialization::Dictionaries::Archive& archive, const DictionaryEntity& entity)
                                                  {
                                                      ++projections;
                                                      archive.field("scaled", entity.value * 10);
                                                  });
}

boost::json::value serialize_in_library(serialization::Dictionaries& dictionaries, int value) { return dictionaries.serialize(DictionaryEntity { value }); }

boost::json::array table_in_library(const serialization::Dictionaries& dictionaries) { return dictionaries.table<DictionaryEntity>(); }

}  // namespace ygg::tests
