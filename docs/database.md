# Relational algebra

`<yggdrasil/database/operators/relational/>` provides header-only relational operations in `ygg::database`, one header
per operator: `select.hpp`, `project.hpp` (with `assign`), `join.hpp`, `set.hpp` (`union_`, `difference`) and
`semijoin.hpp` (semijoins and antijoins), sharing `common.hpp`. Relations have a runtime schema and packed,
fixed-width rows. Different columns can hold different types without a tag or variant in every cell.
Nullary relations and runtime arities remain supported.

## Layout

`<yggdrasil/database/>` has one directory per responsibility. Dependencies point one way, down this table (`datalog/`
depends only on `relation/`):

| directory | responsibility |
|---|---|
| `relation/` | schemas, rows, relations, their pools and repositories, and changes (`Delta`) |
| `operators/<family>/` | one family of operators: its plans (`plan.hpp`), full evaluation, and `incremental/`; `relational/` (select, project, join, semijoin, union, difference), `generic_join/`, `distance/` |
| `query/` | query operators as interned records, which hold their operators' plans: tags in `declarations.hpp`, data, views, construction, formatting; `query/rewrite/` holds relaxation and union normal form |
| `optimization/` | `optimize()`, statistics and the planning passes |
| `evaluation/` | evaluators of query DAGs: `full.hpp`, `bound.hpp`, `incremental.hpp`, `incremental_bound.hpp` |
| `search/` | searches over a query's edges: minimum distance, must-pass objects, A* and its trees |
| `datalog/` | the Datalog front end and its rule-level evaluator |

`query/` reads only the `plan.hpp` headers of `operators/`. `detail/` holds headers that only the headers of its
directory include (namespace `detail`). `src/database/` mirrors `include/yggdrasil/database/`.

## Types and schemas

Columns hold one of a fixed set of value types: `uint32_t`, `int32_t`, `uint64_t`,
`int64_t`, `float`, `double`, and `bool` (`ygg::ByteCodable`, in
`<yggdrasil/core/byte_codec.hpp>`). Each has a `ygg::TypeId`, which schemas store,
so relations, schemas, plans and repositories carry no list of value types.
Indices are stored as `uint_t` (`index.get_value()`); the code that owns a relation
knows which tag a column holds and converts back with `Index<Tag>(value)`.

```cpp
#include <yggdrasil/database/operators/relational/project.hpp>
#include <yggdrasil/database/operators/relational/select.hpp>

using namespace ygg::database;
const ygg::Index<Column> source(0), target(1), distance(2);

ygg::Builder<Columns> columns;
columns.push_back<std::uint32_t>(source);
columns.push_back<std::uint32_t>(target);
columns.push_back<double>(distance);
ygg::Builder<Relation> paths(std::move(columns));
paths.insert(std::tuple { std::uint32_t(3), std::uint32_t(8), 2.5 });
paths.insert(std::tuple { std::uint32_t(3), std::uint32_t(9), 4.0 });

const auto length = paths[0].get<double>(distance);
auto reachable = project(paths, {source, target});
auto exact = select_equal_value(paths, distance, 2.5);
```

`Column` identifies a schema-local label. `Columns` follows the usual
`Builder`, `Data`, `Index`, `View` structure. Each `ColumnLayout` records a label,
`TypeId`, byte offset, and width. Identity depends on the ordered labels
and types; offsets and widths are derived and validated. `.span()` borrows the
layout, `.row_size()` returns its packed byte width, and `column_index(label)`
resolves a label. Labels must be unique. Constructing a schema from labels alone
uses `uint_t` for every column; `assign<T>(labels)` picks another type.

`ByteCodec<T>` encodes each of these types with a fixed width. Equal values have
identical encodings and decoding preserves their value, so padding or
uninitialized object bytes never become part of a key. The set of types is
closed; `visit_type_id(id, visitor)` turns a stored `TypeId` back into its type.

Integer and floating-point codecs copy through `memcpy`; packed fields need no
alignment and never expose typed references into bytes. Floating-point encoding
normalizes signed zero and all NaNs, preserving every other finite value and
infinity exactly. Boolean encoding is one byte, 0 or 1. No tolerance rounding is
performed. This is a native-endian in-process representation, not a portable wire
format. Previously serialized homogeneous relation records are incompatible.

## Rows and ownership

`ygg::Builder<Relation>` owns a schema and `RawArraySet<std::byte>`.
`row(i)` exposes canonical bytes for prepared operations. `operator[]` and
`at(i)` return a borrowed `Row`: `get<T>(label)` checks the requested type
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
`View<Row, Context>` for indexing and iteration. Primitive columns remain
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
`RelationViewConcept<V>`.

| Operation | Meaning |
| --- | --- |
| `select(input, predicate)` | Keep rows accepted by a predicate over `Row`. |
| `select_equal_columns(input, a, b)` | Compare columns of the same type. |
| `select_equal_value(input, column, value)` | Match a constant encoded by that column's codec. |
| `project(input, labels)` | Keep the ordered columns and eliminate duplicate rows. |
| `join(lhs, rhs)` | Natural join on common labels, followed by rhs-only columns. |
| `union_(lhs, rhs)` | Union with an identical ordered typed schema. |
| `difference(lhs, rhs)` | Difference with an identical ordered typed schema. |
| `semijoin(lhs, rhs)` | lhs ⋉ rhs: the lhs rows with a match in rhs on the common labels. |
| `antijoin(lhs, rhs)` | lhs ▷ rhs: the lhs rows without such a match. Without common labels, lhs if rhs is empty. |
| `builder.rename(labels)` | Change labels while retaining types and rows. |

Common join labels and equality columns must have matching types. Projection
preserves field types and repacks offsets. Wrong types, incompatible schemas,
duplicate labels, and wrong row widths throw before clearing the output.
Unknown labels throw `std::out_of_range`. Exceptions during evaluation (for
example, a throwing predicate) may leave partial output. An output cannot alias
an input. `assign(destination, source)` supports self-assignment, replaces the
schema and rows, and retains buffers when byte widths match.

Prepare `JoinPlan` and `ProjectionPlan` once per expression.
Their byte slices resolve types, offsets, and widths outside the row loop:

```cpp
const ProjectionPlan plan(paths.columns().span(), {source, target});
ygg::Builder<Relation> output(plan.output_columns().span());
Workspace workspace;
project(paths, plan, output, workspace);
```

Full and incremental operations share byte-key hashing, equality, and field
copying. Row hashing uses the existing byte hashing path. Join keys stream their
selected slices into XXHash; collisions compare actual key bytes. A join indexes
the smaller input with `UnorderedMultiMap`. `JoinIndexCache` may retain
indexes for immutable inputs. Its key includes ordered field types and offsets,
so equal bytes with different interpretations cannot reuse an incompatible index.
Renaming labels can still reuse a compatible index.

## Reuse and interning

Keep a `Workspace` and output builders across evaluations. Scratch byte
buffers, row containers, and hash tables retain capacity. `RelationPool`
uses existing `UniqueObjectPool` instances grouped by **row byte width**, not
logical arity. Prepare a schema once and pass its layout span to checkout to
avoid rebuilding it in the update loop. Larger workloads may still grow storage.
Each evaluator/thread needs its own mutable workspace and pools.

`RelationRepository` interns schemas and compact relation records in the
existing symbol repository. Individual byte rows and sorted row-ID vectors are
interned in `RawVectorSet`s. Relations with identical typed schemas and row sets
share identity regardless of insertion order. Identical bytes may share physical
rows across schemas, but distinct typed schemas retain distinct relation identity.
Iteration follows sorted interned row IDs, not value order.

```cpp
RelationRepositoryFactory factory;
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

`database/operators/relational/incremental/projection.hpp` and `database/operators/relational/incremental/join.hpp` retain
current results and maintain added/removed row sets. Deltas use the same typed
schemas and packed rows. Projection tracks witnesses; a change that replaces a
witness without changing membership produces no output delta. Join supports
updates to either or both operands. `database/operators/relational/incremental/semijoin.hpp`
maintains semijoins and antijoins: only the changed lhs rows and the lhs rows
sharing a key with a changed rhs row can change membership. Every incremental evaluator's `update` takes
one change per input: an `(added, removed)` pair read with `std::get`, such as
`Delta::change()` or `std::tie(added, removed)`; `RelationChange`
states the requirement. Prepared byte operations are shared with full
recomputation. Existing reset, apply, undo, and storage ownership rules remain
unchanged.

## Python

```python
from pyyggdrasil import database as db

columns = db.Columns()
columns.push_back(db.ColumnIndex(0), db.ColumnType.UINT32)
columns.push_back(db.ColumnIndex(1), db.ColumnType.UINT32)
columns.push_back(db.ColumnIndex(2), db.ColumnType.FLOAT64)
relation = db.Relation(columns)
relation.insert([3, 8, 2.5])

repository = db.RelationRepositoryFactory().create()
interned, created = repository.insert(relation)
assert created and tuple(interned[0]) == (3, 8, 2.5)
assert tuple(relation[0]) == (3, 8, 2.5)
assert relation.columns().type(2) == db.ColumnType.FLOAT64
```

The bindings expose the library types directly. A typed schema is a `Columns`
builder; `Relation(labels)` (a list of `ColumnIndex`) defaults every column to
`UINT32`, and `Relation(columns)` also accepts another relation's `columns()`.
`ColumnType` also exposes `INT32`, `UINT64`, `INT64`, `FLOAT32`, and `BOOL`. Rows
yield Python values of the column's type; no packed bytes leak through the Python
API. `RelationPool.get_or_allocate(columns)` takes a schema view, such as
`Columns.columns()`. `RelationRepository.insert(relation, schema_namespace=0)`,
`copy(view)`, and `rename(view, labels)` intern relations. Labels are `ColumnIndex`
values; `.columns()` remains a borrowed `ColumnIndex` sequence with
`type(position)` for schema inspection. Rows, views, and schemas keep their
Python owners alive; explicitly clearing an owner still invalidates them.

Run the native regression suite with
`ctest --test-dir <build> -R '^database_' --output-on-failure`.
It covers full and incremental operations, interned identities, canonical
encodings, schema validation, and retained allocations. Python lifetime and
mixed-type behavior are covered in `python/tests/test_database.py`.

## Tuple distances

`<yggdrasil/database/operators/distance/distance.hpp>` computes directed, unweighted shortest-path
distances between tuple-valued vertices. Sources and targets have `k` columns;
an edge has `2k`, with its source tuple first and target tuple second. All four
vertex positions must have matching field types in the same order. Their column
labels may differ. The distance field is a `uint_t` column.

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
`distance(sources, edges, targets, plan)`. The output/workspace form
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

Include `<yggdrasil/database/operators/distance/incremental/distance.hpp>` and retain one evaluator:

```cpp
incremental::DistanceEvaluator<> evaluator(plan);
evaluator.initialize(sources, edges, targets);
Delta source_change(sources.columns().span());
Delta edge_change(edges.columns().span());
Delta target_change(targets.columns().span());
edge_change.removed.insert(std::tuple{uint_t(7), uint_t(9)});
evaluator.update(source_change.change(), edge_change.change(), target_change.change());
assert(evaluator.get_result().empty());
assert(evaluator.get_delta().removed.contains(
    std::tuple{uint_t(4), uint_t(9), uint_t(2)}));
```

The evaluator owns the compact graph, source/target membership, maintained
distances, output, and last `Delta`. Inputs are borrowed only during a
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
Schema, overlap, aliasing, and membership rejections (adding a present or
removing an absent row) precede mutation and preserve the previous evaluation.
An exception during mutation requires reinitialization; result/delta access and further updates then
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

### Minimum distance

When only the smallest distance between any source and any target is needed, use the
aggregates over the same plans. They reuse `distance` and `incremental::DistanceEvaluator`
unchanged and read the distance column:

```cpp
Builder<Relation<>> result(plan.output_columns().span());
const uint_t full = min_distance(sources, edges, targets, plan, result, workspace);

incremental::SourceMinDistance<> minimum(plan);
minimum.initialize(sources, edges, targets);
minimum.update(source_delta.change(), edge_delta.change(), target_delta.change());
const uint_t current = minimum.get_result();
```

Both return `uint_t`'s maximum when no target is reachable, and 0 when a source equals a target.
The incremental form counts result rows per distance value, so deleting the current minimum
reveals the next one. Memory follows the wrapped evaluator, plus one counter per distance value.

### Must-pass objects

`MustPass` (`search/must_pass.hpp`) finds, per key, the probe objects that every path from the key's
sources to its targets passes in some vertex: the landmarks of the key's graph (Hoffmann, Porteous &
Sebastia, JAIR 2004). Sources, edges and targets start with the same key columns; a vertex has k columns;
probes have one column, and a vertex contains an object if one of its columns of the probes' type holds it:

```cpp
// sources (key…, vertex), edges (key…, from, to), targets (key…, vertex), probes (object)
MustPassPlan<> plan(sources.columns().span(), edge_query.columns(), targets.columns().span(), probes.columns().span(), keys);
MustPass<> must_pass(edge_query, plan);
Builder<Relation<>> out(plan.output_columns().span());   // (key…, object)
must_pass.must_pass(input, sources, targets, probes, out);
```

Like `MinDistance`, the edge query is evaluated on demand with the key and a vertex bound, so each key's
graph is explored from its sources and never materialized. A* finds a shortest witness path; only probe
objects on it can be must-pass. Objects in every source or every target are trivially must-pass (initial facts and
goals are landmarks); every other object on the witness is verified as in landmark detection (Richter,
Helmert & Westphal, AAAI 2008) by testing whether the targets stay reachable without the vertices
containing it. Each search is bidirectional: a backward breadth-first search from the targets alternates
with the forward side by the vertices each expanded, and its exhaustion proves unreachability from the
smaller side. An optional heuristic (`MustPass<H>`, given the key followed by a vertex) guides the
forward side; with `exclude(key, object)` it is told which object a verification avoids. A key whose targets are unreachable has none. An edge
query that projects vertices onto fewer columns is an abstraction: objects that must be passed in the
projection must be passed by every original path too.

### Distance from Python

Column labels are `ColumnIndex` values. Python evaluators and `distance` take
`BorrowedRelation` inputs, or interned `RelationView` inputs when every input of
the call is interned. Wrap a `Relation` explicitly with `db.BorrowedRelation(relation)`;
to mix them, copy a view with `db.assign(db.Relation(), view)`. For vertices
consisting of two integers:

```python
from pyyggdrasil import database as db

def columns(*labels):
    return [db.ColumnIndex(label) for label in labels]

sources = db.Relation(columns(0, 1))
sources.insert([10, 20])
edges = db.Relation(columns(0, 1, 2, 3))
edges.insert([10, 20, 10, 21])
targets = db.Relation(columns(2, 3))
targets.insert([10, 21])
plan = db.DistancePlan(sources.columns(), edges.columns(), targets.columns(), db.ColumnIndex(4))

inputs = (db.BorrowedRelation(sources), db.BorrowedRelation(edges), db.BorrowedRelation(targets))
result = db.distance(*inputs, plan)
assert tuple(result[0]) == (10, 20, 10, 21, 1)

evaluator = db.DistanceEvaluator(plan)
evaluator.initialize(*inputs)
assert tuple(evaluator.get_result()[0]) == (10, 20, 10, 21, 1)
assert evaluator.get_delta().added.empty()
```

`distance` returns an independent owning `Relation`. Incremental `get_result()`
returns a read-only `BorrowedRelation` (the library's `BorrowedRelationView`), and
`get_delta()` returns a `RelationDelta` whose `added` and `removed` relations are
borrowed the same way. They and their borrowed rows keep the evaluator alive; rows
must not be retained across an evaluator `initialize` or `update`;
`db.assign(db.Relation(), result)` copies a result. Like the C++ interface, `update(sources, edges, targets)` takes
one `(added, removed)` tuple per input.

## Query graphs and optimization

`<yggdrasil/database/query/query_repository.hpp>` adds an interned relational query graph without
capturing input rows. A `QueryRepository` only stores and interns query
records; create repositories with a `QueryRepositoryFactory`, whose
repositories have distinct identities. Queries are built like other interned
data: check out a `Data<Query<Tag>>` from a `QueryBuilder`, fill
its operands and parameters, and intern it with `insert_query(repository,
builder, data)` from `query_construction.hpp`. That inserts the concrete record
and its heterogeneous root. `prepare_for_insert` validates operands, schemas,
constants, and execution orders and derives the prepared metadata. Query handles
borrow the repository. Input-slot numbers identify bindings at evaluation time;
repeated references to a slot use the same input relation.

The data, view, and repository definitions are separated into
`query_data.hpp`, `query_view.hpp`, and `query_repository.hpp`; the index is the generic `Index<T>`.
`Query<Tag>` identifies a concrete operation; `Query<>` is the
heterogeneous root whose data stores a variant of concrete query indices. Each
concrete data type stores only its own operands and parameters. For example,
`QueryJoinTag` has `lhs` and `rhs` query indices, while `QuerySelectValueTag` has
`arg`, `column`, and an encoded `constant`. Derived output columns are cached
but do not participate in operator identity. Input and empty schemas, and the
ordered labels requested by projection or rename, do participate in identity.

`insert_query` returns `QueryView`. Its `get_variant()` exposes
concrete views with operation-specific getters such as `get_lhs()`, `get_arg()`,
and `get_constant()`. All query views expose `columns()`; every view borrows its
repository. `size()` counts heterogeneous root queries, not the separate
concrete operator records.

Queries use the same typed representation during optimization and evaluation.
Join, projection, distance, and Generic Join records retain their prepared
execution plans. Selection records retain resolved column positions. These
caches are derived during validated construction and excluded from interning
identity. Their output schemas are read directly from the prepared plans.
Generic Join is a query constructor with input queries, a variable order, and
an independent output order. An empty output order is canonicalized to the
natural union of the input columns.

`compile(roots)` produces a copyable, read-only `QueryPlan`
that owns a const query repository. Only reachable queries are copied, with
children preceding their parents; shared queries and repeated roots stay shared.
The plan preserves root order and ordered, typed output schemas and remains valid
after its source repository is destroyed. `node_count()`, `root_count()`, and
`roots()` expose its shape; indexing by a root query index returns an ordinary
`QueryView`. Row enumeration order is not a query guarantee.

Include `<yggdrasil/database/query/formatter.hpp>` to format a plan with
`fmt::format("{}", plan)`, `ygg::to_string(plan)`, or `explain(plan)`.
Python's `plan.explain()` uses the same rendering. The `fmt::formatter`
specialization follows `YGG_ENABLE_FMT_FORMATTERS`; `explain()` remains available
when public formatters are disabled.

`<yggdrasil/database/optimization/optimization.hpp>` plans the roots from what is known about the data:

```cpp
using namespace ygg::database;
QueryRepositoryFactory<> factory;
auto repository = factory.create();
QueryBuilder<> builder;
const auto input = [&](size_t slot, const auto& relation)
{
    auto data = checkout<Query<DefaultColumnTypes, QueryInputTag>>(builder);
    data->input_slot = slot;
    data->columns.set(relation.columns().begin(), relation.columns().end());
    return insert_query(repository, builder, *data);
};
auto join = checkout<Query<DefaultColumnTypes, QueryJoinTag>>(builder);
join->lhs = input(0, left_relation).get_index();
join->rhs = input(1, right_relation).get_index();
const auto query = insert_query(repository, builder, *join);

// Any range of relation views: interned RelationView or BorrowedRelationView over builders.
const std::array bindings { left_view, right_view };
const auto structural = optimize(query);                                   // structure only
const auto measured = optimize(query, collect_statistics<DefaultColumnTypes>(bindings));
QueryEvaluator<> evaluator(measured);
evaluator.evaluate(bindings);
const auto& result = evaluator.get_result();
```

`optimize(roots, statistics = {})` returns a `QueryPlan` with every root's ordered
schema. It implements established algorithms as published:

1. **Normalization.** The textbook algebraic rewrites [GMUW08]: selections are
   pushed down through joins, projections, renames, unions, and differences, and
   into the lhs of semijoins and antijoins, projections through renames and
   unions, and empty relations are propagated.
2. **Join blocks.** Each maximal tree of joins is planned as a whole. Unions,
   differences, semijoins, antijoins, distances, and selections spanning several
   join operands bound the blocks.
3. **Planning each block.** A block whose operands all have known cardinalities
   (measured inputs, observed queries, or queries over them) is planned
   cost-based, any other block from structure:
   - *Cost-based:* DPccp [MN06] finds the bushy join order of least C_out
     [CM95, Leis15] under System R estimates [Selinger79], with the textbook
     estimates of [GMUW08] for projection, union, and difference, T(R ⋉ S) =
     min(T(R), T(π_R(R ⋈ S))) and T(R ▷ S) = T(R) − T(R ⋉ S), and observed
     cardinalities replacing estimates [LEO01]; disconnected components are
     joined last by cross products. The join tree is then refined by Algorithm 4
     of [Freitag20]: growing joins and their ancestors become one
     worst-case-optimal join.
   - *Structure:* if the GYO reduction [Graham79, YO79] shows the block acyclic,
     Yannakakis' algorithm [Yannakakis81]; otherwise Generic Join [NRR13], whose
     running time is bounded by the AGM bound [AGM08].

   Generic Join's variable order is fixed by column label: any order attains the
   worst-case bound, but [Freitag20] additionally optimizes it with the Tributary
   Join cost model, which is not implemented. Distances have no estimate in the
   literature and are only known when observed.
4. **Compilation** of the planned roots into an owning `QueryPlan`.

Union and difference bound join blocks, so joins are not distributed over unions,
and blocks of different roots are planned independently (shared subplans are
still interned once). Planning for incremental maintenance is not modeled.

References:
[Selinger79] P. G. Selinger et al., *Access Path Selection in a Relational
Database Management System*, SIGMOD 1979.
[GMUW08] H. Garcia-Molina, J. D. Ullman, J. Widom, *Database Systems: The
Complete Book*, 2nd ed., 2008, ch. 16.
[Graham79] M. H. Graham, *On the Universal Relation*, University of Toronto, 1979.
[YO79] C. T. Yu, M. Z. Özsoyoğlu, *An Algorithm for Tree-Query Membership of a
Distributed Query*, COMPSAC 1979.
[Yannakakis81] M. Yannakakis, *Algorithms for Acyclic Database Schemes*, VLDB 1981.
[AGM08] A. Atserias, M. Grohe, D. Marx, *Size Bounds and Query Plans for
Relational Joins*, FOCS 2008.
[NRR13] H. Q. Ngo, C. Ré, A. Rudra, *Skew Strikes Back: New Developments in the
Theory of Join Algorithms*, SIGMOD Record 2013.
[MN06] G. Moerkotte, T. Neumann, *Analysis of Two Existing and One New Dynamic
Programming Algorithm for the Generation of Optimal Bushy Join Trees without
Cross Products*, VLDB 2006; corrected exclusion set from PVLDB 11 (2018), p. 1069.
[CM95] S. Cluet, G. Moerkotte, *On the Complexity of Generating Optimal Left-Deep
Processing Trees with Cross Products*, ICDT 1995.
[Leis15] V. Leis et al., *How Good Are Query Optimizers, Really?*, PVLDB 9(3), 2015.
[Freitag20] M. Freitag, M. Bandle, T. Schmidt, A. Kemper, T. Neumann, *Adopting
Worst-Case Optimal Joins in Relational Database Systems*, PVLDB 13(11), 2020.
[LEO01] M. Stillger, G. Lohman, V. Markl, M. Kandil, *LEO – DB2's LEarning
Optimizer*, VLDB 2001.

`prepare_for_insert` checks schemas, constants, and execution orders before
publishing immutable query data. Query records hold plain indices, so callers
keep operands in the repository they insert into. The roots passed to `optimize`
belong to one repository.

### Relaxation

`relax(roots, known, repository, builder)` (`query/rewrite/relaxation.hpp`) over-approximates queries whose
inputs not marked `known(slot)` may hold any rows. Each result Q̂ reads only the known inputs, which are renumbered once
for all roots into `Relaxation::slots`, and has a subset S' of the query's labels. For all inputs, Q ⊆ Q̂ × U^(labels
not in S'), and `nullopt` means nothing is constrained.

Relaxing π_L(Q) needs only the labels L. Labels no one needs are eliminated as early as possible:
- a conjunct that shares nothing needed becomes a nullary existence test;
- antijoins and differences are kept only as exact filters on needed labels.

This is the projection of pattern databases. The header has the full translation table and references. Evaluate the
results like any query, e.g. after `optimize()`.

### Statistics

`Statistics` describes what is known; everything is optional and nothing
known means an unbounded domain:

- `objects`: the domain size. It caps distinct counts; without it the domain is
  unbounded.
- `inputs`: per input slot, a `RelationStatistics` with `rows` and distinct counts
  keyed by `Index<Column>`. A missing distinct count is the row count, capped by
  `objects`.
- `expressions`: observed results of queries in the roots' repository, keyed by
  `query.get_index()`. They replace the derived estimate of the equivalent planned
  query [LEO01].

Python keys distinct counts by plain column labels and expressions by
`QueryIndex`. `collect_statistics(bindings)` scans relations to fill `inputs`.
Estimates assume independent, uniform columns and containment of join columns
[Selinger79]; conjunctions are normalized so join association never changes the
estimate, and projection starts a new scope for its hidden columns. Statistics
guide planning only; they never justify replacing an input by a constant or an
empty relation.

### Evaluators

Evaluators differ along two independent axes: what they return, the whole result (*full*, the default) or answers to
lookups with some output columns given (*bound*); and whether they follow input changes (*incremental*) or evaluate
the current inputs only (*one-shot*, the default). A namespace exists only for a non-default mode, its header under
`<yggdrasil/database/evaluation/>` has the same name, and the class is always `QueryEvaluator`:

| | one-shot | incremental |
|---|---|---|
| full | `QueryEvaluator` (`full.hpp`) | `incremental::QueryEvaluator` (`incremental.hpp`) |
| bound | `bound::QueryEvaluator` (`bound.hpp`) | `incremental::bound::QueryEvaluator` (`incremental_bound.hpp`) |

Use full evaluation for a whole result, bound evaluation for many selective lookups against the same inputs (the edge
oracle of the distance search), and an incremental evaluator when the inputs change between uses. Bound evaluation
compiles the query into union normal form (`query/rewrite/union_normal_form.hpp`) and plans each branch per set of bound
columns (`evaluation/conjunctive_plan.hpp`). Every incremental evaluator takes its first inputs with `initialize` and exact
changes with `update`; the bound ones also offer `bind`, which diffs the inputs when the changes are unknown, and
`seed`, which reports the answers a change can add. The operator plans of the query records (`JoinPlan`, `ProjectionPlan`,
`GenericJoinPlan`, `DistancePlan`) are shared by all evaluators and stay in `ygg::database`.

`<yggdrasil/database/evaluation/full.hpp>` provides `QueryEvaluator`.
`evaluate(bindings)` computes all roots, reusing retained output and scratch
storage. `<yggdrasil/database/evaluation/incremental.hpp>` provides
`incremental::QueryEvaluator`. Initialize it once with input bindings,
then call `update(changes)` with one `(added, removed)` pair of relation views per
input slot. Each pair contains actual set additions and removals in that input's ordered typed schema.
An empty change uses an empty relation with the correct schema. Swap additions
and removals to undo a transition.

`get_result(root_index)` borrows a root's current relation;
`get_delta(root_index)` borrows exact net added/removed rows after an incremental
update. Initialization clears output deltas. A subsequent evaluation or update
can invalidate borrowed rows. Input adapters and their underlying storage need
only remain alive during the call. Results belong to the evaluator, and may not
be fed back as aliased inputs to that same evaluator. `memory_usage()` reports
retained evaluator storage; it is distinct from the optimizer's estimate.

#### Distance search

`MinDistance<H>` (`search/min_distance.hpp`) and `incremental::MinDistance<H>`
(`search/incremental/min_distance.hpp`) compute the exact minimum distance from any source to any
target over edges given by a query over input slots (or by one edge relation), and return
`uint_t`'s maximum when no target is reachable. Edges are never materialized: a bound
evaluator answers successor, predecessor and edge-check lookups, and A* (a binary heap
ordered by f, then g) runs against a backward breadth-first frontier.

- `MinDistance::min_distance(input, sources, targets, heuristic)` binds the inputs for one
  call; only compiled plans and static slots are kept.
- `incremental::MinDistance` keeps the search tree across calls. `initialize(input)` binds a
  new baseline; `bind(input, maybe_changed)` diffs the inputs against it, and
  `update(change, changed)` takes exact `(added, removed)` changes per slot. A change that
  throws makes the next search start afresh.
  Its `set_targets(targets)` sets the targets, and different targets discard the stored
  search; `min_distance(sources, heuristic)` then searches over the current inputs.

The stored search tree (`search/search_tree.hpp`) keeps per vertex a learned lower bound and
a next pointer with its path's length, an upper bound while the path's edges exist. A search
ends on a stored path whose length meets the vertex's lower bound, or once nothing shorter than
the best stored path seen can remain. A* learns with its whole estimate, the backward frontier's
bound included: the learned values stay consistent with the estimates of unstored vertices,
which repair after added edges relies on, and a new frontier forgets them. A* still reopens a
vertex reached more cheaply, which never happens under a consistent estimate.

The heuristic `H` (`search/concepts.hpp`, `DistanceHeuristic`) must be consistent in every
graph the inputs can form while the targets stay unchanged: it may depend on the targets and
on static inputs only. `BlindDistanceHeuristic` is the zero estimate.

### Python query API

```python
from pyyggdrasil import database as db

x, y, z = db.ColumnIndex(0), db.ColumnIndex(1), db.ColumnIndex(2)
left = db.Relation([x, y])
left.insert((1, 2))
right = db.Relation([y, z])
right.insert((2, 3))

repository = db.QueryRepositoryFactory().create()

def insert(data):
    concrete, _ = repository.insert(data)  # e.g. a QueryJoin view
    return repository.insert(db.QueryData(concrete))[0]  # the type-erased Query

a = insert(db.QueryInputData(0, left.columns()))
b = insert(db.QueryInputData(1, right.columns()))
query = insert(db.QueryProjectData(insert(db.QueryJoinData(a, b)), [z, x]))
inputs = [db.BorrowedRelation(left), db.BorrowedRelation(right)]
optimized = db.optimize([query], db.collect_statistics(inputs))
evaluator = db.QueryEvaluator(optimized)
evaluator.evaluate(inputs)
assert {tuple(row) for row in evaluator.get_result()} == {(3, 1)}
independent = db.assign(db.Relation(), evaluator.get_result())
print(optimized.explain())
```

Like the C++ library, every operator has an index (`QueryJoinIndex`), a record
(`QueryJoinData`), and a view (`QueryJoin`) with its getters; `repository.insert(data)`
returns `(view, created)`. `QueryData(view)` makes the type-erased `Query` that
operators take as operands. The records are `QueryInputData(slot, columns)`,
`QueryEmptyData(columns)`, `QueryJoinData`/`QueryUnionData`/`QueryDifferenceData`/`QuerySemijoinData`/`QueryAntijoinData(lhs, rhs)`,
`QueryProjectData`/`QueryRenameData(arg, labels)`, `QuerySelectEqualData(arg, lhs, rhs)`,
`QuerySelectValueData(arg, column, type, value)`, `QueryDistanceData(sources, edges,
targets, distance_column)`, and `QueryGenericJoinData(inputs, variable_order,
output_order=[])`. A select constant must have the selected column's type.
Query repositories come from a `QueryRepositoryFactory`; repositories of one factory
have distinct identities, so queries compare across them. `compile(roots)` and
`optimize(roots, statistics=Statistics())` take a list of roots and return a `QueryPlan`.

`IncrementalQueryEvaluator(plan)` exposes `initialize(inputs)` and
`update(changes)` with one `(added, removed)` pair per input slot. Its result and
`RelationDelta` relations are read-only `BorrowedRelation` views that keep the
evaluator alive; `db.assign(db.Relation(), relation)` copies one. Query
handles keep their repository alive; compiled plans and evaluators are independent
of it. For sparse slot numbering, an unused slot takes an empty `Relation()`.
Dictionary-valued statistics fields are converted by value; construct them with
keyword dictionaries or replace the field to change entries.

### Query benchmarks

`database_query_benchmark` separates optimizer time from execution. It compares
full and incremental evaluation of the original plan and the plans from structure
and from statistics, validates
forward/undo results and net deltas before timing, and reports retained bytes.
The cold/warm Generic Join cases include full trie construction; warm means
reusing the evaluator's allocated workspace, not skipping input indexing. The
initial triangle fixture is a reproducible correctness and performance smoke
workload, not a broad claim about optimizer quality.

## Datalog

`<yggdrasil/database/datalog/>` is a stratified Datalog engine over relations of object columns, with aggregates. Its
records are interned in a `DatalogRepository` (`repository.hpp`), built with `checkout`/`insert` and validated by
`prepare_for_insert` (`construction.hpp`):

- a relation: an input (with an input slot of the evaluator) or a derived relation (with an id);
- an atom: a relation applied to terms, each a variable numbered within its rule or an object constant;
- an aggregate: `result = function value : atoms, not negated, comparisons`, grouped by the variables that the
  enclosing rule's positive atoms bind; COUNT and SUM are 0 for a group without rows;
- a rule: `head ← atoms, not negated, comparisons, aggregates, arithmetic`, with a derived head of distinct variables
  and every variable bound by a positive atom, an aggregate's result or an arithmetic result;
- comparisons `=`, `≠`, `<`, `≤` over values, and arithmetic `result = lhs op rhs` (`+ − × ÷ min max`, in order, each
  reading the variables bound before it) over {0, …, M − 1} ∪ {∞} with M the largest `uint_t` as ∞: `+` and `×`
  saturate to ∞, `−` at 0, `÷ 0` is ∞ (runir's numerical semantics);
- a weighted relation: a derived relation whose last column is a weight, keeping per row of its other columns the least
  weight derived; a rule into it computes the head's weight as the sum or maximum (`+`, `max`) of its weighted atoms'
  weights and other terms, and uses those weights nowhere else (no filters or joins on them), so that the weight
  functions are non-decreasing and superior; weighted and unweighted relations are not recursive together;
- a program: rules, stratified by the strongly connected components of their dependencies (`detail/stratification.hpp`).

`Evaluator(program, outputs, static_slots)` (`evaluator.hpp`) evaluates the requested relations rule by rule over
indexed relations, without intermediate relations:

- only the strata the outputs depend on are evaluated, and only the inputs they read are read; each stratum is one
  object of its kind (`detail/strata.hpp`: plain, recursive, or an aggregate's), which owns the state it maintains;
- each rule is an index nested-loop join, planned once per literal that reads a change, with the most bound literal
  next (ties go to relations outside the rule's stratum); hash indexes are chosen per access pattern;
- atoms whose only unbound variables are those a step binds are intersected with it: the one with the fewest matches
  for its key enumerates, the others are checked (Generic Join [NRR13]), so cyclic bodies stay worst-case optimal;
- `evaluate(inputs)` is stratified and semi-naive;
- `initialize(inputs)` and `update(changes)` maintain the relations: a stratum without recursion by derivation counts,
  a recursive one by Delete and Rederive [Gupta93]; when the over-deleted rows exceed a tenth of the stratum's rows,
  the stratum is evaluated anew and its change is the difference to the old rows;
- an aggregate keeps the satisfying assignments of its body by counting and its groups incrementally;
- a stratum of weighted relations is evaluated by Knuth's lightest derivation algorithm [Knuth77, as stated in FM07 §3]:
  rows settle in order of weight from a priority queue, each expanded by the rules that read it with the settled rows;
  if only goals read it (a minimizing head over its relation and targets from earlier strata, and none of its relations
  an output), it stops once the goals hold; a goal with key columns, which the stratum's rules keep, holds per key, and
  the rows of a key whose goal holds are no longer settled (implementation decision, not from [FM07]). It searches by
  A\* [FM07 §4] with a heuristic derived from its rules [FM07 §5.1]: their static literals give an abstract weighted
  graph per key column (single-column pattern databases [CS98], as `n_distance`'s heuristic; rules that keep the column
  are self-loops there and left out), whose distances to the current targets, computed by Dijkstra's algorithm
  backward from them once per set of targets and kept while the static inputs stay the same, bound the weight to the
  nearest target from below; h is their maximum. With one goal whose key columns are target columns, each goal key has
  its own tables, to its own targets (as `n_distance`'s keyed heuristic), computed on its first estimate. With one goal whose targets bind every key column, a breadth-first
  search per goal key backward from its targets over the rules reversed (all their literals, in the current state)
  advances with the forward one, and its exhaustion without reaching a source proves that the key's goal has no row
  (bidirectional search [Pohl71]; implementation decision). With unit costs as well (base rules of weight 0, recursive
  rules adding 1), each goal key is searched as `n_distance` searches (`search/incremental/min_distance.hpp`): A\*
  over the rules as edges with learned distances [GAA*], stored paths [MPGAA*] and a backward frontier, kept across
  updates while the key's targets and the heuristic stay the same; the edges an update adds are derived by the rules
  with a changed literal reading its change (columns the rule keeps and leaves unbound stand for every stored key)
  and repaired, or restart the frontier where they break its bounds; otherwise an update evaluates the stratum anew.
  If no weight of the goals' heads can be observed (no output or aggregate reads them, and no rule uses them but by
  copying them into such a head), a goal holds with any weight, and each search ends at the first path it finds,
  a valid stored one included (implementation decision);
- `initialize()` of an initialized evaluator compares its inputs with the rows read last and updates by the difference,
  so that what strata maintain carries over;
- static input slots are read once.

Python exposes the records as `DatalogRelationData`, `DatalogAtomData`, `DatalogAggregateData`, `DatalogRuleData`,
`DatalogProgramData` and the evaluator as `DatalogEvaluator`.

`database_datalog_benchmark` (`YGGDRASIL_BUILD_PROFILING`) measures the per-state cost of planning-like programs: full
evaluation per state, and maintenance between two successor states.

[CS98] J. C. Culberson, J. Schaeffer, *Pattern Databases*, Computational Intelligence 14(3), 1998.
[GAA*] X. Sun, S. Koenig, W. Yeoh, *Generalized Adaptive A\**, AAMAS 2008.
[MPGAA*] C. Hernández, R. Asín, J. A. Baier, *Reusing Previously Found A\* Paths for Fast Goal-Directed Navigation in
Dynamic Terrain*, AAAI 2015.
[Pohl71] I. Pohl, *Bi-directional Search*, Machine Intelligence 6, 1971.
[Gupta93] A. Gupta, I. S. Mumick, V. S. Subrahmanian, *Maintaining Views Incrementally*, SIGMOD 1993.
[FM07] P. F. Felzenszwalb, D. McAllester, *The Generalized A\* Architecture*, JAIR 29, 2007.
[Knuth77] D. E. Knuth, *A Generalization of Dijkstra's Algorithm*, Information Processing Letters 6(1), 1977.
