#ifndef YGG_SERIALIZATION_DICTIONARIES_HPP_
#define YGG_SERIALIZATION_DICTIONARIES_HPP_

#include "yggdrasil/serialization/conversion.hpp"

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
#include <yggdrasil/formatting/formatter.hpp>
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

    public:
        boost::json::object fields;

        explicit Archive(Dictionaries& dictionaries) : m_dictionaries(dictionaries) {}

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
                fields["kind"] = TypeName<std::remove_cvref_t<decltype(item)>>::get();
                fields["value"] = boost::json::value_from(item, &m_dictionaries);
            }, value.index_variant());
        }
    };

    template<typename T, typename Body>
    boost::json::value collect(const T& value, Body&& body)
    {
        if constexpr (Hashable<T>)
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
        }
        // Unregistered entities use native text. A missing formatter must fail to compile;
        // do not fall back to structural serialization, which silently changes the representation.
        return boost::json::value(ygg::to_string(value));
    }

public:
    template<typename T, typename Fields>
    void object(boost::json::value& result, const T& value, Fields&& fields)
    {
        result = collect(value, [&]
        {
            Archive archive(*this);
            fields(archive);
            return std::move(archive.fields);
        });
    }

    template<Hashable T>
    void register_table(std::string name, std::string prefix)
    {
        check_valid();
        if (m_started)
            throw std::logic_error("Register tables before serialization begins");
        if (name.empty() || prefix.empty() || (prefix.back() >= '0' && prefix.back() <= '9'))
            throw std::invalid_argument("Table name and prefix must be nonempty; prefix must end in a non-digit");
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

};

}

#endif
