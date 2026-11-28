#ifndef YGG_SERIALIZATION_DICTIONARIES_HPP_
#define YGG_SERIALIZATION_DICTIONARIES_HPP_

#include "yggdrasil/core/unsigned_integer.hpp"
#include "yggdrasil/serialization/conversion.hpp"
#include "yggdrasil/serialization/fields.hpp"

#include <algorithm>
#include <concepts>
#include <functional>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeinfo>
#include <urd/elements/child_view.hpp>
#include <urd/elements/concepts.hpp>
#include <urd/elements/equal_to.hpp>
#include <urd/elements/hash.hpp>
#include <urd/format.hpp>
#include <urd/repr/maps/std_map.hpp>
#include <urd/repr/records/records.hpp>
#include <urd/repr/sequences/sequence.hpp>
#include <urd/store.hpp>
#include <utility>
#include <vector>

namespace ygg::serialization
{

namespace detail
{
/// The values of one table, each once: a value's index is its row's.
template<typename T>
using Index = urd::Store<urd::pool<T>>;
}

/// Types with a describe_fields overload, serialized as dictionary rows.
template<typename T>
concept Described = requires(FieldNames& archive) { describe_fields(archive, std::type_identity<T> {}); };

/// A plain value that declares its fields (urd::Fields) and is no view: serialized inline, as an object of its fields.
template<typename T>
concept PlainFields = urd::HasFields<T> && !urd::View<T> && !urd::ChildView<T>;

class Dictionaries;

/// What a range's element or an optional's value is serialized as, one by one: a described type, a variant
/// (urd::variant_value), a child view (urd::ChildView), or a plain value with fields (PlainFields).
template<typename T>
concept DescribedElement = Described<T> || urd::variant_value<T> || urd::ChildView<T> || PlainFields<T>;

/// Ranges of such elements, serialized element by element.
template<typename T>
concept DescribedRange = std::ranges::input_range<const T> && DescribedElement<std::remove_cvref_t<std::ranges::range_reference_t<const T>>>;

namespace detail
{
template<typename T>
inline constexpr bool described_optional = false;
template<typename T>
inline constexpr bool described_optional<std::optional<T>> = DescribedElement<T>;
}  // namespace detail

/// A std::optional of such an element.
template<typename T>
concept DescribedOptional = detail::described_optional<T>;

/// A type serialized as another object: it declares serialize_as(Dictionaries&, const T&) returning it.
template<typename T>
concept SerializedAs = requires(Dictionaries& dictionaries, const T& value) { serialize_as(dictionaries, value); };

/// Objects referenced by stored keys must remain valid while the dictionaries are used.
class Dictionaries
{
public:
    class Archive;

private:
    struct Table
    {
        std::string name;
        std::string prefix;
        boost::json::array rows;
        std::function<std::pair<size_t, bool>(const void*)> index;
        std::optional<urd::Store<urd::sequence<std::string>>> fields;
        std::function<void(Archive&, const void*)> project;
    };

    urd::Store<urd::sequence<Table>> m_tables;
    // Hidden RTTI can differ across native modules (notably on macOS). Compare type names,
    // as nanobind does; the callbacks keep typed storage in the module that registered it.
    urd::Store<urd::map<std::string, size_t>, urd::stable_refs> m_types;
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
        Dictionaries* m_dictionaries;
        const std::optional<urd::Store<urd::sequence<std::string>>>* m_selected_fields;

        boost::json::object m_fields;

    public:
        Archive(Dictionaries& dictionaries, const std::optional<urd::Store<urd::sequence<std::string>>>& selected_fields) :
            m_dictionaries(&dictionaries),
            m_selected_fields(&selected_fields)
        {
        }

        /// Stores an already converted value; callers check accepts(name) first.
        void put(std::string_view name, boost::json::value value) { m_fields[name] = std::move(value); }

        /// The collected fields; the archive is done.
        boost::json::object take_fields() && { return std::move(m_fields); }

        [[nodiscard]] bool accepts(std::string_view name) const
        { return !*m_selected_fields || std::ranges::find(**m_selected_fields, name) != (*m_selected_fields)->end(); }

        template<typename T>
        void field(std::string_view name, const T& value)
        {
            // Filter before conversion: omitted fields must not collect descendants into other tables.
            if (accepts(name))
                m_fields[name] = m_dictionaries->convert(value);
        }
    };

private:
    template<typename T, typename Body>
    boost::json::value collect(const T& value, Body body)
    {
        if constexpr (urd::Hashable<T>)
        {
            if (const auto found = m_types.find(typeid(T).name()); found != m_types.end())
            {
                auto& table = m_tables[found->second];
                const auto [id, inserted] = table.index(std::addressof(value));
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
        throw std::invalid_argument("Unregistered serialization type: " + urd::spelling<T>());
    }

public:
    /// Converts described types, and ranges and optionals of them, without Boost's conversion dispatch, which
    /// otherwise computes every conversion category of every nested type; other types use boost::json::value_from.
    /// A type serialized as another object declares serialize_as(Dictionaries&, const T&) returning it.
    template<typename T>
    boost::json::value convert(const T& value)
    {
        if constexpr (SerializedAs<T>)
            return convert(serialize_as(*this, value));
        else if constexpr (std::ranges::view<T> && std::ranges::input_range<T> && !std::ranges::input_range<const T>)
        {
            // A view iterable only when mutable (std::views::filter caches its begin): read from a copy.
            auto range = value;
            auto result = boost::json::value {};
            auto& array = result.emplace_array();
            for (auto&& item : range)
                array.push_back(convert(item));
            return result;
        }
        else if constexpr (urd::variant_value<T>)
            return visit([&](const auto& alternative) { return convert(alternative); }, value);  // a variant: the alternative it holds
        else if constexpr (urd::ChildView<T>)
            return urd::visit_view(value, [&](const auto& form) { return convert(form); });  // a child: the view of the form it holds
        else if constexpr (Described<T>)
        {
            auto result = boost::json::value {};
            object(result,
                   value,
                   [&](auto& archive)
                   {
                       auto writer = FieldWriter { &archive, &value };
                       describe_fields(writer, std::type_identity<T> {});
                   });
            return result;
        }
        else if constexpr (PlainFields<T>)
        {
            // Inline: an object of its fields, each converted.
            auto result = boost::json::value {};
            auto& object = result.emplace_object();
            urd::for_each_field<T>([&]<typename F>() { object[F::name] = convert(value.*F::member); });
            return result;
        }
        else if constexpr (DescribedOptional<T>)
            return value ? convert(*value) : boost::json::value(nullptr);
        else if constexpr (DescribedRange<T>)
        {
            auto result = boost::json::value {};
            auto& array = result.emplace_array();
            for (const auto& item : value)
                array.push_back(convert(item));
            return result;
        }
        else
            return boost::json::value_from(value, this);
    }

    template<typename T, typename Fields>
    void object(boost::json::value& result, const T& value, Fields fields)
    {
        result = collect(value,
                         [&](const Table& table)
                         {
                             Archive archive(*this, table.fields);
                             if (table.project)
                                 table.project(archive, std::addressof(value));
                             else
                                 fields(archive);
                             return std::move(archive).take_fields();
                         });
    }

    template<urd::Hashable T>
    void register_table(std::string name,
                        std::string prefix,
                        std::optional<urd::Store<urd::sequence<std::string>>> fields = std::nullopt,
                        std::function<void(Archive&, const T&)> project = {})
    {
        check_valid();
        if (m_started)
            throw std::logic_error("Register tables before serialization begins");
        if (name.empty() || prefix.empty() || (prefix.back() >= '0' && prefix.back() <= '9'))
            throw std::invalid_argument("Table name and prefix must be nonempty; prefix must end in a non-digit");
        if (m_types.contains(typeid(T).name()))
            throw std::invalid_argument("Type already has a dictionary table");
        for (const auto& table : m_tables)
            if (table.name == name || table.prefix == prefix)
                throw std::invalid_argument("Table names and prefixes must be unique");
        const auto position = m_tables.size();
        // Numbered as every pool is; no view is made of its indices.
        auto index = [entries = detail::Index<T>(urd::layer { 0 })](const void* value) mutable
        {
            const auto [id, inserted] = entries.insert(*static_cast<const T*>(value));
            return std::pair<size_t, bool>(id.value, inserted);
        };
        std::function<void(Archive&, const void*)> projection;
        if (project)
            projection = [project = std::move(project)](Archive& archive, const void* value) { project(archive, *static_cast<const T*>(value)); };
        m_tables.push_back({ std::move(name), std::move(prefix), {}, std::move(index), std::move(fields), std::move(projection) });
        m_types.emplace(typeid(T).name(), position);
    }

    template<typename T>
    boost::json::value serialize(const T& value)
    {
        check_valid();
        m_started = true;
        try
        {
            return convert(value);
        }
        catch (...)
        {
            m_failed = true;
            throw;
        }
    }

    template<typename T>
    boost::json::array table() const
    {
        check_valid();
        const auto found = m_types.find(typeid(T).name());
        if (found == m_types.end())
            throw std::invalid_argument("Type has no registered dictionary table");
        return m_tables[found->second].rows;
    }

    boost::json::object tables() const
    {
        check_valid();
        boost::json::object result;
        for (const auto& table : m_tables)
            result[table.name] = boost::json::object { { "prefix", table.prefix }, { "rows", table.rows } };
        return result;
    }
};

template<Described T>
void tag_invoke(boost::json::value_from_tag, boost::json::value& result, const T& value, Dictionaries* dictionaries)
{ result = dictionaries->convert(value); }

/// A nonnegative integer of its own type (ygg::UnsignedInteger: a parameter's position, an FDR value): its value.
template<typename T>
    requires std::derived_from<T, ygg::UnsignedInteger<T>>
void tag_invoke(boost::json::value_from_tag, boost::json::value& result, const T& value, Dictionaries* /*dictionaries*/)
{ result = value.value; }

/// An enum that names its values (to_string, found by its namespace): its name.
template<typename E>
    requires std::is_enum_v<E> && requires(E value) { std::string(to_string(value)); }
void tag_invoke(boost::json::value_from_tag, boost::json::value& result, const E& value, Dictionaries* /*dictionaries*/)
{ result = std::string(to_string(value)); }

/// A key not read through its context (an index held as a value, a column's object index): its value, or the values of
/// its parts in order where it has several.
template<typename... Ts>
void tag_invoke(boost::json::value_from_tag, boost::json::value& result, const urd::key<Ts...>& value, Dictionaries* /*dictionaries*/)
{
    if constexpr (sizeof...(Ts) == 1)
        result = value.template get<Ts...>().value;
    else
        result = boost::json::array { value.template get<Ts>().value... };
}

}

#endif
