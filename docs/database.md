# Relational algebra

`<yggdrasil/database/operations.hpp>` provides a small, header-only library in
`ygg::database`. Relations contain sets of fixed-arity tuples, including arity
zero. Intermediate relations have no special arity limit.

Public headers expose the API and include their implementations from `details/`
automatically. Continue including the public headers; no separate library or
additional include is required.

`ygg::Builder<Relation<T>>` owns rows stored in Yggdrasil's `RawArraySet<T>`. The default value
type is `ygg::uint_t`, suitable for interned object IDs. Values must be trivially
copyable, support copying into scratch vectors, and provide consistent
`ygg::Hash<T>` / `ygg::EqualTo<T>` semantics. Column labels are query-local
`ygg::Index<Column>` values with `ygg::uint_t` storage; callers can intern variable
names once rather than hashing strings while evaluating tuples. `Column` is an
entity tag distinct from tuple values. All columns have the same value type.

`Columns` is the schema entity tag. `ygg::Builder<Columns>` owns an ordered
schema and enforces unique labels. Relation builders and plans own schema
builders; canonical schemas live in their repository. Relation builders expose
their schema builder by const reference. Contextual relations and plans expose
standard `ygg::View` schema views. Use `.span()` for a borrowed
`std::span<const ygg::Index<Column>>`, or `ygg::make_view(columns, context)`
for contextual access. Borrowing does not validate or allocate. Borrowed labels
must remain alive and unchanged. Temporary owning schemas cannot be borrowed.
`columns.assign(labels)` validates the replacement labels, retains capacity
where possible, and invalidates existing views. Lookup through
`column_index(label)` throws if the label is missing. These types are available
through `columns.hpp`.

## Example

```cpp
#include <yggdrasil/database/operations.hpp>

using namespace ygg::database;
constexpr ygg::Index<Column> package {0}, truck {1}, destination {2};

ygg::Builder<Relation<>> options({package, truck, destination});
options.insert({10, 20, 30});
options.insert({10, 21, 31});
options.insert({11, 20, 31});

ygg::Builder<Relation<>> goals({package, destination});
goals.insert({10, 30});
goals.insert({11, 31});

// Match both package and destination before dropping the destination.
auto matched = join<ygg::uint_t>(options, goals);
auto eligible = project<ygg::uint_t>(matched, {package, truck});
// eligible contains (10,20) and (11,20).

auto for_truck_20 = select_equal_value<ygg::uint_t>(eligible, truck, 20);
auto packages = project<ygg::uint_t>(for_truck_20, {package});
auto guard = project<ygg::uint_t>(packages, {});
bool any_package = !guard.empty();

// Reuse both output and scratch storage on subsequent evaluations.
ygg::Builder<Relation<>> output({package, truck});
Workspace<> workspace;
project(matched, {package, truck}, output, workspace);
```

The library operates on concrete relations; it does not parse a query language
or attach meaning to predicate, variable, or object IDs.

## Operations

Materializing operations accept read-only inputs satisfying `RelationViewConcept`,
including builders directly and contextual views over builders, data records,
or typed indexes.

`row(i)` provides the stored `std::span<const T>` used by relational operations.
Contextual indexing, `at(i)`, and iteration return generic span views from
`<yggdrasil/containers/span.hpp>`. These copy the span handle and borrow its
elements and context; their iterators also remain valid after the temporary
view is destroyed. Elements resolve through `get_repository(context)`, so a
relation of typed indices can yield semantic object views without copying rows.
The element repository may differ from `get_relation_repository(context)`,
which owns the relation's data. `repository.rename(view, labels)` preserves
the source view's context while sharing its rows.

Materializing operations have an explicit element-type parameter `T` and
constrain each input with `RelationViewConcept<V, T>`. Overloads taking an
output builder or workspace deduce `T` from those arguments. Convenience
overloads that return a new builder require it explicitly, for example
`join<ygg::uint_t>(lhs, rhs)` or `project<ygg::uint_t>(input, labels)`.
Renaming does not require an element-type argument.

| Function | Semantics |
| --- | --- |
| `builder.rename(labels)` | Replace the builder's column labels in place, retaining its rows. |
| `repository.rename(canonical_view, labels)` | Intern replacement labels and share canonical rows. |
| `select<T>(input, predicate)` | Keep rows for which the predicate returns true. |
| `select_equal_columns<T>(input, a, b)` | Keep rows with equal values in the two labelled columns. |
| `select_equal_value<T>(input, column, value)` | Keep rows matching a constant. |
| `join<T>(lhs, rhs)` | Natural join on all common labels; output has lhs columns followed by rhs-only columns. |
| `project<T>(input, labels)` | Keep columns in the supplied order and eliminate duplicates. |
| `union_<T>(lhs, rhs)` | Set union. The suffix avoids the C++ keyword. |
| `difference<T>(lhs, rhs)` | Set difference. |

The selection predicate receives `std::span<const T>` in input column order.
Use `view.column_index(label)` once outside the predicate to resolve a label.
Unary relation membership can also be expressed as a join with that relation.

Union and difference require identical ordered schemas. Use projection to align
different orders. A rename changes labels, not tuple positions, and cannot merge
two columns into one. For a repeated variable, select equality before projecting
away the redundant column. Duplicate labels and wrong tuple lengths are errors.
Unknown labels throw `std::out_of_range`; incompatible schemas throw
`std::invalid_argument`.

Every materializing operation also accepts a final `ygg::Builder<Relation<T>>& out` argument.
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
const JoinPlan joining(options.columns().span(), goals.columns().span());
const ProjectionPlan projecting(joining.output_columns().span(), {package, truck});
ygg::Builder<Relation<>> matched({package, truck, destination});
ygg::Builder<Relation<>> eligible({package, truck});
Workspace<> workspace;

// For each state, after refreshing options:
join(options, goals, joining, matched, workspace);
project(matched, projecting, eligible, workspace);
```

Prepared execution uses stored positions without resolving labels again. It
checks that the ordered input/output schemas match the plan before clearing
the output; renamed or reordered inputs need a matching plan. Nullary schemas
are supported. Plans hold no rows or state-specific indexes and can be reused
with different relation objects, copied, or shared across evaluators. Each
evaluator still needs its own workspace and outputs. The hash index is rebuilt
from current rows, and the smaller join input is chosen each time.

Pass builders directly when a context is unnecessary. A contextual view created
with `ygg::make_view(builder, context)` borrows the builder and context without
allocating; both must outlive the view. It observes the builder's current rows
and schema.

To rename a builder in a state loop, prepare the labels once and assign them
without changing its rows:

```cpp
const ygg::Builder<Columns> renamed_columns {package, truck};
// Inside each evaluation:
eligible.rename(renamed_columns.span());
```

The builder owns its assigned labels. Renaming requires matching arity and
invalidates previously borrowed schema spans. Creating or assigning a
`ygg::Builder<Columns>` validates uniqueness, including during pool checkout and relation
reinitialization. The repository also validates schemas when publishing
canonical relation records. Borrowing a schema view or label span performs none
of these checks; arity and operation-specific schema compatibility checks still apply.

For recursive evaluation, `<yggdrasil/database/relation_pool.hpp>` provides
`RelationPool<T>`, using Yggdrasil's `UniqueObjectPool` separately for each arity:

```cpp
RelationPool<> temporaries;
Workspace<> workspace;
const JoinPlan joining(options.columns().span(), goals.columns().span());
const ProjectionPlan projecting(joining.output_columns().span(), {package, truck});

// Inside each evaluation; fill options for the current state first.
auto matched = temporaries.get_or_allocate(joining.output_columns().span());
join(options, goals, joining, *matched, workspace);
auto eligible = temporaries.get_or_allocate(projecting.output_columns().span());
project(*matched, projecting, *eligible, workspace);
// Consume eligible before releasing its handle. Handle destruction returns the
// whole relation, including its allocated buffers, to the pool.
```

Checkout clears old rows and sets the labels. Grouping by arity avoids replacing
the fixed-arity tuple storage when different intermediate schemas are needed.
`Builder<Relation<T>>::initialize(labels)` supports the same reuse with a caller-managed
`UniqueObjectPool<ygg::Builder<Relation<T>>>`; keep each such pool at one arity.
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

## Interned relations

`<yggdrasil/database/relation_repository.hpp>` separates mutable computation
from canonical storage using the same `Builder`, `Data`, `Index`, and `View`
structure as other Yggdrasil entities. `Relation<T>` is the entity tag:

- `ygg::Builder<Relation<T>>` owns reusable mutable schema and row buffers.
- `ygg::Data<Relation<T>>` contains an `Index<Relation<T>>`, an
  `Index<Columns>` for its canonical schema, an `Index<RelationRowSet<T>>`
  for its canonical row-ID vector, and the caller's schema namespace.
  The assigned relation index is excluded from `identifying_members()`.
- `ygg::Index<Relation<T>>` identifies a record within one relation repository.
- `ygg::View<Handle, Context>` provides read-only relation access for a
  `Builder<Relation<T>>`, `Data<Relation<T>>`, or `Index<Relation<T>>` handle.
  Builder and data views borrow their handle; index views resolve their record
  through the context. Canonical data and index views access the repository's
  schemas and rows directly. All three satisfy `RelationViewConcept`.

Schemas use the same structure. `ygg::Data<Columns>` contains its assigned
`ygg::Index<Columns>` and an owned `cista::offset::vector<ygg::Index<Column>>`
of labels; its identity depends only on the ordered labels. Contextual views
over `Builder<Columns>`, `Data<Columns>`, and `Index<Columns>` provide read-only
schema access and satisfy `ColumnsViewConcept`. Indexed schema views resolve through
`get_columns_repository(context)`.

Use `insert(repository, builder)` to obtain a canonical schema view
and a boolean indicating whether it was inserted. The lower-level path is
`assign(data, builder)` followed by `repository.insert(data)`;
`repository.find(data)` and `repository[index]` also accept the schema entity.
`repository.insert(labels)` accepts a typed label span, available from
builders and schema views through `.span()`, and returns the canonical schema
view together with its insertion flag. It copies labels into retained scratch storage before
publication, so subsequent calls within warmed capacities do not allocate.
Schema publication validates unique labels. Canonical schema records use the
existing symbol repository arena; there is no additional schema interner.

Canonical row-ID vectors contain `ygg::Index<RelationRow<T>>` values.
The schema, row, and row-set index types use the existing `IndexMixin` with
`uint_t` storage. Raw row containers still address slots with `uint_t`; the
repository converts between those slots and typed handles at its boundary.
This keeps schema, row, and row-set references distinct without changing the
generic containers.

`RelationView<T>` names the index view with a `RelationRepository<T>`
context. Its identity combines the typed index and the repository's
factory-local identity. Create repositories used together from one
`RelationRepositoryFactory<T>`. A custom context can provide the repository
through `get_relation_repository(context)`.

```cpp
#include <yggdrasil/database/relation_repository.hpp>

using namespace ygg::database;
RelationRepositoryFactory<> factory;
auto repository = factory.create();
using ColumnIndex = ygg::Index<Column>;
ygg::Builder<Relation<>> builder({ColumnIndex {0}, ColumnIndex {1}});
builder.insert({10, 20});

auto [result, inserted] = insert(repository, builder);
auto pending = ygg::make_view(builder, repository);
auto stored = ygg::make_view(result.get_data(), repository);
// result, pending, and stored expose the same read-only relation operations.

const ygg::Builder<Columns> labels {ColumnIndex {2}, ColumnIndex {3}};
auto renamed = repository.rename(result, labels.span());
builder.clear(); // result and renamed retain their canonical rows.
```

Interning stores each distinct row in a `RawVectorSet`, sorts and deduplicates
its row IDs, and interns that ID vector in another `RawVectorSet`. Ordered
schemas and compact relation records are interned in one
`SymbolRepository<Columns, Relation<T>>`. Relations with the same schema and rows therefore share
one identity regardless of builder insertion order. Canonical iteration follows
sorted row IDs, whose assignment depends on the repository's row-interning
history; it does not preserve builder insertion order or sort row values.

The builder remains independent and can be reused immediately. Relations can
share individual canonical rows as well as entire row-ID vectors. Canonical
relations are renamed through `repository.rename`, which interns another
schema while retaining the same row-ID vector. A builder's `rename` method
changes its own schema in place. Pass a
`schema_namespace` when the same numeric labels belong to different
caller-owned naming contexts.
Renaming preserves the source namespace unless an explicit replacement is
supplied, and requires a source from the same repository.

Repository `clear()` invalidates its views, rows, and labels while retaining
storage for reuse. Clear any memoization and join indexes borrowing that
storage first. When temporary builders and canonical relations share a join
index cache, construct their factories from the same `RelationPoolFactory<T>`
so their row-storage identities share one sequence.

Python keeps `database.Relation` as the mutable builder and exposes
`RelationRepositoryFactory`, `RelationRepository`, `RelationIndex`,
`RelationView`, and `insert(repository, builder, schema_namespace=0)`. The Python insertion function returns `(view, inserted)`.
Python constructors, pool checkout, and rename accept integer column labels;
`.columns()` returns a read-only borrowed `ColumnIndices` sequence exposing
integers without copying the schema. Rows use the separate `RelationRow` type.
Views, borrowed rows, and labels keep their owners alive; an explicit repository
`clear()` still invalidates them. Resetting memoization alone leaves them valid.

## Nullary relations and object domains

`ygg::Builder<Relation<>> boolean;` is initially false (no tuples). `boolean.insert({});`
makes it true (the unique empty tuple). Projecting a nonempty relation to zero
columns produces true. Joining with true preserves a relation; joining with
false empties it. These rules also hold with an empty object domain.

Represent the object domain by an ordinary unary relation populated with all
task objects, including objects absent from other predicates. Rename and join
copies to construct domain products when needed for complementation. State,
goal, and register relations are ordinary inputs populated by the caller.

## Storage and cost

Builders use pooled fixed-length row storage and hash-based deduplication;
canonical relations use shared rows and sorted row-ID vectors. Renaming shares
the corresponding row storage. Projection and joins reuse one scratch tuple per
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

Builders are movable but not copyable. Contextual builder views borrow the
builder and context; keep both alive and stationary while using the view.
Insertion preserves existing row spans, while `clear()` invalidates them.
Views observe the builder's current contents. Reinitializing or renaming a
builder invalidates borrowed schema spans. Returning a pooled builder ends
the permitted lifetime of its views, rows, and labels; cached results must be
interned in a repository or retain their builder. There is no internal
synchronization.

The CMake test targets are `database_operations`, `database_relation_repository`,
and `database_allocations`; run
them with `ctest --test-dir <build> -R '^database_' --output-on-failure`.
The allocation regression counts allocation and deallocation calls during
changing-state evaluations after warmup.

The conversion operations have separate responsibilities: `insert(repository,
builder)` interns compatible mutable input and returns `(view, inserted)`;
`assign(destination, source)` replaces ordered columns and rows, supports
self-assignment, and invalidates the destination's canonical index while keeping
its storage identity. It copies tuples in source column order, without projecting
or reordering their values. Matching arity retains available capacity; changing
arity replaces tuple storage. In state loops, acquire a pool entry with the source
arity to retain buffers. Treat borrowed destination rows and schema spans as
invalidated; an insertion failure may leave partial output.

`copy(source, repository)` remaps an interned relation into another repository and
returns `(view, inserted)`, preserving ordered columns and schema namespaces.
Mutable builders contain labels and rows, so assignment does not carry a schema
namespace. Inserting the assigned builder uses the explicit namespace argument
or its default of zero.

Python uses these same names and argument order. Both `insert` and `copy` return
`(view, inserted)` tuples, and the returned view retains its repository even after
the tuple is unpacked and released.
