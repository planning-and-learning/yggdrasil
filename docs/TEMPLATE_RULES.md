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
   incomplete type" warnings as errors.
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
   in every includer and saves little.
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
    header, and the parts stay in one test executable.

12. **One container for many record types.** Memo tables, visited sets and
    caches keyed by records of every type in a type list use one container
    keyed by (type position, index) instead of a tuple with one container
    per type: each container type instantiates its whole hash table. The
    key carries no dispatch; views are rebuilt from the index.

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
| 7 | yggdrasil `BasicSymbolRepository` members out of class, symbol storage instantiated in loki and tyr | loki max TU 2.2 GB to 1.8 GB, tyr summed memory -4.6% |
| 7 | yggdrasil `BasicSymbolRepository` constructor and destructor out of class | loki max TU 1.80 GB to 1.70 GB; summed memory tyr -7.5 GB, runir -9.6 GB (query evaluation test 5.8 GB to 4.6 GB) |
| 8 | runir DL evaluator compiled in eight library sources | evaluating TUs -0.2 to -1.0 GB |
| 8 | runir Ext evaluator with borrowed register values instantiated in the library instead of five tests | test TUs -0.4 to -2.3 GB (max 7.5 GB to 5.3 GB), two new 2.8 GB sources |
| 9 | runir Uns parser includes extern declarations of the Uns symbol storage | 3.95 GB to 2.89 GB |
| 9 | runir optimization, query evaluation and successor expander tests include the lists of the families they intern | -0.3 to -0.5 GB per TU; the same lists cost +0.1 to +0.7 GB in TUs that only evaluate |
| 9 | pyrunir bindings that intern or evaluate include their family's lists and the denotation list | 24 binding TUs -0.2 to -1.5 GB, summed pyrunir memory -22 GB; the lists cost up to +0.4 GB in bindings that only wrap views |
| 11 | tyr applicability, serialization and datalog solver tests split along task kind and test groups | 3.05-3.3 GB to at most 2.95 GB |
| 11 | the runir optimization test split by task kind | no change (7.1 GB per half); the cost depends on the family |
| 11 | runir DL evaluator and incremental engine merged from 16 sources (family x kind x context) into one per family | 2.8-3.6 GB per part, 3.5-4.0 GB per family; summed build memory 334 GB to 299 GB, wall 403 s to 370 s |
| 14 | runir program executor data headers without the proof graph and plan definitions | 1.4 GB to 0.5 GB on their own, no change in the build: every includer uses the graph; reverted |
| 12 | runir DL and ps optimizers memoize in one map instead of one per record type | optimization test 6.4 GB to 4.2 GB; pyrunir bindings that optimize -1.1 to -3.0 GB each (max 8.8 GB to 5.8 GB) |
| 14 | yggdrasil `BasicRelationRepository` members out of class with extern declarations in loki and tyr | summed memory +0.1 GB loki, +1.2 GB tyr, +0.8 GB runir: few TUs use the relation storage; reverted |
| 14 | runir `EvaluationPolicyConcept` in its own concepts header | `ps/rule_evaluator_concepts.hpp` 2.1 GB to 0.8 GB on its own, no change in the build: every includer needs the policies; reverted |
| 14 | yggdrasil `BuilderStorage` constructor and destructor out of class, builders declared extern in loki and tyr (global) and runir (opt-in) | tyr summed memory +7.9 GB (123 TUs heavier, max 2.80 GB to 2.64 GB), runir +4.4 GB, opt-in sweep +0.2 to +1.3 GB in every heavy runir TU: the extern class instantiates every pool; reverted |
| 11 | runir symbol storage sources kept per repository and family | merging three costs as much as the parts (44-57 s, 4-5 GB) |
