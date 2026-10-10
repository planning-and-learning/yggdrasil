#ifndef PYYGGDRASIL_DATABASE_QUERY_INPUTS_HPP_
#define PYYGGDRASIL_DATABASE_QUERY_INPUTS_HPP_

#include "yggdrasil/database/semantics/relation.hpp"

#include <cstddef>
#include <nanobind/nanobind.h>
#include <span>
#include <variant>
#include <vector>

namespace yggdrasil::database_python
{
namespace nb = nanobind;
using Relation = ygg::Builder<ygg::database::Relation<>>;
using RelationView = ygg::database::RelationView<>;
using Row = ygg::database::Row<>;

inline std::vector<ygg::Index<ygg::database::Column>> column_labels(const std::vector<ygg::uint_t>& labels)
{
    std::vector<ygg::Index<ygg::database::Column>> result;
    result.reserve(labels.size());
    for (const auto label : labels)
        result.emplace_back(label);
    return result;
}

struct BorrowedRelation
{
    const Relation* value;
};

// Dispatch individual reads so mixed Python inputs share one algorithm instantiation.
class RelationInput
{
    std::variant<const Relation*, const RelationView*> m_relation;

    static std::variant<const Relation*, const RelationView*> borrow(nb::handle object)
    {
        if (nb::isinstance<Relation>(object))
            return &nb::cast<const Relation&>(object);
        if (nb::isinstance<RelationView>(object))
            return &nb::cast<const RelationView&>(object);
        if (nb::isinstance<BorrowedRelation>(object))
            return nb::cast<const BorrowedRelation&>(object).value;
        throw nb::type_error("Expected Relation, RelationView, or BorrowedRelation.");
    }
    template<typename F>
    decltype(auto) visit(F&& function) const
    {
        return std::visit([&](const auto* relation) -> decltype(auto) { return function(*relation); }, m_relation);
    }

public:
    explicit RelationInput(const Relation& relation) : m_relation(&relation) {}
    explicit RelationInput(nb::handle object) : m_relation(borrow(object)) {}

    auto columns() const
    {
        return visit([this](const auto& relation) { return ygg::make_view(relation.columns().get_data(), *this); });
    }
    size_t arity() const
    {
        return visit([](const auto& relation) { return relation.arity(); });
    }
    size_t size() const
    {
        return visit([](const auto& relation) { return relation.size(); });
    }
    bool empty() const
    {
        return visit([](const auto& relation) { return relation.empty(); });
    }
    const void* get_storage_address() const
    {
        return visit([](const auto& relation) { return relation.get_storage_address(); });
    }
    size_t get_storage_index() const
    {
        return visit([](const auto& relation) { return relation.get_storage_index(); });
    }
    size_t column_index(ygg::Index<ygg::database::Column> column) const
    {
        return visit([column](const auto& relation) { return relation.column_index(column); });
    }
    std::span<const std::byte> row(size_t index) const
    {
        return visit([index](const auto& relation) { return relation.row(index); });
    }
    bool contains(std::span<const std::byte> row) const
    {
        return visit([row](const auto& relation) { return relation.contains(row); });
    }
    bool contains(Row row) const
    {
        return visit([row](const auto& relation) { return relation.contains(row); });
    }
};

static_assert(ygg::database::RelationViewConcept<RelationInput, ygg::database::DefaultColumnTypes>);
}  // namespace yggdrasil::database_python

#endif
