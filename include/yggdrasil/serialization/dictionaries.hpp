#ifndef YGG_SERIALIZATION_DICTIONARIES_HPP_
#define YGG_SERIALIZATION_DICTIONARIES_HPP_

#include "yggdrasil/serialization/conversion.hpp"

#include <algorithm>
#include <any>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>
#include <yggdrasil/containers/variant.hpp>
#include <yggdrasil/core/concepts.hpp>
#include <yggdrasil/semantics/equal_to.hpp>
#include <yggdrasil/semantics/hash.hpp>

namespace ygg::serialization
{

namespace detail
{
template<typename T>
using Index = std::unordered_map<T, size_t, Hash<T>, EqualTo<T>>;
}

/// Objects referenced by stored keys must remain valid while the dictionaries are used.
class Dictionaries
{
    struct Table
    {
        std::string name;
        std::string prefix;
        boost::json::array rows;
        std::any index;
    };

    std::vector<Table> m_tables;
    std::unordered_map<std::type_index, size_t> m_types;
    boost::json::object m_enums;
    size_t m_next_kind_index = 0;
    bool m_started = false;
    bool m_failed = false;

    void check_valid() const
    {
        if (m_failed)
            throw std::logic_error("Serialization failed; create a new dictionary registry");
    }

    class Archive
    {
        Dictionaries& m_dictionaries;
        std::string m_type_name;

    public:
        boost::json::object fields;

        Archive(Dictionaries& dictionaries, std::string name) : m_dictionaries(dictionaries), m_type_name(std::move(name)) {}

        template<typename T>
        void field(std::string_view name, const T& value)
        {
            fields[name] = boost::json::value_from(value, &m_dictionaries);
        }

        template<typename Variant>
        void variant(const Variant& value)
        {
            // Visit the underlying variant directly: ygg::visit is noexcept, while serialization can throw.
            std::visit([&](const auto& alternative)
            {
                using Alternative = std::remove_cvref_t<decltype(alternative)>;
                const auto& item = value.template get<Alternative>();
                const auto kind = value.index_variant().index();
                fields["kind"] = m_dictionaries.add_kind(m_type_name, kind, TypeName<std::remove_cvref_t<decltype(item)>>::get());
                fields["value"] = boost::json::value_from(item, &m_dictionaries);
            }, value.index_variant());
        }
    };

    template<typename T, typename Body>
    boost::json::value collect(const T&, Body&& body)
    {
        return body();
    }

    template<Hashable T, typename Body>
    boost::json::value collect(const T& value, Body&& body)
    {
        if (const auto found = m_types.find(typeid(T)); found != m_types.end())
        {
            auto& table = m_tables[found->second];
            auto& index = std::any_cast<detail::Index<T>&>(table.index);
            const auto [entry, inserted] = index.try_emplace(value, table.rows.size());
            const auto id = entry->second;
            const auto reference = table.prefix + std::to_string(id);
            if (inserted)
            {
                table.rows.emplace_back(nullptr);
                // Descendants may append to this table. Keep the index, not a reference to its row.
                auto row = body();
                table.rows[id] = std::move(row);
            }
            return boost::json::value(reference);
        }
        return body();
    }

public:
    template<typename T, typename Fields>
    void object(boost::json::value& result, const T& value, Fields&& fields)
    {
        result = collect(value, [&]
        {
            Archive archive(*this, TypeName<T>::get());
            fields(archive);
            return std::move(archive.fields);
        });
    }

    std::string add_kind(std::string_view type, size_t id, std::string name)
    {
        auto [entry, inserted] = m_enums.emplace(type, boost::json::array {});
        auto& rows = entry->value().as_array();
        const auto position = std::ranges::find_if(rows, [id](const auto& row) { return row.as_object().at("id").as_uint64() >= id; });
        if (position != rows.end() && position->as_object().at("id").as_uint64() == id)
        {
            const auto& reference = position->as_object().at("ref").as_string();
            return {reference.data(), reference.size()};
        }
        auto reference = "@" + std::to_string(m_next_kind_index);
        rows.insert(position, boost::json::object {{"ref", reference}, {"id", id}, {"name", std::move(name)}});
        ++m_next_kind_index;
        return reference;
    }

    template<Hashable T>
    void register_table(std::string name, std::string prefix)
    {
        check_valid();
        if (m_started)
            throw std::logic_error("Register tables before serialization begins");
        if (name.empty() || prefix.empty() || (prefix.back() >= '0' && prefix.back() <= '9'))
            throw std::invalid_argument("Table name and prefix must be nonempty; prefix must end in a non-digit");
        if (prefix == "@")
            throw std::invalid_argument("The @ prefix is reserved for enum and variant references");
        if (m_types.contains(typeid(T)))
            throw std::invalid_argument("Type already has a dictionary table");
        for (const auto& table : m_tables)
            if (table.name == name || table.prefix == prefix)
                throw std::invalid_argument("Table names and prefixes must be unique");
        const auto position = m_tables.size();
        m_tables.push_back({std::move(name), std::move(prefix), {}, detail::Index<T> {}});
        m_types.emplace(typeid(T), position);
    }

    template<typename T>
    boost::json::value serialize(const T& value)
    {
        check_valid();
        m_started = true;
        try { return boost::json::value_from(value, this); }
        catch (...) { m_failed = true; throw; }
    }

    template<typename T>
    boost::json::array table() const
    {
        check_valid();
        const auto found = m_types.find(typeid(T));
        if (found == m_types.end())
            throw std::invalid_argument("Type has no registered dictionary table");
        return m_tables[found->second].rows;
    }

    boost::json::object tables() const
    {
        check_valid();
        boost::json::object result;
        for (const auto& table : m_tables)
            result[table.name] = boost::json::object {{"prefix", table.prefix}, {"rows", table.rows}};
        return result;
    }

    boost::json::object enums() const
    {
        check_valid();
        return m_enums;
    }
};

}

#endif
