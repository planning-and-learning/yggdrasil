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
   incomplete type" warnings as errors. The same holds for calls: a call to a
   function template with non-dependent arguments inside a template, or any
   template call in a non-template inline function, instantiates the callee
   in every includer even if the enclosing function is never used. Route
   heavy ones (a query compiler, canonicalization) through a declared
   library function. `assert` bodies count too: they compile only in Debug
   builds, so build Debug once after such changes. A class may also hold a
   `std::vector` of an incomplete type: declare the members that need the
   element complete (size, element access, move assignment, destructor) and
   define them out of class with the other hidden definitions (rule 8).
5. **Include what you use.** A header compiles on its own. Library code,
   tests and executables include the components they use, never umbrella
   headers such as `tyr/planning/planning.hpp`. This includes the formatter
   headers of every type that is formatted (`tyr/planning/formatter.hpp`,
   `yggdrasil/formatting/cista_formatters.hpp`).
   Measure third-party headers on their own before including them from a
   widely used header; a small utility can carry a large include tree.
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
   lists or externs over heavy classes cost more than they save. Compile a
   shared library with `-fno-semantic-interposition` (GCC): otherwise calls
   between its own exported functions go through the PLT and are not inlined,
   so small hot functions moved out of headers become calls.
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
   the peak at the build's parallelism. Opt-in lists rely on the library
   exporting every instantiated member; ld64's LTO drops weak explicit
   instantiations that nothing inside the dylib calls, so a library whose
   includers call members it never calls itself builds without LTO on
   macOS.
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

18. **Template on what the code depends on, not on what it passes along.** A
    parameter that only forwards to other templates multiplies every
    instantiation by its values and keeps the code out of the library. When
    the data is uniform (encoded bytes, indices) and only typed access needs
    the type, give the type its own identity (a constant of its codec or
    traits, not its position in a list) and keep the type list at the edges
    that turn a runtime type back into a value type. Once the parameter is
    gone, the code is no longer a template: move its definitions into the
    library (rule 8), or every includer compiles them (rule 4). The parameter
    also deferred checking: expressions that no longer depend on a template
    parameter are checked where they are written, so they need complete types
    there (an include, a deduced parameter such as the repository type, or a
    library definition), and a recursive structure must define a type before
    the members that hold it by value.

19. **Split storage templates into a read header and a write impl.** When the
    write path of a widely included storage template (insertion,
    serialization) needs a dependency its read path does not, keep the class
    and the read API (`at`, `operator[]`, `size`, iteration, views) in
    `<name>.hpp` and define the whole write API (`insert*`, `create*`,
    `complete_miss*`, `append*`, `serialize`) in `detail/<name>_impl.hpp`.
    Split along this boundary, not along the individual functions that need
    the dependency, and only when the measured difference is large. The class
    comment names the impl header. Sources with the explicit instantiation
    lists include the impl, `<PROJECT>_HEADER_INSTANTIATION` builds include
    it in the `#else` branch of the extern list, and code that interns types
    nobody else instantiates adds explicit instantiations in one source.
    When a member only serves the write side (the value-to-index hash table
    of an interning set), declare its class in the header, define it in the
    impl and hold it by an owning raw pointer that the impl creates and
    deletes: the read header then never instantiates it. Not by
    `std::unique_ptr` in a template instantiated per record type: it
    instantiates `unique_ptr`, `__uniq_ptr_impl` and a `std::tuple` for every
    element type. Value lookup (`find*`), construction, destruction,
    `clear` and memory accounting go to the impl with it. Hold what reads
    need by value, so the indirection moves off the read path. Define these
    members out of class: functions defined in the class body are implicitly
    inline, and an extern template declaration does not stop their
    instantiation in the includer.

20. **Keep `std::tuple` and `std::unique_ptr` out of per-type code.** A
    `std::tuple` of N types instantiates N levels of `_Tuple_impl` and the
    constraint traits of its constructors, and `std::unique_ptr` stores its
    pointer in a `std::tuple`. In code instantiated for every record type or
    over long type lists, hold an owned object by raw pointer and keep the
    ownership in the out-of-class special members (rule 19), keep one object
    per type in flat bases (`struct Slots : Slot<Ts>... {}`, listed in reverse
    to keep `std::tuple`'s destruction order: pooled objects may own handles
    into the pools of later types), and list record fields as member pointers
    (`static constexpr auto cista_members() noexcept { return
    ygg::Members<&Data::index, &Data::a> {}; }`; our cista fork folds over a
    static `cista_members()` that returns such a pack) so that serialization,
    hashing, child visits and `clear()` fold over the pointers
    (`ygg::for_each_member`, `ygg::clear_members`) instead of `std::tie` and
    `std::apply`. A `cista_members()` that returns `std::tie` in a non-template
    record compiles in every includer. Find such costs with clang's
    `-ftime-trace` over the heaviest TUs, summed by template name.

## Process

13. **Measure before and after.** Use the standalone cost of a header
    (`-fsyntax-only` of a TU that only includes it), the cost of a single API
    call (the TU's includes plus that call, at `-fsyntax-only` and at `-O3`),
    and a full instrumented build logging the max RSS per TU. GCC's peak RSS
    also depends on when its garbage collector runs, which moves single TUs by
    up to about 200 MB without any change in live memory; check a surprising
    rise with `--param ggc-min-expand=10 --param ggc-min-heapsize=32768`
    before and after. Build each repository against a prefix that holds only
    the third-party dependencies and the freshly installed upstream
    repositories: the yggdrasil install bundles the prefix into
    `pyyggdrasil/`, whose include directory comes first, so a stale loki or
    tyr install there silently replaces the headers being measured.
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
| 4 | runir projection heuristic calls `ygg::database::compile` through a library function instead of a non-dependent call in `abstract_edges` | `heuristics/projection.hpp` 0.81 GB to 0.59 GB, `task_context.hpp` 1.38 GB to 1.04 GB on their own |
| 4 | runir Ext execution storage reads the entry module's register counts from its data instead of a non-dependent `get_registers<...>()` call | `ps/ext/execution_storage.hpp` 2.13 GB to 1.79 GB on its own; executors unchanged (they use the views anyway) |
| 4 | loki `detail::copy` declared with explicit types and defined in `src/semantic/translator.cpp` instead of two inline non-template functions | `semantic/translator/copy_translator.hpp` 1.27 GB to 1.00 GB on its own (6 sources and 2 tests) |
| 5 | a standalone scan of all 1192 public headers (clang, one TU each) | three headers did not compile on their own: tyr `datalog/ground/workspaces/rule.hpp` and `datalog/statistics/program.hpp` (missing includes), runir `dl/family_traits.hpp` (include cycle with `declarations.hpp`) |
| 5 | yggdrasil `SpinSharedMutex` and `SpinMutex` (`core/spin_mutex.hpp`) implement oneTBB's `spin_rw_mutex` and `spin_mutex` algorithms with `<atomic>` and compiler builtins instead of including the oneTBB headers, whose backoff pulls in `<immintrin.h>` (116 MB) and `<thread>` (74 MB); same state words, sizes and alignments | `formalism/symbol_repository.hpp` 396 MB to 293 MB on its own; summed memory yggdrasil 64.2 GB to 57.9 GB, loki 105.9 GB to 92.2 GB (no TU heavier) |
| 1, 8 | tyr headers that only take `ygg::ExecutionContext&` include `yggdrasil/execution/declarations.hpp` instead of `execution/onetbb.hpp` (`task_arena`, 215 MB on its own); `execute_model` is defined in `src/datalog/solver_impl.hpp` and instantiated next to `compute_model`; with the row above | summed memory tyr 236.1 GB to 214.1 GB (TUs above 1 GiB 136 to 94), runir 256.4 GB to 237.5 GB (median 1.23 GB to 1.10 GB); 24 tyr TUs that create an `ExecutionContext` grew 21-36 MB because `onetbb.hpp` now comes after the tyr headers (first include: back to the old cost); `ground/solver.cpp` +54 MB for the `execute_model` instantiations |
| 19, 5 | cista fork: `hashing.h` without the `std::chrono` specializations (moved to `hashing_chrono.h`, included by `serialization.h`), so `cista/containers/variant.h` no longer pulls in `<chrono>` (`<format>`, `<locale>`, `<sstream>`); yggdrasil `buffer/declarations.hpp` includes `cista/targets/buf.h` instead of `cista/serialization.h`, and the write APIs of `buffer::IndexedHashSet` and `BasicSymbolRepository` are defined in `detail/*_impl.hpp`, included by the instantiating sources (moving the whole write API instead of only the functions that serialize: no change in loki, tyr or runir) | `formalism/symbol_repository.hpp` 293 MB to 197 MB, `database/semantics/relation_repository.hpp` 341 MB to 268 MB on their own; summed memory yggdrasil 57.9 GB to 55.6 GB, loki 92.2 GB to 82.8 GB, tyr 214.1 GB to 206.4 GB, runir 237.5 GB to 228.8 GB; CPU time -3 to -14% |
| 13 | the TUs that grew with the row above (tyr search and heuristics +35 to +72 MB, runir `ps/ext/program_executor.cpp` +196 MB) | with frequent collections the same TUs are unchanged or lighter (gbfs_lazy 1206 MB to 1169 MB, program_executor 3276 MB to 3288 MB including its new explicit instantiations); the rise is GC timing |
| 5 | gtl patch (`src/gtl/settings-struct.patch`): `raw_hash_set` keeps growth_left, hasher, key_equal and allocator in a struct with `[[no_unique_address]]` members instead of a `std::tuple`, which every record type's hash set instantiated anew (std::tuple and its traits were 31% of the instantiation time of the 18 heaviest downstream TUs); same layout | summed memory loki 82.8 GB to 77.5 GB (TUs above 1 GiB 11 to 5), tyr 206.4 GB to 188.4 GB (median 885 MB to 784 MB, above 1 GiB 90 to 70), runir 228.8 GB to 203.1 GB (max 3.89 GB to 3.40 GB, above 1 GiB 109 to 83); CPU -6 to -8%; no TU heavier |
| 18 | records list their identifying members as member pointers, `static constexpr auto identifying_members() noexcept { return ygg::Members<&Data::a, &Data::b> {}; }`, instead of returning `std::tie(a, b)`; `Hash`, `EqualTo` and `Less` fold over the pointers, so no tuple, `std::tie` or `std::apply` is instantiated per record type (a static function, not a type alias: a class template is still incomplete while its own aliases are instantiated) | tyr's 107 records converted: summed memory 188.4 GB to 176.7 GB, median 784 MB to 701 MB, CPU -5%; one TU +36 MB |
| 19 | yggdrasil `IndexedHashSet` (both) holds its element vector by value and its value-to-index table (`Lookup`: the gtl set and its functors) behind a `std::unique_ptr` to a class defined in `detail/indexed_hash_set_impl.hpp`; `BasicSymbolRepository` construction, `clear`, `memory_usage` and `find_local*` moved to its impl; with inline segment pointers in `SegmentedVector` and a segmented `ObjectPoolStorage` | summed memory loki 74.9 GB to 70.1 GB, tyr 180.3 GB to 156.5 GB (above 1 GiB 67 to 50, max 1.95 GB to 1.70 GB, median 565 MB to 483 MB), runir 198.1 GB to 169.2 GB (above 1 GiB 76 to 59, max 3.19 GB to 2.79 GB), yggdrasil 58.8 GB to 57.7 GB; CPU -4 to -16%; changes the container layout, so prebuilt downstream packages must be rebuilt |
| 19 | yggdrasil `IndexedHashSet` (both) holds its `Lookup` by an owning raw pointer instead of `std::unique_ptr` (clang time traces of the heaviest TUs: `unique_ptr`, `__uniq_ptr_*` and their `std::tuple` were about 12% of runir's front end, nearly all from `IndexedHashSet`) | summed memory loki 69.0 GB to 65.8 GB (above 1 GiB 3 to 1), tyr 152.5 GB to 141.1 GB (median 579 MB to 525 MB, above 1 GiB 46 to 30, max 1.66 GB to 1.54 GB), runir 164.5 GB to 143.8 GB (median 672 MB to 545 MB, above 1 GiB 60 to 48), yggdrasil 53.2 GB to 52.8 GB; peak at -j32 tyr 41.6 GB to 37.5 GB; CPU -4 to -9%; no TU more than 20 MB heavier |
| 7 | runir `ConstructorRepositoryFactory::create` defined in `detail/constructor_repository_impl.hpp` (the class is extern but `create` was in the class body) | a TU that creates a repository 2.27 GB to 0.78 GB; with the projection change: 49 runir TUs -50 MB to -0.6 GB, summed -13.3 GB, none heavier than +6 MB |
| 7 | runir `DenotationRepository` constructor, destructor and factory, and the ps `BasicRepositoryFactory::create`, defined in the library | callers no longer construct or destroy the symbol pools; needed for the macOS link of the incremental query test |
| 4 | runir CNF translation asserted `==` on two `shared_ptr`s to the planning repository | completed the repository type in Debug builds only (GCC 13 CI error); compares `.get()` now |
| 9 | runir on macOS: ld64 LTO dropped the denotation and query pool members that only tests call through the opt-in extern lists | runir skips LTO on Apple; Linux keeps it |
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
| 18 | yggdrasil database engine without the `ColumnTypes Values` parameter (360 templates); column layouts store `ColumnCodec<T>::id`, the list remains only in `visit_column_type` and `validate_row`; definitions still in headers | query test 2.41 GB to 1.55 GB and 34 s to 18 s (it instantiated the engine for three lists), other database TUs -8% to +44%: median 622 MB to 879 MB, headers on their own 0.45-0.65 GB to 0.57-0.95 GB, since the now non-template inline code compiles in every includer (rule 4); the definitions belong in the library |
| 18, 8, 7, 9 | the same engine compiled once in `yggdrasil::database` (133 definitions in `src/database`), `QueryRepository` and `RelationRepository` constructors and destructors out of class, their symbol storage instantiated in the library with extern lists in the main headers, `Builder<Relation>` moves and destructor out of class | 36 database TUs: max 2.41 GB to 1.23 GB, above 1 GiB 8 to 3, summed 29.8 GB to 25.1 GB, wall 371 s to 246 s; the library adds 26 sources (12.7 GB, max 0.88 GB), and test builds a second, instrumented copy for the two work-counting tests (another 12.7 GB) |
| 7 | 28 non-template member functions of 8 lines or more (search, generic join, plans, cost model) defined out of class in the library; codecs and row accessors stay inline for the hot paths | database TUs summed 25.1 GB to 24.6 GB, max 1.23 GB to 1.15 GB, but 7 more library sources at about 0.5 GB each: library 12.7 GB to 16.3 GB |
| 11 | the 33 `yggdrasil::database` sources compiled as 5 unity batches of 8 | library 16.3 GB to 3.8 GB, 140 s to 39 s, heaviest 1.0 GB; with the previous rows the whole yggdrasil build goes from 61.8 GB summed, max 2.41 GB, 9 TUs above 1 GiB, 67 s at -j16 to 59.7 GB, 1.15 GB, 2, 45 s |
| 8 | the shared `yggdrasil::database` library compiled with `-fno-semantic-interposition` | incremental distance with dense ties 5.5 µs (header-only) to 12.1 µs (library) back to 4.7 µs: without it, `successor_distance`, `contains_edge` and the work counters were PLT calls inside the library |
| 20 | yggdrasil `BuilderStorage` keeps one pool per type in flat bases (reversed) instead of `std::tuple<UniqueObjectPool<Data<Ts>>...>`; the first version listed the bases in type order and crashed a loki test with `free(): invalid pointer` at destruction | summed memory tyr 141.1 GB to 138.7 GB (above 1 GiB 30 to 28), runir 143.8 GB to 142.3 GB, loki and yggdrasil -0.3%; peak at -j32 tyr 37.5 GB to 35.6 GB, runir 67.3 GB to 64.0 GB; CPU +2 to +3% in tyr and runir in two runs |
| 20 | records return their fields from a static `cista_members()` as `ygg::Members<&Data::index, ...>` instead of `std::tie` (254 classes in the four repositories; four tyr rule class templates keep `cista_members()`); the cista fork folds over the pointers in `for_each_field` and `for_each_ptr_field` and builds a tuple only in `to_tuple`; same type hash | summed memory yggdrasil 52.6 GB to 50.6 GB, loki 65.5 GB to 60.6 GB (median 346 MB to 308 MB), tyr 138.7 GB to 128.8 GB (median 509 MB to 480 MB, above 1 GiB 28 to 21, max 1.51 GB to 1.42 GB), runir 142.3 GB to 129.1 GB (median 541 MB to 507 MB, above 1 GiB 47 to 41); CPU -4 to -12%; peak at -j32 loki 16.6 GB to 14.4 GB, tyr 35.6 GB to 32.3 GB; one TU +33 MB |
| 19 | tyr planning and datalog `repository.hpp` without `prepare_for_insert` and `insert`: both move to a new `interning.hpp` with the canonicalization they need (71 MB of `repository.hpp`'s 351 MB on its own), included by the 36 tyr sources, headers and tests and the one runir test that intern; `ygg::formalism::insert` requires the hook, so a missing include fails to compile | summed memory tyr 128.8 GB to 123.5 GB (median 479 MB to 438 MB, above 1 GiB 21 to 18), runir 129.1 GB to 127.1 GB (median 507 MB to 490 MB); one TU +27 MB |
| 14 | cista `type_at_index` in constant depth (`__type_pack_element`, with a flat fallback) instead of a linear recursion, used once per case by `variant::apply` | no change in any repository (-0.1% to +0.1%); clang traces had already put it at about 1% of runir's front end; reverted |
| 11 | runir DL grammar `parser_def.hpp` defined the Spirit rules of all three families and both dialects in every instantiation source, each of which instantiates one family; a probe without any instantiation cost 1.82 GB. Each source now defines its family (`RUNIR_DL_GRAMMAR_PARSER_BASE`, `_EXT`, `_UNS`) before the include. Splitting by dialect instead did not help: the two halves cost 2.62 GB and 2.51 GB against 2.69 GB merged | base 2.40 GB to 1.68 GB, ext 2.57 GB to 1.83 GB, uns 2.69 GB to 1.94 GB, 34-38 s to 15-18 s each; runir summed 127.1 GB to 124.9 GB, max 2.68 GB to 2.21 GB, peak at -j32 60.9 GB to 56.2 GB |
| 11 | runir `evaluation_ext.cpp`, `evaluation_uns.cpp` and `ps/ext/program_executor.cpp` split into ground and lifted sources (probes: the direct evaluator and the incremental engine share most of their instantiations, a ground or lifted half does not) | halves 1.62-1.80 GB against 2.00-2.21 GB, runir max 2.21 GB to 1.94 GB, but summed 124.9 GB to 128.7 GB, CPU +3%, peak at -j32 56.2/62.9/62.9 GB to 69.3/66.5/62.7 GB in three runs each; reverted |
| 11 | tyr `lmcut.cpp` and `rpg_{ff,add,max}.cpp` split into ground and lifted sources, each including only its kind's RPG header (probes) | lmcut 1.35 GB to 1.06 + 1.25 GB, rpg_ff 1.25 GB to 0.99 + 1.17 GB: the lifted half keeps 90-95%, so the max drops about 0.1 GB for about 1 GB more summed memory; not applied |
| 1, 7 | yggdrasil `GenericJoinPlan` and `DistancePlan` in their own headers (`generic_join_plan.hpp`, `distance_plan.hpp`) without the evaluators, `GenericJoinPlan`'s constructor and `matches` in the library, `must_pass_plan.hpp` without the unused `relation_view.hpp`; runir's query and numerical data headers include only the plans | `generic_join.hpp` alone 238 MB, `plans.hpp` 155 MB; runir summed 124.9 GB to 123.8 GB, CPU -2%; other repositories unchanged |
| 18 | runir: sketches and classifiers moved to the Ext DL constructor set (made the union of all three), a capability check per language in the shared AST translation, then the Base and Uns DL instantiations (evaluation, symbol storage, Spirit rules, AST parsers) and the Ext and Uns grammar and CNF storage deleted | runir summed 123.8 GB to 107.6 GB, CPU 22.1 to 18.6 min, wall 94 s to 69 s, peak at -j32 58.4 GB to 35.9 GB, largest link 2.31 GB to 1.18 GB |
| 18, 4, 8 | runir DL `Family` template parameter removed (scripted, 296 files) | first version 107.6 GB to 109.6 GB: generation grammar and CNF types took the union constructor set (their symbol storage sources +0.32 GB each), and the query translator, query construction and others became non-template code in headers; generation grammars back on the Base set 108.2 GB; translator and query construction defined in the library 108.1 GB, max 2.13 GB, peak at -j32 33.3 GB, wall 69 s; with the renames 108.1 GB, peak 30.0 GB |
