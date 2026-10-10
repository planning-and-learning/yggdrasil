#ifndef PYYGGDRASIL_DATABASE_QUERY_INPUTS_HPP_
#define PYYGGDRASIL_DATABASE_QUERY_INPUTS_HPP_

#include "yggdrasil/database/relation.hpp"

#include <cstddef>
#include <nanobind/nanobind.h>
#include <span>

namespace yggdrasil::database_python
{
namespace nb = nanobind;
using Relation = ygg::Builder<ygg::database::Relation<>>;
using RelationView = ygg::database::RelationView<>;
using Row = ygg::database::Row<>;

struct BorrowedRelation
{
    const Relation* value;
};

// Dispatch individual reads so mixed Python inputs share one algorithm instantiation.
class RelationInput
{
    const Relation* m_builder = nullptr;
    const RelationView* m_view = nullptr;

    template<typename F>
    decltype(auto) visit(F&& function) const
    {
        if (m_builder)
            return function(*m_builder);
        return function(*m_view);
    }

public:
    explicit RelationInput(const Relation& relation) : m_builder(&relation) {}
    explicit RelationInput(nb::handle object)
    {
        if (nb::isinstance<Relation>(object))
            m_builder = &nb::cast<const Relation&>(object);
        else if (nb::isinstance<RelationView>(object))
            m_view = &nb::cast<const RelationView&>(object);
        else if (nb::isinstance<BorrowedRelation>(object))
            m_builder = nb::cast<const BorrowedRelation&>(object).value;
        else
            throw nb::type_error("Expected Relation, RelationView, or BorrowedRelation.");
    }

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
