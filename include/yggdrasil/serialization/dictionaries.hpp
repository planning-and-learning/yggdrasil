#ifndef YGG_SERIALIZATION_DICTIONARIES_HPP_
#define YGG_SERIALIZATION_DICTIONARIES_HPP_

#include "yggdrasil/serialization/conversion.hpp"
#include "yggdrasil/serialization/fields.hpp"

#include <algorithm>
#include <any>
#include <functional>
#include <optional>
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
        std::optional<std::vector<std::string>> fields;
        std::any project;
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

public:
    class Archive
    {
        Dictionaries& m_dictionaries;
        const std::optional<std::vector<std::string>>& m_selected_fields;

    public:
        boost::json::object fields;

        Archive(Dictionaries& dictionaries, const std::optional<std::vector<std::string>>& selected_fields) :
            m_dictionaries(dictionaries), m_selected_fields(selected_fields)
        {}

        bool accepts(std::string_view name) const
        {
            return !m_selected_fields || std::ranges::find(*m_selected_fields, name) != m_selected_fields->end();
        }

        template<typename T>
        void field(std::string_view name, const T& value)
        {
            // Filter before conversion: omitted fields must not collect descendants into other tables.
            if (accepts(name))
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
                field(detail::variant_fields[0], TypeName<std::remove_cvref_t<decltype(item)>>::get());
                field(detail::variant_fields[1], item);
            }, value.index_variant());
        }
    };

private:
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
                    auto row = body(table);
                    table.rows[id] = std::move(row);
                }
                return boost::json::value(reference);
            }
        }
        // Missing registrations must not silently emit potentially huge native text, including in nested fields.
        // A projection can explicitly convert an entity to a string when that representation is wanted.
        throw std::invalid_argument("Unregistered serialization type: " + TypeName<T>::get());
    }

public:
    template<typename T, typename Fields>
    void object(boost::json::value& result, const T& value, Fields&& fields)
    {
        result = collect(value, [&](const Table& table)
        {
            Archive archive(*this, table.fields);
            const auto& project = std::any_cast<const std::function<void(Archive&, const T&)>&>(table.project);
            if (project)
                project(archive, value);
            else
                fields(archive);
            return std::move(archive.fields);
        });
    }

    template<Hashable T>
    void register_table(std::string name,
                        std::string prefix,
                        std::optional<std::vector<std::string>> fields = std::nullopt,
                        std::function<void(Archive&, const T&)> project = {})
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
        m_tables.push_back({std::move(name), std::move(prefix), {}, detail::Index<T> {}, std::move(fields), std::move(project)});
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

template<typename T>
    requires requires(FieldNames& archive) { describe_fields(archive, std::type_identity<T> {}); }
void tag_invoke(boost::json::value_from_tag, boost::json::value& result, const T& value, Dictionaries* dictionaries)
{
    dictionaries->object(result, value, [&](auto& archive)
    {
        auto writer = FieldWriter {archive, value};
        describe_fields(writer, std::type_identity<T> {});
    });
}

}

#endif
