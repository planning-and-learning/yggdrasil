#include "module.hpp"
#include "yggdrasil/database/datalog/construction.hpp"
#include "yggdrasil/database/datalog/evaluator.hpp"
#include "yggdrasil/python/bindings.hpp"
#include "yggdrasil/python/type_casters.hpp"

#include <nanobind/stl/optional.h>
#include <nanobind/stl/pair.h>
#include <nanobind/stl/vector.h>
#include <optional>
#include <vector>

namespace yggdrasil
{
namespace
{
namespace dl = ygg::database::datalog;
using Repository = dl::DatalogRepository;

template<typename T>
using DatalogView = ygg::View<ygg::Index<T>, Repository>;

template<typename Index, typename V>
void set_indices(ygg::IndexList<Index>& target, const std::vector<V>& values)
{
    target.clear();
    for (const auto& value : values)
        target.push_back(value.get_index());
}
}  // namespace

void bind_database_datalog(nb::module_& m)
{
    using namespace nb::literals;

    nb::enum_<dl::AggregateFunction>(m, "DatalogAggregateFunction")
        .value("COUNT", dl::AggregateFunction::COUNT)
        .value("SUM", dl::AggregateFunction::SUM)
        .value("MIN", dl::AggregateFunction::MIN)
        .value("MAX", dl::AggregateFunction::MAX);

    auto term = nb::class_<dl::Term>(m, "DatalogTerm", "A variable, numbered within its rule, or an object constant.");
    term.def_static("variable", &dl::Term::variable, "variable"_a)
        .def_static("constant", &dl::Term::constant, "object"_a)
        .def("is_variable", &dl::Term::is_variable)
        .def_ro("value", &dl::Term::value);
    ygg::add_comparison(term);

    nb::enum_<dl::Comparison::Kind>(m, "DatalogComparisonKind")
        .value("EQUAL", dl::Comparison::Kind::EQUAL)
        .value("NOT_EQUAL", dl::Comparison::Kind::NOT_EQUAL)
        .value("LESS", dl::Comparison::Kind::LESS)
        .value("LESS_EQUAL", dl::Comparison::Kind::LESS_EQUAL);
    nb::class_<dl::Comparison>(m, "DatalogComparison")
        .def(
            "__init__",
            [](dl::Comparison* self, dl::Comparison::Kind kind, dl::Term lhs, dl::Term rhs) { new (self) dl::Comparison { kind, lhs, rhs }; },
            "kind"_a,
            "lhs"_a,
            "rhs"_a)
        .def_ro("kind", &dl::Comparison::kind)
        .def_ro("lhs", &dl::Comparison::lhs)
        .def_ro("rhs", &dl::Comparison::rhs);

    nb::enum_<dl::Arithmetic::Operation>(m, "DatalogArithmeticOperation")
        .value("ADD", dl::Arithmetic::Operation::ADD)
        .value("SUBTRACT", dl::Arithmetic::Operation::SUBTRACT)
        .value("MULTIPLY", dl::Arithmetic::Operation::MULTIPLY)
        .value("DIVIDE", dl::Arithmetic::Operation::DIVIDE)
        .value("MIN", dl::Arithmetic::Operation::MIN)
        .value("MAX", dl::Arithmetic::Operation::MAX);
    nb::class_<dl::Arithmetic>(m, "DatalogArithmetic", "result = lhs operation rhs over saturating numbers; the largest value is infinity.")
        .def(
            "__init__",
            [](dl::Arithmetic* self, dl::Arithmetic::Operation operation, dl::Term lhs, dl::Term rhs, ygg::uint_t result)
            { new (self) dl::Arithmetic { operation, lhs, rhs, result }; },
            "operation"_a,
            "lhs"_a,
            "rhs"_a,
            "result"_a)
        .def_ro("operation", &dl::Arithmetic::operation)
        .def_ro("lhs", &dl::Arithmetic::lhs)
        .def_ro("rhs", &dl::Arithmetic::rhs)
        .def_ro("result", &dl::Arithmetic::result);

    nb::class_<dl::DatalogRepositoryFactory>(m, "DatalogRepositoryFactory", "Creates Datalog repositories with distinct identities within this factory.")
        .def(nb::init<>())
        .def("create", [](dl::DatalogRepositoryFactory& factory) { return new Repository(factory.create()); }, nb::rv_policy::take_ownership);
    auto repository = nb::class_<Repository>(m, "DatalogRepository", "Interns validated Datalog relations, atoms, aggregates, rules and programs.");

    {
        using V = ygg::Data<dl::Relation>;
        ygg::bind_data<V>(m, "DatalogRelationData")
            .def(
                "__init__",
                [](V* self, size_t id, size_t arity, std::optional<size_t> input_slot, const std::string& name)
                { new (self) V(id, arity, input_slot.value_or(V::derived), name); },
                "id"_a,
                "arity"_a,
                "input_slot"_a = std::nullopt,
                "name"_a = "",
                "An input relation (with its input slot) or a derived relation.");
        using W = DatalogView<dl::Relation>;
        auto cls = nb::class_<W>(m, "DatalogRelation");
        cls.def("get_index", &W::get_index)
            .def("get_id", &W::get_id)
            .def("get_arity", &W::get_arity)
            .def("get_name", [](const W& self) { return std::string(self.get_name().view()); })
            .def("is_input", &W::is_input)
            .def("get_input_slot", &W::get_input_slot);
        ygg::add_comparison(cls);
        ygg::add_hash(cls);
        ygg::bind_insert<dl::Relation>(repository);
    }
    {
        using V = ygg::Data<dl::Atom>;
        ygg::bind_data<V>(m, "DatalogAtomData")
            .def(
                "__init__",
                [](V* self, DatalogView<dl::Relation> relation, const std::vector<dl::Term>& terms)
                {
                    new (self) V();
                    self->relation = relation.get_index();
                    self->terms.set(terms.begin(), terms.end());
                },
                "relation"_a,
                "terms"_a);
        using W = DatalogView<dl::Atom>;
        auto cls = nb::class_<W>(m, "DatalogAtom");
        cls.def("get_index", &W::get_index)
            .def("get_relation", &W::get_relation, nb::keep_alive<0, 1>())
            .def("get_terms", [](const W& self) { return std::vector<dl::Term>(self.get_terms().begin(), self.get_terms().end()); });
        ygg::add_comparison(cls);
        ygg::add_hash(cls);
        ygg::bind_insert<dl::Atom>(repository);
    }
    {
        using V = ygg::Data<dl::Aggregate>;
        ygg::bind_data<V>(m, "DatalogAggregateData")
            .def(
                "__init__",
                [](V* self,
                   dl::AggregateFunction function,
                   ygg::uint_t result,
                   ygg::uint_t value,
                   const std::vector<DatalogView<dl::Atom>>& atoms,
                   const std::vector<DatalogView<dl::Atom>>& negated,
                   const std::vector<dl::Comparison>& comparisons,
                   const std::vector<dl::Arithmetic>& arithmetic)
                {
                    new (self) V();
                    self->function = function;
                    self->result = result;
                    self->value = value;
                    set_indices(self->atoms, atoms);
                    set_indices(self->negated, negated);
                    self->comparisons.set(comparisons.begin(), comparisons.end());
                    self->arithmetic.set(arithmetic.begin(), arithmetic.end());
                },
                "function"_a,
                "result"_a,
                "value"_a,
                "atoms"_a,
                "negated"_a = std::vector<DatalogView<dl::Atom>> {},
                "comparisons"_a = std::vector<dl::Comparison> {},
                "arithmetic"_a = std::vector<dl::Arithmetic> {},
                "result = function value : atoms, arithmetic, not negated, comparisons; COUNT ignores value.");
        using W = DatalogView<dl::Aggregate>;
        auto cls = nb::class_<W>(m, "DatalogAggregate");
        cls.def("get_index", &W::get_index).def("get_function", &W::get_function).def("get_result", &W::get_result).def("get_value", &W::get_value);
        ygg::add_comparison(cls);
        ygg::add_hash(cls);
        ygg::bind_insert<dl::Aggregate>(repository);
    }
    {
        using V = ygg::Data<dl::Rule>;
        ygg::bind_data<V>(m, "DatalogRuleData")
            .def(
                "__init__",
                [](V* self,
                   DatalogView<dl::Atom> head,
                   const std::vector<DatalogView<dl::Atom>>& atoms,
                   const std::vector<DatalogView<dl::Atom>>& negated,
                   const std::vector<dl::Comparison>& comparisons,
                   const std::vector<DatalogView<dl::Aggregate>>& aggregates,
                   const std::vector<dl::Arithmetic>& arithmetic)
                {
                    new (self) V();
                    self->head = head.get_index();
                    set_indices(self->atoms, atoms);
                    set_indices(self->negated, negated);
                    self->comparisons.set(comparisons.begin(), comparisons.end());
                    set_indices(self->aggregates, aggregates);
                    self->arithmetic.set(arithmetic.begin(), arithmetic.end());
                },
                "head"_a,
                "atoms"_a,
                "negated"_a = std::vector<DatalogView<dl::Atom>> {},
                "comparisons"_a = std::vector<dl::Comparison> {},
                "aggregates"_a = std::vector<DatalogView<dl::Aggregate>> {},
                "arithmetic"_a = std::vector<dl::Arithmetic> {});
        using W = DatalogView<dl::Rule>;
        auto cls = nb::class_<W>(m, "DatalogRule");
        cls.def("get_index", &W::get_index).def("get_head", &W::get_head, nb::keep_alive<0, 1>());
        ygg::add_comparison(cls);
        ygg::add_hash(cls);
        ygg::bind_insert<dl::Rule>(repository);
    }
    {
        using V = ygg::Data<dl::Program>;
        ygg::bind_data<V>(m, "DatalogProgramData")
            .def(
                "__init__",
                [](V* self, const std::vector<DatalogView<dl::Rule>>& rules)
                {
                    new (self) V();
                    set_indices(self->rules, rules);
                },
                "rules"_a);
        using W = DatalogView<dl::Program>;
        auto cls = nb::class_<W>(m, "DatalogProgram");
        cls.def("get_index", &W::get_index);
        ygg::add_comparison(cls);
        ygg::add_hash(cls);
        ygg::bind_insert<dl::Program>(repository);
    }

    {
        using E = dl::Evaluator;
        using Borrowed = ygg::database::BorrowedRelationView;
        const auto borrow = [](const ygg::Builder<ygg::database::Relation>& relation) { return Borrowed(relation, borrowed_relation_context()); };
        auto cls = nb::class_<E>(m,
                                 "DatalogEvaluator",
                                 "Evaluate a program's relations rule by rule (semi-naive), and maintain them under input changes (counting, "
                                 "Delete and Rederive). Static input slots are read once.");
        cls.def(
               "__init__",
               [](E* self, DatalogView<dl::Program> program, const std::vector<DatalogView<dl::Relation>>& outputs, const std::vector<size_t>& static_slots)
               { new (self) E(program, outputs, static_slots); },
               "program"_a,
               "outputs"_a,
               "static_slots"_a = std::vector<size_t> {})
            .def(
                "get_result",
                [borrow](const E& evaluator, size_t output) { return borrow(evaluator.get_result(output)); },
                "output"_a = 0,
                nb::keep_alive<0, 1>(),
                "Borrowed result; the next evaluate, initialize or update invalidates it.")
            .def(
                "get_delta",
                [](const E& evaluator, size_t output) -> const ygg::database::Delta& { return evaluator.get_delta(output); },
                "output"_a = 0,
                nb::rv_policy::reference_internal)
            .def("memory_usage", &E::memory_usage);
        using Inputs = std::vector<Borrowed>;
        using Changes = std::vector<std::pair<Borrowed, Borrowed>>;
        cls.def("evaluate", &E::template evaluate<Inputs>, "inputs"_a)
            .def("initialize", &E::template initialize<Inputs>, "inputs"_a)
            .def("update", &E::template update<Changes>, "changes"_a, "One exact (added, removed) pair per input slot.");
    }
}
}  // namespace yggdrasil
