# Template and include rules

Compile cost in yggdrasil, loki, tyr and runir is dominated by the C++ front
end: template instantiation and the headers that trigger it, not
optimization or LTO. These rules keep translation units (TUs) small. They
apply to all four repositories and are maintained only here; the other
repositories link to this file.

Target: every TU stays below 3 GB of compiler memory (max RSS of `cc1plus`)
in a Release + LTO build with GCC.

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
   In inline code, dereference with `*ptr.get()`, not `*ptr`: the overloaded
   `operator*` makes argument-dependent lookup complete every template
   argument of the `shared_ptr`, which instantiates the pointee.
5. **Include what you use.** A header compiles on its own. Library code,
   tests and executables include the components they use, never umbrella
   headers such as `tyr/planning/planning.hpp`.
6. **Pimpl only at high-level entities** (heuristics, state repositories,
   executors), never in views, repositories, interning or evaluation.

## Instantiation

7. **Explicitly instantiate when the same instantiation is repeated in many
   TUs and the declaration is cheap.** Define the members out of class:
   members defined in the class body are implicitly inline and GCC still
   instantiates them in every TU that uses them, despite `extern template`.
8. **Prefer hiding definitions over `extern template`.** Put the definitions
   in `detail/<name>_impl.hpp`, include it only from the instantiating
   sources (and under `<PROJECT>_HEADER_INSTANTIATION`), and instantiate in
   the library. Users then only link. An `extern template` declaration of a
   class instantiates the class definition in every includer, so long extern
   lists or externs over heavy classes cost more than they save.
9. **No deduced return types on functions that are declared `extern` or used
   only through declarations**: deducing the type instantiates the body.
10. **Split instantiating sources along the dimension that drives their
    cost** (family, task kind, context or constructor category) so each stays
    below the target. Splitting along a dimension the cost does not depend
    on does not help.

## Process

11. **Measure before and after.** Use the standalone cost of a header
    (`-fsyntax-only` of a TU that only includes it), the cost of a single API
    call (the TU's includes plus that call, at `-fsyntax-only` and at `-O3`),
    and a full instrumented build logging the max RSS per TU.
12. **Keep only what wins in the full build.** A smaller standalone header
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
| 8 | runir DL evaluator compiled in eight library sources | evaluating TUs -0.2 to -1.0 GB |
| 10 | the runir optimization test split by task kind | no change (7.1 GB per half); the cost depends on the family |
