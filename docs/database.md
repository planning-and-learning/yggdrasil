# Relational algebra

`<yggdrasil/database/operations.hpp>` provides header-only set operations in
`ygg::database`. Relations have a runtime schema and packed, fixed-width rows.
Different columns can hold different types without a tag or variant in every cell.
Nullary relations and runtime arities remain supported.

## Types and schemas

The template parameter is a `ygg::TypeList` of supported value types, not one
cell type. `DefaultColumnTypes` contains `uint32_t`, `int32_t`, `uint64_t`,
`int64_t`, `float`, `double`, and `bool`. A smaller or extended list can be used
consistently for relations, schemas, plans, workspaces, and repositories.

```cpp
#include <yggdrasil/database/operations.hpp>

using namespace ygg::database;
using Values = ygg::TypeList<std::uint32_t, double>;
const ygg::Index<Column> source(0), target(1), distance(2);

ygg::Builder<Columns<Values>> columns;
columns.push_back<std::uint32_t>(source);
columns.push_back<std::uint32_t>(target);
columns.push_back<double>(distance);
ygg::Builder<Relation<Values>> paths(std::move(columns));
paths.insert(std::tuple { std::uint32_t(3), std::uint32_t(8), 2.5 });
paths.insert(std::tuple { std::uint32_t(3), std::uint32_t(9), 4.0 });

const auto length = paths[0].get<double>(distance);
auto reachable = project<Values>(paths, {source, target});
auto exact = select_equal_value<Values>(paths, distance, 2.5);
```

`Column` identifies a schema-local label. `Columns<Values>` follows the usual
`Builder`, `Data`, `Index`, `View` structure. Each `ColumnLayout` records a label,
type ordinal, byte offset, and width. Identity depends on the ordered labels
and types; offsets and widths are derived and validated. `.span()` borrows the
layout, `.row_size()` returns its packed byte width, and `column_index(label)`
resolves a label. Labels must be unique. Constructing a schema from labels alone
uses the first type in `Values` for every column.

`ColumnCodec<T>` is the extension point for additional fixed-width values.
`ColumnValue` requires a constant positive `size`, `encode(value, byte_span)`
and `decode(const_byte_span)`. Equal values must have identical encodings;
decoding must preserve their semantic value. Explicit codecs prevent padding
or uninitialized object bytes from becoming part of a key. Scalar `Index<T>`
types derived from `IndexMixin` use the existing `IndexCoder`. Composite indices
need an explicit codec.

`ColumnValueFor<T, Values>` additionally requires `T` to be registered in the
column family. Typed access and tuple operations use this shared constraint.

Integer and floating-point codecs copy through `memcpy`; packed fields need no
alignment and never expose typed references into bytes. Floating-point encoding
normalizes signed zero and all NaNs, preserving every other finite value and
infinity exactly. Boolean encoding is one byte, 0 or 1. No tolerance rounding is
performed. This is a native-endian in-process representation, not a portable wire
format. Previously serialized homogeneous relation records are incompatible.

## Rows and ownership

`ygg::Builder<Relation<Values>>` owns a schema and `RawArraySet<std::byte>`.
`row(i)` exposes canonical bytes for prepared operations. `operator[]` and
`at(i)` return a borrowed `Row<Values>`: `get<T>(label)` checks the requested type
and returns a value; `visit(position, visitor)` dispatches a runtime column type.
The visitor must return one common result type. `.bytes()` and `.columns()`
expose the borrowed spans.

A `Row` borrows an already validated schema and canonical bytes. Its construction
has those preconditions, like constructing a span from valid storage. Inserting
such a row checks the schema without decoding every field. Inserting a raw byte
span validates width and canonical field encodings before mutation. Inserting
`std::tuple` values checks arity/types and encodes into a fixed-size stack buffer.
`find` and `contains` offer the same tuple, typed-row, and raw-byte interfaces.

Contextual `ygg::View` over relation `Builder`, `Data`, or `Index` handles uses
`View<Row<Values>, Context>` for indexing and iteration. Primitive columns remain
values; typed indices resolve through `make_view` and the context's repository.
This keeps object-view construction at the external access boundary. Row and
relation iterators borrow storage; they do not copy payloads.

Builders are movable and noncopyable. Insertion retains existing row storage;
erase moves the last row into the erased slot. Clear invalidates rows but retains
capacity. Schema replacement invalidates borrowed schema spans. Keep a borrowed
row's owner and schema alive and unchanged. A returned pool entry cannot be used
through an old view. There is no internal synchronization.

## Operations

All operations accept builders or contextual views satisfying
`RelationViewConcept<V, Values>`. Output/workspace overloads deduce `Values`;
return-by-value overloads take it explicitly.

| Operation | Meaning |
| --- | --- |
| `select<Values>(input, predicate)` | Keep rows accepted by a predicate over `Row<Values>`. |
| `select_equal_columns<Values>(input, a, b)` | Compare columns with the same registered type. |
| `select_equal_value<Values>(input, column, value)` | Match a constant encoded by that column's codec. |
| `project<Values>(input, labels)` | Keep the ordered columns and eliminate duplicate rows. |
| `join<Values>(lhs, rhs)` | Natural join on common labels, followed by rhs-only columns. |
| `union_<Values>(lhs, rhs)` | Union with an identical ordered typed schema. |
| `difference<Values>(lhs, rhs)` | Difference with an identical ordered typed schema. |
| `builder.rename(labels)` | Change labels while retaining types and rows. |

Common join labels and equality columns must have matching types. Projection
preserves field types and repacks offsets. Wrong types, incompatible schemas,
duplicate labels, and wrong row widths throw before clearing the output.
Unknown labels throw `std::out_of_range`. Exceptions during evaluation (for
example, a throwing predicate) may leave partial output. An output cannot alias
an input. `assign(destination, source)` supports self-assignment, replaces the
schema and rows, and retains buffers when byte widths match.

Prepare `JoinPlan<Values>` and `ProjectionPlan<Values>` once per expression.
Their byte slices resolve types, offsets, and widths outside the row loop:

```cpp
const ProjectionPlan<Values> plan(paths.columns().span(), {source, target});
ygg::Builder<Relation<Values>> output(plan.output_columns().span());
Workspace<Values> workspace;
project(paths, plan, output, workspace);
```

Full and incremental operations share byte-key hashing, equality, and field
copying. Row hashing uses the existing byte hashing path. Join keys stream their
selected slices into XXHash; collisions compare actual key bytes. A join indexes
the smaller input with `UnorderedMultiMap`. `JoinIndexCache<Values>` may retain
indexes for immutable inputs. Its key includes ordered field types and offsets,
so equal bytes with different interpretations cannot reuse an incompatible index.
Renaming labels can still reuse a compatible index.

## Reuse and interning

Keep a `Workspace<Values>` and output builders across evaluations. Scratch byte
buffers, row containers, and hash tables retain capacity. `RelationPool<Values>`
uses existing `UniqueObjectPool` instances grouped by **row byte width**, not
logical arity. Prepare a schema once and pass its layout span to checkout to
avoid rebuilding it in the update loop. Larger workloads may still grow storage.
Each evaluator/thread needs its own mutable workspace and pools.

`RelationRepository<Values>` interns schemas and compact relation records in the
existing symbol repository. Individual byte rows and sorted row-ID vectors are
interned in `RawVectorSet`s. Relations with identical typed schemas and row sets
share identity regardless of insertion order. Identical bytes may share physical
rows across schemas, but distinct typed schemas retain distinct relation identity.
Iteration follows sorted interned row IDs, not value order.

```cpp
RelationRepositoryFactory<Values> factory;
auto repository = factory.create();
auto [stored, created] = insert(repository, paths);
paths.clear(); // stored retains its canonical rows
```

`insert(repository, builder)` publishes and synchronizes the builder's index.
`copy(source, repository)` remaps canonical IDs to another repository.
`repository.rename(view, labels)` preserves the source context, field types,
row-ID set, and namespace. A caller may supply a different schema namespace.
Repository clear invalidates all its views while retaining reusable storage.
Reset caches borrowing those rows before clearing. When temporary and canonical
inputs share a join cache, derive their factories from one `RelationPoolFactory`
so storage IDs share one sequence.

A zero-column empty relation represents false. `insert(std::tuple {})` makes it
true. Projection of a nonempty input onto no columns yields true. An empty
object domain needs no special-case representation.

## Incremental evaluation

`database/incremental/projection.hpp` and `database/incremental/join.hpp` retain
current results and maintain added/removed row sets. Deltas use the same typed
schemas and packed rows. Projection tracks witnesses; a change that replaces a
witness without changing membership produces no output delta. Join supports
updates to either or both operands. Prepared byte operations are shared with
full recomputation. Existing reset, apply, undo, and storage ownership rules
remain unchanged.

## Python

```python
from pyyggdrasil import database as db

relation = db.Relation([0, 1, 2],
                       [db.ColumnType.UINT32, db.ColumnType.UINT32,
                        db.ColumnType.FLOAT64])
relation.insert([3, 8, 2.5])
assert tuple(relation[0]) == (3, 8, 2.5)
assert relation.columns().type(2) == db.ColumnType.FLOAT64
```

Labels-only construction defaults to `UINT32`. `ColumnType` also exposes `INT32`,
`UINT64`, `INT64`, `FLOAT32`, and `BOOL`. Rows yield Python values of the column's
type; no packed bytes leak through the Python API. Pool checkout accepts the
same label/type arguments. `.columns()` remains a borrowed label sequence with
`type(position)` for schema inspection. Rows, views, and schemas keep their
Python owners alive; explicitly clearing an owner still invalidates them.

Run the native regression suite with
`ctest --test-dir <build> -R '^database_' --output-on-failure`.
It covers full and incremental operations, interned identities, canonical
encodings, schema validation, and retained allocations. Python lifetime and
mixed-type behavior are covered in `python/tests/test_database.py`.

## Tuple distances

`<yggdrasil/database/distance.hpp>` computes directed, unweighted shortest-path
distances between tuple-valued vertices. Sources and targets have `k` columns;
an edge has `2k`, with its source tuple first and target tuple second. All four
vertex positions must have matching field types in the same order. Their column
labels may differ. `Values` must include `uint_t` for the distance field.

```cpp
using namespace ygg;
using namespace ygg::database;
Builder<Relation<>> sources({Index<Column>(10)});
Builder<Relation<>> edges({Index<Column>(1), Index<Column>(2)});
Builder<Relation<>> targets({Index<Column>(20)});
sources.insert(std::tuple{uint_t(4)});
edges.insert(std::tuple{uint_t(4), uint_t(7)});
edges.insert(std::tuple{uint_t(7), uint_t(9)});
targets.insert(std::tuple{uint_t(9)});

DistancePlan<> plan(sources.columns().span(), edges.columns().span(),
                    targets.columns().span(), Index<Column>(3));
DistanceWorkspace<> workspace(plan);
Builder<Relation<>> result(plan.output_columns().span());
distance(sources, edges, targets, plan, result, workspace);
assert(result.contains(std::tuple{uint_t(4), uint_t(9), uint_t(2)}));
```

The result has `2k + 1` columns: the original source tuple, the original target
tuple, and their shortest distance as `uint_t`. It uses the edge relation's
ordered column labels followed by the explicitly supplied distance label, which
must be fresh. Each reachable source/target pair occurs once. Equal endpoint
tuples have distance zero even without an edge; unreachable pairs are absent.
Only requested sources and targets appear in the result, but paths may traverse
other vertices. Empty source or target sets produce an empty result. Zero-column
tuples are supported and represent the single empty vertex.

The plan owns its schemas and checks arities/types once. Calls require each
input's original ordered schema. The return-by-value form is
`distance<Values>(sources, edges, targets, plan)`. The output/workspace form
retains capacity between calls; the workspace is reusable for the same vertex
byte width. Schema, workspace-width, and output-alias errors are rejected before
clearing output. A later evaluation failure can leave partial output.

Tuple IDs are private. An existing `RawArraySet<std::byte>` maps canonical tuple
bytes to dense IDs and IDs back to tuples; adjacency stores only pairs of IDs.
The full evaluator rebuilds this graph and runs one breadth-first search per
source. It needs graph storage proportional to unique tuple bytes plus edges,
and one vertex-sized distance/queue buffer. It does not materialize all-pairs
distances. The expanded output can nevertheless contain every source/target
pair, so output size can dominate memory. The maximum `uint_t` value is reserved
internally for infinity; attempting to exceed the supported vertex count throws.

### Incremental distance

Include `<yggdrasil/database/incremental/distance.hpp>` and retain one evaluator:

```cpp
incremental::DistanceEvaluator<> evaluator(plan);
evaluator.initialize(sources, edges, targets);
incremental::Delta<> source_change(sources.columns().span());
incremental::Delta<> edge_change(edges.columns().span());
incremental::Delta<> target_change(targets.columns().span());
edge_change.removed.insert(std::tuple{uint_t(7), uint_t(9)});
evaluator.update(source_change.added, source_change.removed,
                 edge_change.added, edge_change.removed,
                 target_change.added, target_change.removed);
assert(evaluator.get_result().empty());
assert(evaluator.get_delta().removed.contains(
    std::tuple{uint_t(4), uint_t(9), uint_t(2)}));
```

The evaluator owns the compact graph, source/target membership, maintained
distances, output, and last `Delta<Values>`. Inputs are borrowed only during a
call. Pass empty changes for unchanged inputs. Deltas must describe actual set
changes, with disjoint additions/removals and each input's original schema.
Initialization replaces the baseline and clears the output delta. Swapping all
three pairs of added/removed inputs undoes a batch.

Edge updates examine every surviving source and repair its shortest-path state
where needed. They report the exact net result change after the whole batch.
A changed distance removes the old output tuple and adds the new one; replacing
a shortest-path witness without changing the distance produces no output delta.
The algorithm handles simultaneous
source, edge, and target changes, including deletions and cycles. This is exact
maintenance, not a guarantee that every update is faster than full evaluation.

#### Algorithm and reference

Edge repair specializes Ramalingam and Reps,
[*An Incremental Algorithm for a Generalization of the Shortest-Path Problem*](https://research.cs.wisc.edu/wpis/papers/jalg96.pdf),
Section 4, Figure 4, to unit edges. Mixed changes enter together: local heaps
track improving incoming edges and a global heap selects inconsistent vertices.
Equal-length replacements can cancel before descendant distances change.

For one surviving source, let `C = |δ|` count modified edges and vertices whose
final distances change. Let `D = ||δ||` also include their incident dependency
edges. Repair takes expected/amortized `O(D log(2 + D))` for mixed batches, or
`O(D + C log(2 + C))` for insertion-only batches with Fibonacci heaps.

Hash lookups contribute the expected qualification; heap operations contribute
the amortized qualification. These are per-source repair bounds. The complete
relation API also pays for tuple encoding, validation, adjacency changes,
growing retained arrays, source initialization, and output materialization.
Every surviving source
processes the edge batch. An empty output delta alone does not imply small work.

The implementation retains `distances` and `supports` for each active source.
`supports[v]` stores the cardinality of the paper's `SP[v]`, rather than a set
or membership bit for every edge and source. Membership is determined from an
edge's old/new contribution relative to the current distance; changes update the
count directly. At a finite consistent distance this counts shortest predecessors,
not complete paths. The source has an independent zero-length support. During
repair, `SP` also includes contributions below the tentative distance; its
infinity case follows the same comparison rule.

`ygg::FibonacciHeap` configures Boost's mutable heap with PMR allocation. One workspace owns the global
heap, vertex-local heaps, and a map from queued endpoint pairs to their local handles.
These heaps are empty between repairs and are reused across source trees, so
heap storage is not duplicated per source. A private
`std::pmr::unsynchronized_pool_resource` retains node allocations for reuse; a
`ygg::CountingMemoryResource` upstream records its retained chunks for `memory_usage()`.
The resource and its heaps have stable ownership when the evaluator moves.
There is no process-wide heap pool or per-source edge-handle array.

Initialization and newly added sources use BFS. Removed sources release their
active slot for reuse. Target-only changes read retained distances without graph
search. No adaptive fallback from expensive repair to fresh BFS is used.

Tuple IDs are retained until `initialize` starts a new baseline. Removed sources'
state buffers are reused for later sources. The distance/support state can occupy
`O(S_peak * V_seen)`, using the peak simultaneous source count and historical
vertex count. Reinitialization resets membership while retaining reusable
capacities. Adjacency, tuple bytes, shared heap nodes and handles, output, and
output deltas add to this footprint. New tuples can grow storage even when live
cardinality is fixed. `memory_usage()` includes retained pool chunks even after
heaps become empty; it excludes the plan and caller-owned inputs. Warm
finite-universe workloads can reuse allocations, but unbounded churn is not
allocation-free.

Results and deltas are borrowed until the next mutation. Inputs must not alias
the evaluator's owned buffers, and calls must be sequential and nonreentrant.
Schema/overlap rejections preserve the previous evaluation. An exception during
mutation requires reinitialization; result/delta access and further updates then
throw `std::logic_error` instead of exposing partial state.

The `database_distance` tests compare full and incremental evaluation with an
independent pairwise BFS, including mixed tuple types, nullary vertices, joint
changes, and undo. The native distance target enables internal work counters for
locality regressions; they are absent from production builds and the public API.
`database_allocations` measures warmed full evaluation, finite-universe source
churn, and edge repair/undo. With `YGGDRASIL_BUILD_PROFILING` enabled,
`database_distance_benchmark` compares full evaluation with incremental updates
for bounded graph sizes and tuple arities 1, 2, 4, and 8. It reports retained
bytes and output rows; timings are measurements, not CI performance thresholds.
Update benchmark iterations measure one forward edge-change batch and its undo,
and report `updates_per_iteration = 2`; divide their displayed time by two for
the mean per-update time. Cases include shortcut replacements and harmless
self-loop changes that leave every distance unchanged. Witness-tail and dense-tie
cases check scaling when updates cancel or most neighboring distances stay fixed. The `initialize_warm`
family measures one baseline initialization per iteration after capacity warmup.
Prepared snapshots and correctness checks are outside the timed loops.

### Distance from Python

The Python API accepts mutable `Relation`, interned `RelationView`, and read-only
`BorrowedRelation` inputs. For vertices consisting of two integers:

```python
from pyyggdrasil import database as db

sources = db.Relation([0, 1])
sources.insert([10, 20])
edges = db.Relation([0, 1, 2, 3])
edges.insert([10, 20, 10, 21])
targets = db.Relation([2, 3])
targets.insert([10, 21])
plan = db.DistancePlan(sources.columns(), edges.columns(), targets.columns(), 4)

result = db.distance(sources, edges, targets, plan)
assert tuple(result[0]) == (10, 20, 10, 21, 1)

evaluator = db.DistanceEvaluator(plan)
evaluator.initialize(sources, edges, targets)
assert tuple(evaluator.get_result()[0]) == (10, 20, 10, 21, 1)
assert evaluator.get_delta().added.empty()
```

`distance` returns an independent owning `Relation`. Incremental `get_result()`
returns a read-only `BorrowedRelation`, and `get_delta()` returns a read-only
`RelationDelta` with `added` and `removed` relations. They and their borrowed rows
keep the evaluator alive; they expose no mutating relation methods. Rows must
not be retained across an evaluator `initialize` or `update`. The `update` method
takes the six added/removed relations in the same order as the C++ interface.
