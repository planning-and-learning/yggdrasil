# Template and include rules

Compile cost in yggdrasil, loki, tyr and runir is dominated by the C++ front
end: template instantiation and the headers that trigger it, not
optimization or LTO. These rules keep translation units (TUs) small. They
apply to all four repositories and are maintained only here; the other
repositories link to this file.

Goal: minimal compiler memory (max RSS of `cc1plus`) and time per TU in a
Release + LTO build with GCC, with a coherent structure that every repository
follows.

## Headers

1. **Declaration headers declare.** `declarations.hpp` contains forward
   declarations, aliases, enums and tags. It includes only other
   `declarations.hpp` or `*_concepts.hpp` headers and light standard or
   yggdrasil core headers (`yggdrasil/core/*`, `yggdrasil/formalism/builder.hpp`).
2. **Concepts used as constraints live in `<name>_concepts.hpp`**, next to the
   header that implements them, with no implementation includes. Forward
   declarations that own default template arguments (`Node<Kind, State = ...>`)
   live there too; the definition then omits the defaults.
3. **Data headers hold data.** `*_data.hpp` defines `ygg::Data<T>` with indices
   and values only. Helpers that take views are templates over the view
   context, or live in the view header.
4. **Hold heavy types by pointer or reference with declaration-only includes.**
   Where the header otherwise keeps the pointee incomplete, dereference a
   smart pointer with `*ptr.get()`, not `*ptr`, in inline code: the overloaded
   `operator*` makes argument-dependent lookup complete every template
   argument of the `shared_ptr`, which instantiates the pointee. Raw pointers
   and pointees that are complete anyway are not affected. A header that
   uses a view by value (`std::optional<ClassifierView>`) includes the
   repository of that view: a view specialization constrained on an
   incomplete repository silently falls back to the primary template, and
   GCC keeps that choice for the rest of the TU. Code in a template that
   uses a held type in a non-dependent expression (`m_builder.get_workspace()`
   with `Builder& m_builder`) is checked at the definition and needs the
   type complete: move such uses into small non-template functions defined
   in the library, or include the definition. Treat GCC's "invalid use of
   incomplete type" warnings as errors. A class may also hold a
   `std::vector` of an incomplete type: declare the members that need the
   element complete (size, element access, move assignment, destructor) and
   define them out of class with the other hidden definitions (rule 8).
5. **Include what you use.** A header compiles on its own. Library code,
   tests and executables include the components they use, never umbrella
   headers such as `tyr/planning/planning.hpp`. This includes the formatter
   headers of every type that is formatted (`tyr/planning/formatter.hpp`,
   `yggdrasil/formatting/cista_formatters.hpp`).
6. **Pimpl only at high-level entities** (heuristics, state repositories,
   executors), never in views, repositories, interning or evaluation.

## Instantiation

7. **Explicitly instantiate when the same instantiation is repeated in many
   TUs and the declaration is cheap.** Define the members out of class:
   members defined in the class body are implicitly inline and GCC still
   instantiates them in every TU that uses them, despite `extern template`.
   This includes the constructors and the destructor: declare them in the
   class and define them out of class (`~T();` and
   `template<...> T<...>::~T() = default;`), or every TU that creates or
   destroys an object instantiates the destructors of all its members.
   Instantiate the whole class (`template class T<...>;`), not only its
   out-of-class members: under `extern template`, GCC also calls inline
   members it does not inline, so the library must provide them too.
8. **Prefer hiding definitions over `extern template`.** Put the definitions
   in `detail/<name>_impl.hpp`, include it only from the instantiating
   sources (and under `<PROJECT>_HEADER_INSTANTIATION`), and instantiate in
   the library. Users then only link. Headers that the definitions need
   include the declarations as `<name>_decl.hpp`, and only the public
   `<name>.hpp` adds the definitions under `<PROJECT>_HEADER_INSTANTIATION`,
   so that build mode has no include cycle. Build it once after moving
   definitions. An `extern template` declaration of a
   class instantiates the class definition in every includer, so long extern
   lists or externs over heavy classes cost more than they save.
9. **Make extern lists opt-in when the definitions must stay visible.** Put
   `extern template` declarations for widely shared instantiations (such as
   `BasicSymbolRepository<T>` of every symbol type) in a separate
   `detail/<name>_instantiations.hpp` that only the TUs doing that work
   include, e.g. parsers and translators that intern every type. Including
   such a list everywhere costs every light TU the class instantiations.
   Keep the list in the main header instead when nearly every includer does
   the work: in tyr, removing it from the repository headers made 60 TUs
   heavier and one lighter. Decide by measuring both, in the full build: a
   standalone probe of the same command can show a gain the build does not.
   Extern lists pay off for classes whose member bodies are heavy and used
   by many TUs (interning: hashing, lookup, insertion). They lose for
   classes whose cost is the class definition itself (object pools, builder
   storage, relation storage): the declaration instantiates the definition
   in every includer and saves little. The instantiating sources of a
   list also lengthen the library's LTO link, a serial step of the build;
   peak build memory decides: drop a list only if that does not raise
   the peak at the build's parallelism.
10. **No deduced return types on functions that are declared `extern` or used
   only through declarations**: deducing the type instantiates the body.
11. **Split instantiating sources only along the dimension that drives their
    cost** (family, task kind, context or constructor category) so no single
    TU dominates, even if the summed memory grows by the repeated
    includes. Test a dimension by compiling the parts merged: if the merged
    source costs about as much as one part, the parts share their
    instantiations and the split only compiles them twice, so merge them
    as long as the merged source stays below the heaviest TU of the build.
    Tests split the same way: shared fixtures go to a `<test>_fixtures.hpp`
    header, and the parts stay in one test executable. A split lowers the
    heaviest TU, but the peak build memory only if the heavy TUs no longer
    overlap: the parts fill the parallel slots. Keep a split only if the
    peak drops (identical builds vary by about 1 GB at -j12, so compare
    two runs each).

12. **One container for many record types.** Memo tables, visited sets and
    caches keyed by records of every type in a type list use one container
    keyed by (type position, index) instead of a tuple with one container
    per type: each container type instantiates its whole hash table. The
    key carries no dispatch; views are rebuilt from the index.

15. **No all-types invocability constraints on internal visitors over long
    type lists.** `InvocableWithEach<F, List>` (or `std::invocable<F, T>`
    for each `T`) on a generic lambda instantiates the lambda body for every
    type in the list, at every instantiation of the constrained function: a
    recursive visitor over N record types costs N x N body instantiations
    although each node visits only its few children. Constrain the public
    entry point if a diagnostic is wanted; leave internal helpers that only
    forward the visitor unconstrained.

16. **Dispatch your own types before a generic library's trait-based
    dispatch.** `boost::json::value_from` computes every conversion category
    (user, native, generic: sequence, map, tuple, optional, described class)
    of every nested type it converts, even when a `tag_invoke` overload
    matches. For types the library describes itself (yggdrasil's
    `describe_fields`), and ranges and optionals of them, convert directly
    and hand only leaf types to the generic library. Types serialized as
    another object declare `serialize_as(Dictionaries&, const T&)` instead
    of a competing `tag_invoke`.

17. **Check compile-time contracts where the instantiation already exists.**
    A `static_assert` over overload resolution against a heavy class
    (`InternsData`, `rejects_symbol` on a repository) instantiates that
    class. In a test that checks many repositories it re-instantiates all of
    them; in the library source that explicitly instantiates the repository
    it costs almost nothing. Put such contracts next to the explicit
    instantiation, with the shared contract concepts in a `detail` header;
    tests keep the contracts on light types.

## Process

13. **Measure before and after.** Use the standalone cost of a header
    (`-fsyntax-only` of a TU that only includes it), the cost of a single API
    call (the TU's includes plus that call, at `-fsyntax-only` and at `-O3`),
    and a full instrumented build logging the max RSS per TU.
14. **Keep only what wins in the full build.** A smaller standalone header
    often does not shrink its includers, which may instantiate the same code
    anyway. Revert changes that do not lower the max or summed memory of the
    affected TUs.

## Examples

| Rule | Change | Effect |
| --- | --- | --- |
| 1 | `tyr/analysis/declarations.hpp` stopped including the formalism repositories | 1.0 GB to 0.3 GB per includer (113 TUs) |
| 2 | `tyr/planning/state_view_concepts.hpp` | `StateViewConcept` costs 0.2 GB instead of 1.1 GB |
| 3 | `runir/kr/dl/semantics/register_values_data.hpp` | 0.75 GB to 0.26 GB |
| 4 | runir constructor and denotation repositories hold the tyr repository by pointer | `runir/kr/dl/repository.hpp` 0.75 GB to 0.47 GB |
| 5 | tyr statistics and heuristic tests include only the tested component | 2.5 GB to 2.0 GB per test TU |
| 5 | pyrunir serialization bindings include their component's type list and serializers instead of `runir/serialization/{types,serialization}.hpp` (1.65 GB each); the type lists split into `runir/serialization/types/*.hpp` | 20 DL, denotation and Uns bindings -0.9 to -1.0 GB, ps bindings -0.25 to -0.62 GB, identical symbols; pyrunir summed memory -20 GB, peak at -j12 29.6/31.0 GB to 27.8/26.8 GB, same wall time |
| 15 | runir DL `QueryOptimizer::for_each_child` without the `InvocableWithEach` constraint over all record types | pyrunir DL semantics and feature bindings -0.5 to -0.67 GB each (4.6 GB to 4.1 GB), optimization test -0.12 GB; pyrunir summed memory -5 GB, peak at -j12 26.4/28.2 GB to 26.3/25.6 GB |
| 16 | yggdrasil `Dictionaries::convert` serializes described types and their ranges and optionals directly; runir's bidirectional graph uses `serialize_as` | pyrunir `graphs` binding 4.65 GB to 3.88 GB and 40 s to 36 s, execution and ps bindings -0.18 to -0.28 GB, DL bindings about -0.12 GB; same JSON (new nested test passes with the old and new header) |
| 17 | runir constructor repository interning contracts moved from the DL family traits test into the nine `repository_*.cpp` sources | test 4.69 GB to 2.72 GB and 31 s to 17 s (the heaviest runir TU); repository sources +0 to +9 MB |
| 17 | runir DL type tests' view and data contracts (one per constructor type) probed in the repository source | `repository_ext.cpp` +0.95 GB for the Ext third: the library does not instantiate the views, so the cost only moves; not applied |
| 6 | tyr planning `Parser` keeps its loki state behind a pointer | tyr summed memory -24.5 GB (38 TUs up to -0.74 GB), runir -12 GB |
| 7 | yggdrasil `BasicSymbolRepository` members out of class, symbol storage instantiated in loki and tyr | loki max TU 2.2 GB to 1.8 GB, tyr summed memory -4.6% |
| 7 | yggdrasil `BasicSymbolRepository` constructor and destructor out of class | loki max TU 1.80 GB to 1.70 GB; summed memory tyr -7.5 GB, runir -9.6 GB (query evaluation test 5.8 GB to 4.6 GB) |
| 7 | runir `EvaluationGraph` and `IncrementalEvaluation` destructors out of class | 8 TUs -57 to -113 MB |
| 8 | runir DL evaluator compiled in eight library sources | evaluating TUs -0.2 to -1.0 GB |
| 8 | runir Ext evaluator with borrowed register values instantiated in the library instead of five tests | test TUs -0.4 to -2.3 GB (max 7.5 GB to 5.3 GB), two new 2.8 GB sources |
| 8 | runir Ext query evaluator over builder states instantiated in the library instead of a test | successor expander test 4.5 GB to 4.0 GB, library source +44 MB; tests that pass their own callbacks to member templates (`for_each_successor`) keep including the definitions |
| 4 | runir incremental `EvaluationGraph` holds its nodes as a vector of incomplete types; the accessors that read nodes are defined in `detail/evaluation_impl.hpp` and instantiated in the library, `SetOperationWorkspace` has its own header, and the delta policy reaches the database workspace through a library function | `incremental/evaluation.hpp` 1.93 GB to 0.82 GB on its own; executors -0.27 and -0.35 GB, executor and evaluation tests -0.15 to -1.4 GB; peak at -j12 25.7/25.1 GB to 23.5/24.9 GB, same wall time |
| 9 | runir Uns parser includes extern declarations of the Uns symbol storage | 3.95 GB to 2.89 GB |
| 9 | runir optimization, query evaluation and successor expander tests include the lists of the families they intern | -0.3 to -0.5 GB per TU; the same lists cost +0.1 to +0.7 GB in TUs that only evaluate |
| 9 | pyrunir bindings that intern or evaluate include their family's lists and the denotation list | 24 binding TUs -0.2 to -1.5 GB, summed pyrunir memory -22 GB; the lists cost up to +0.4 GB in bindings that only wrap views |
| 9 | runir grammar and CNF grammar lists dropped, then restored | link -56 s and wall 379 s to 342 s, but peak at -j12 26.1 GB to 28.5 GB; restored because peak memory limits the build |
| 9 | second runir sweep over all TUs above 1.4 GB without lists | ten more TUs -65 to -534 MB (domain_context, parser, structural termination, classifier and executor tests); none between 1.4 and 2 GB gained more than 100 MB |
| 11 | tyr applicability, serialization and datalog solver tests split along task kind and test groups | 3.05-3.3 GB to at most 2.95 GB |
| 11 | the runir optimization test split by task kind | no change (7.1 GB per half); the cost depends on the family |
| 11 | runir DL evaluator and incremental engine merged from 16 sources (family x kind x context) into one per family | 2.8-3.6 GB per part, 3.5-4.0 GB per family; summed build memory 334 GB to 299 GB, wall 403 s to 370 s |
| 14 | runir program executor data headers without the proof graph and plan definitions | 1.4 GB to 0.5 GB on their own, no change in the build: every includer uses the graph; reverted |
| 12 | runir DL and ps optimizers memoize in one map instead of one per record type | optimization test 6.4 GB to 4.2 GB; pyrunir bindings that optimize -1.1 to -3.0 GB each (max 8.8 GB to 5.8 GB) |
| 14 | runir `DenotationCaches` with one map for the four category caches (rule 12) | evaluating library sources -38 to -54 MB (1.5%): the query and join-index caches dominate the class; tests check per-category entries, which one map can answer only with extra counters on the hot path; reverted |
| 14 | yggdrasil `BasicRelationRepository` members out of class with extern declarations in loki and tyr | summed memory +0.1 GB loki, +1.2 GB tyr, +0.8 GB runir: few TUs use the relation storage; reverted |
| 14 | runir `EvaluationPolicyConcept` in its own concepts header | `ps/rule_evaluator_concepts.hpp` 2.1 GB to 0.8 GB on its own, no change in the build: every includer needs the policies; reverted |
| 14 | yggdrasil `BuilderStorage` constructor and destructor out of class, builders declared extern in loki and tyr (global) and runir (opt-in) | tyr summed memory +7.9 GB (123 TUs heavier, max 2.80 GB to 2.64 GB), runir +4.4 GB, opt-in sweep +0.2 to +1.3 GB in every heavy runir TU: the extern class instantiates every pool; reverted |
| 11 | runir DL grammar parser instantiations split by family | 4.2 GB, 64 s to 2.5-2.8 GB, 25-28 s per family; summed memory +3.7 GB |
| 14 | runir symbol storage instantiated member by member (only out-of-class members) to shorten the library LTO link | link 240 s to 225 s, but tests failed to link: inline members such as `at_local` are called out of line; reverted |
| 14 | runir `datasets/declarations.hpp` so the task context does not include the task search context | `task_context.hpp` 1.7 GB to 1.4 GB on its own, +175 MB in the build: its includers need the search context; reverted |
| 14 | runir DL optimizer behind a pointer and compiled once per family in the library (rules 6 and 8) | pyrunir summed memory -18 GB and TUs above 3 GiB 37 to 20, but peak at -j12 runir 26.4 GB to 28.9 GB, pyrunir 28.6 GB to 29.4 GB; reverted |
| 14 | the same with each family's source split into optimizer and statistics halves (rule 11) | runir peak 26.6 GB to 26.9 GB and wall 333 s to 359 s, pyrunir peak 28.6 GB to 30.0 GB; reverted: new library sources raise the peak while other heavy TUs compile |
| 11 | runir family traits, query evaluation and query allocation tests split along their test groups | heaviest TU 4.7 GB to 4.2 GB, but peak at -j12 24.6/26.0 GB before and 25.7/26.4 GB after, same wall time; reverted |
| 11 | runir symbol storage sources kept per repository and family | merging three costs as much as the parts (44-57 s, 4-5 GB) |
