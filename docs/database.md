# Relational algebra

`<yggdrasil/database/operations.hpp>` provides a small, header-only library in
`ygg::database`. Relations contain sets of fixed-arity tuples, including arity
zero. Intermediate relations have no special arity limit.

Public headers expose the API and include their implementations from `details/`
automatically. Continue including the public headers; no separate library or
additional include is required.

`Relation<T>` owns rows stored in Yggdrasil's `RawArraySet<T>`. The default value
type is `ygg::uint_t`, suitable for interned object IDs. Values must be trivially
copyable, support copying into scratch vectors, and provide consistent
`ygg::Hash<T>` / `ygg::EqualTo<T>` semantics. Column labels are query-local
`Column` IDs (`ygg::uint_t`); callers can intern variable names once rather than
hashing strings while evaluating tuples. All columns have the same value type.

`Columns` owns an ordered schema and enforces unique labels. `ColumnsView` borrows
an already validated schema without allocating or checking duplicates again.
Relations and plans own `Columns`; their schema accessors return `ColumnsView`.
Use `columns.view()` to borrow an owning schema, or construct a `ColumnsView`
from an explicit span to validate and borrow external labels. Borrowed labels
must remain alive and unchanged. Temporary owning schemas cannot be borrowed.
`Columns::assign(validated_columns)` retains capacity where possible and
invalidates existing views. Lookup through `column_index(label)` throws if the
label is missing. These types are also available through `columns.hpp`.

## Example

```cpp
#include <yggdrasil/database/operations.hpp>

using namespace ygg::database;
constexpr Column package = 0, truck = 1, destination = 2;

Relation<> options({package, truck, destination});
options.insert({10, 20, 30});
options.insert({10, 21, 31});
options.insert({11, 20, 31});

Relation<> goals({package, destination});
goals.insert({10, 30});
goals.insert({11, 31});

// Match both package and destination before dropping the destination.
auto matched = join(options.view(), goals.view());
auto eligible = project(matched.view(), {package, truck});
// eligible contains (10,20) and (11,20).

auto for_truck_20 = select_equal_value(eligible.view(), truck, 20);
auto packages = project(for_truck_20.view(), {package});
auto guard = project(packages.view(), {});
bool any_package = !guard.empty();

// Reuse both output and scratch storage on subsequent evaluations.
Relation<> output({package, truck});
Workspace<> workspace;
project(matched.view(), {package, truck}, output, workspace);
```

The library operates on concrete relations; it does not parse a query language
or attach meaning to predicate, variable, or object IDs.

## Operations

All operations take `RelationView<T>` inputs, obtained with `relation.view()`.

| Function | Semantics |
| --- | --- |
| `rename(input, labels)` | Replace labels positionally; shares tuple storage. |
| `select(input, predicate)` | Keep rows for which the predicate returns true. |
| `select_equal_columns(input, a, b)` | Keep rows with equal values in the two labelled columns. |
| `select_equal_value(input, column, value)` | Keep rows matching a constant. |
| `join(lhs, rhs)` | Natural join on all common labels; output has lhs columns followed by rhs-only columns. |
| `project(input, labels)` | Keep columns in the supplied order and eliminate duplicates. |
| `union_(lhs, rhs)` | Set union. The suffix avoids the C++ keyword. |
| `difference(lhs, rhs)` | Set difference. |

The selection predicate receives `std::span<const T>` in input column order.
Use `view.column_index(label)` once outside the predicate to resolve a label.
Unary relation membership can also be expressed as a join with that relation.

Union and difference require identical ordered schemas. Use projection to align
different orders. A rename changes labels, not tuple positions, and cannot merge
two columns into one. For a repeated variable, select equality before projecting
away the redundant column. Duplicate labels and wrong tuple lengths are errors.
Unknown labels throw `std::out_of_range`; incompatible schemas throw
`std::invalid_argument`.

Every materializing operation also accepts a final `Relation<T>& out` argument.
The output must have the exact expected schema and must not share storage with
any input, including a renamed view. These checks happen before clearing its
previous rows. Valid calls replace the output's contents; exceptions during
evaluation, including a throwing selection predicate, can leave partial output.

## Repeated evaluation

Keep a `Workspace<T>` and output relations across states. The four-argument
`join(lhs, rhs, out, workspace)` and `project(input, labels, out, workspace)`
overloads reuse their scratch vectors and join hash table. Column mappings and
index contents are rebuilt into retained buffers; there is no result cache to
invalidate. The shorter overloads create temporary scratch storage for convenience.
Selection, union, and difference need only their existing output overloads.

When schemas stay the same, prepare column positions once with `JoinPlan` and
`ProjectionPlan` (available through `operations.hpp`, or separately in
`plans.hpp`):

```cpp
// Once per expression. Plans own their schemas and resolved column positions.
const JoinPlan joining(options.columns(), goals.columns());
const ProjectionPlan projecting(joining.output_columns(), {package, truck});
Relation<> matched({package, truck, destination});
Relation<> eligible({package, truck});
Workspace<> workspace;
auto options_view = options.view();
auto goals_view = goals.view();
auto matched_view = matched.view();

// For each state, after refreshing options:
join(options_view, goals_view, joining, matched, workspace);
project(matched_view, projecting, eligible, workspace);
```

Prepared execution uses stored positions without resolving labels again. It
checks that the ordered input/output schemas match the plan before clearing
the output; renamed or reordered inputs need a matching plan. Nullary schemas
are supported. Plans hold no rows or state-specific indexes and can be reused
with different relation objects, copied, or shared across evaluators. Each
evaluator still needs its own workspace and outputs. The hash index is rebuilt
from current rows, and the smaller join input is chosen each time.

`relation.view()` borrows rows and validated column labels without allocating
or repeating duplicate-label checks. Clearing and refilling rows is supported;
owners must stay alive and stationary, and borrowed labels must stay unchanged.
For renaming in a state loop, construct labels once and borrow their view:

```cpp
const Columns renamed_columns {package, truck};
const auto labels = renamed_columns.view();
// Inside each evaluation:
auto renamed = rename(eligible.view(), labels);
```

`RelationView` and `rename` always borrow both rows and labels. Pass a
`ColumnsView`, a live `Columns` owner, or an explicit span; the labels must
outlive the returned view. Temporary owners and implicit container borrowing
are rejected. Raw spans are validated at the API boundary. Projection, pool
checkout, and relation reinitialization accept validated views without repeating
uniqueness checks. Arity and operation-specific schema compatibility checks
still apply.

For recursive evaluation, `<yggdrasil/database/relation_pool.hpp>` provides
`RelationPool<T>`, using Yggdrasil's `UniqueObjectPool` separately for each arity:

```cpp
RelationPool<> temporaries;
Workspace<> workspace;
const JoinPlan joining(options.columns(), goals.columns());
const ProjectionPlan projecting(joining.output_columns(), {package, truck});

// Inside each evaluation; fill options for the current state first.
auto matched = temporaries.get_or_allocate(joining.output_columns());
join(options.view(), goals.view(), joining, *matched, workspace);
auto eligible = temporaries.get_or_allocate(projecting.output_columns());
project(matched->view(), projecting, *eligible, workspace);
// Consume eligible before releasing its handle. Handle destruction returns the
// whole relation, including its allocated buffers, to the pool.
```

Checkout clears old rows and sets the labels. Grouping by arity avoids replacing
the fixed-arity tuple storage when different intermediate schemas are needed.
`Relation::initialize(labels)` supports the same reuse with a caller-managed
`UniqueObjectPool<Relation<T>>`; keep each such pool at one arity.
Move pool handles to transfer ownership while retaining the pooled relation and
its buffers. Pooled relations must retain their storage; moving from `*handle`
is unsupported. A moved-from standalone relation may only be destroyed or
assigned a new relation before further use; `initialize()` requires intact storage.

After warming up the necessary capacities and simultaneously live pool entries,
these paths reuse memory across evaluations. Larger inputs or intermediate
results can still grow buffers. Retained memory follows the largest requirements
seen by each buffer until its owner is destroyed. Use a separate workspace and
relation pool for each evaluator/thread. A workspace can serve sequential
operations; it must not be used by overlapping calls.

## Nullary relations and object domains

`Relation<> boolean;` is initially false (no tuples). `boolean.insert({});`
makes it true (the unique empty tuple). Projecting a nonempty relation to zero
columns produces true. Joining with true preserves a relation; joining with
false empties it. These rules also hold with an empty object domain.

Represent the object domain by an ordinary unary relation populated with all
task objects, including objects absent from other predicates. Rename and join
copies to construct domain products when needed for complementation. State,
goal, and register relations are ordinary inputs populated by the caller.

## Storage and cost

Rows use pooled fixed-length storage and hash-based deduplication. Renaming
shares tuple storage. Projection and joins reuse one scratch tuple per
operation. A join indexes the smaller input in `ygg::UnorderedMultiMap<hash_t, size_t>`
from `<yggdrasil/containers/unordered_multi_map.hpp>`. It uses a flat hash map
from each key hash to its first entry, plus a contiguous vector of row indices
and next-entry links. Both buffers retain capacity across calls; there are no
per-row node allocations. Hash collisions are checked against the actual values;
there is no allocated key vector for each row. For fixed arities and well-distributed
hashes, keyed joins take expected time linear in inputs plus matching outputs;
disjoint schemas necessarily produce a Cartesian product.

`clear()` and output overloads retain tuple and hash-table capacity. The transient
join index is rebuilt per call. Row iteration order is not part of relational
semantics; use membership to compare results.

Relations are movable but not copyable. Views borrow row storage and either own
or borrow column metadata. Keep borrowed storage alive and stationary while
using a view; borrowed labels must remain unchanged. Insertion preserves existing
row spans, while `clear()` invalidates them. Views observe subsequent insertions
and clears. Reinitializing a relation invalidates its views. Returning a pooled
relation ends the permitted lifetime of all its views and row spans; cached
results must own separate storage. There is no internal synchronization.

The CMake test targets are `database_operations` and `database_allocations`; run
them with `ctest --test-dir <build> -R '^database_' --output-on-failure`.
The allocation regression counts allocation and deallocation calls during
changing-state evaluations after warmup.
