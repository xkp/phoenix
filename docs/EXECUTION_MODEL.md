# Execution Model

This document defines the version one execution model for the command-line
geometry program runtime.

It is based on the decisions captured in `DESIGN_QUESTIONS.md` and is intended
to be precise enough to guide implementation while still leaving some low-level
data structures open.

## 1. Scope

Version one includes:

- in-process execution only
- no feedback loops
- graph-based instruction scheduling
- parallel execution of independent ready instructions
- deterministic final results
- geometry-first runtime values
- literal support for basic non-geometry values
- function-local error propagation through `else`
- actor hierarchy generation
- actor-local geometry accumulation
- instancing through reusable actor prototypes
- cache-backed partial reruns organized around actor subtrees

Version one does not include:

- external process execution
- general POCO/object payloads
- loop semantics
- concrete mesh container standardization in this document

## 2. Core Concepts

### 2.1 Program And Function

A program is a function.

A function has:

- a graph
- named input ports
- named output ports
- instruction nodes
- edges between output ports and input ports

A function invocation executes exactly one graph instance for one set of inputs.

### 2.1.1 Function Invocation And Call Stack

Nested function calls push a new call frame.

Each call frame records:

- the invoked function id
- the stable call path for that invocation
- the caller node id, when the function was invoked by another instruction
- the current actor context, once actor generation is active

The call path is part of deterministic identity. It participates in seed
derivation and will later participate in actor ids, cache keys, diagnostics, and
partial-rerun scope.

When a nested function completes, its function outputs become the outputs of the
function-call instruction that invoked it. If the nested function fails, the
failure propagates to the caller; Phase 5 defines how `else` may handle that
failure.

### 2.2 Instructions

An instruction is a node in the graph.

Each instruction:

- has a stable node id
- has named input ports
- has named output ports
- always exposes an `else` output port in version one
- runs at most once per function invocation

Instruction lifecycle:

- idle: no received input values
- pending: at least one promised input has received a value, but not all
  promised inputs have been fulfilled
- ready: all promised inputs have been fulfilled
- executing: currently running
- completed: ran and will never run again in this function invocation

### 2.3 Promised Inputs

Readiness is based on promised inputs, not all declared inputs.

A promised input is an input port that is expected to receive a value during the
current function invocation. In version one, this means:

- an input port connected from an upstream output is promised
- an unconnected input port is not promised
- unconnected literal inputs may still have configured defaults

An instruction becomes ready when all promised inputs have been fulfilled.

## 3. Runtime Values

### 3.1 Value Families

Version one supports two main value families:

- geometry values
- literal values

Geometry is the primary runtime family in version one. Literal support exists
for parameters and return values such as integers, floating-point values,
booleans, strings, and arrays of literals.

### 3.2 Missing, Empty, And Default

These states are distinct.

- missing: no value was dispatched on a port
- empty: a value was dispatched, but it represents empty geometry or an empty
  container
- default: the engine injected a configured literal value because no upstream
  value was connected or available

Missing is not actual data. A missing value is an execution-state condition in
which nothing is dispatched through a port.

Empty is actual data. An empty geometry value is still a real dispatched value.

Default is actual data created by the engine from instruction configuration.

### 3.3 Geometry Representation

This document does not standardize the concrete in-memory mesh container for
version one.

However, the execution model assumes:

- geometry can be passed as a single runtime value
- multiple geometry inputs may be logically compounded into a virtual mesh
- multiplexing may also process geometry item-by-item
- geometry values may carry an actor accumulation owner once actor generation is
  active
- geometry values with different actor accumulation owners must not be merged as
  one payload

The exact container type is an implementation concern.

## 4. Ports And Typing

All instruction ports are named and typed.

Rules:

- each incoming edge targets one specific input port
- non-geometry ports are strongly typed
- arrays are valid types
- geometry inputs may accept multiple upstream geometry contributions that are
  treated as one virtual geometry input at execution time

That virtual geometry input is valid only when the contributions share one
actor accumulation owner, or when they are unowned and will naturally accumulate
on the current actor context. Contributions from different actor owners are not
implicitly mergeable.

Output node ports are named. Output order does not matter.

## 5. Scheduler

### 5.1 General Rule

Any instruction whose promised inputs are all fulfilled is ready to run.

Ready instructions may run in parallel.

If multiple upstream edges target the same input port, each edge is a promised
contribution. The port is not ready for execution until all promised
contributions for that port have arrived or the instruction is force-run at
equilibrium.

The preferred execution model is a dynamic ready frontier:

- the runtime keeps a queue or set of currently ready instructions
- when an instruction completes, its outputs are propagated
- propagation may make downstream instructions ready
- newly ready instructions may enter the frontier immediately

The runtime does not need a fixed wave or generation boundary between ready
sets. A batch-wave implementation is valid only as a conservative
implementation strategy, not as the semantic model.

The runtime does not require deterministic execution order. It requires
deterministic final results.

Therefore:

- instruction behavior must not depend on thread timing
- random behavior must be seed-driven
- scheduling order must not affect final observable outputs
- shared graph, actor, cache, and diagnostic state must be updated through
  canonical result publication rather than arbitrary worker completion order
- worker execution should produce instruction work results; the frontier owner
  or another centralized publisher commits graph state, actor deltas, cache
  writes, failures, and diagnostics

The first worker-backed implementation is intentionally constrained. It may run
multiple ready regular instruction handlers concurrently when a request opts
into more than one worker. Function calls, force-runs, actor-generating
instructions, and multiplex item parallelism remain on the serial path until
their publication contracts are tightened separately. Even in the threaded
path, workers only compute instruction work results; publication remains
centralized and canonical.

### 5.2 Pending Instructions

A pending instruction is one that:

- has received at least one promised input value
- has not yet received all promised input values

An instruction with zero received values is not pending.

### 5.3 Equilibrium

The system is in equilibrium when:

- no instruction is ready to run
- the function has not yet finished

At equilibrium, the runtime selects one pending instruction to force-run.

Selection rule:

1. Consider only pending instructions with no pending predecessors.
2. Select the instruction with the smallest node id.

If equilibrium is reached and every pending instruction has a pending
predecessor, this is a graph validation error in version one and execution must
stop.

### 5.4 Completion

A function invocation completes when both of the following are true:

- the system is stable, meaning no further instructions can run
- there are no pending instructions left to force-run

At that point, the function result is collected from the special output node.

## 6. Forced Execution

When an instruction is force-run at equilibrium:

- any missing promised literal input may be replaced by its configured default,
  if one exists
- otherwise missing inputs are treated as missing and may result in empty output
  behavior depending on instruction semantics

Version one assumes instructions can be written to tolerate this mode where
appropriate.

## 7. Output Semantics

### 7.1 Normal Instruction Outputs

Each output port may end up in one of these states:

- missing: no value was produced on that output port
- fulfilled with a concrete value
- fulfilled with an empty value

Conceptually, each output port has presence semantics, even if the final
implementation does not store it as an explicit boolean flag.

### 7.2 Function Output

Function results are taken from the connections into the special output node.

Rules:

- output node ports are named
- output order does not matter
- an unfulfilled output port returns an empty value
- the output node may expose partial state when the function ends

## 8. Error Handling And `else`

Every instruction exposes an `else` output port in version one.

Output ports and `else` are independent output channels. An instruction may emit
normal outputs and `else` outputs during the same execution.

If an instruction encounters one or more failures:

- it may emit normal outputs for successful work
- it may emit failed input contexts through `else`
- `else` receives the failed instruction input context as its value
- if the instruction multiplexes, `else` receives the failed item contexts
- if the instruction does not multiplex, the failure is instruction-scoped

This supports the pattern:

- do this operation
- for failed inputs or items, continue through `else`
- for successful inputs or items, continue through normal outputs

For multiplexing instructions, failure is item-scoped. One execution may produce
successful normal outputs for some items and `else` outputs for other failed
items.

When multiple geometry values are delivered to the same downstream input during
one function invocation, they are accumulated as a geometry collection. This
allows repeated item-level normal outputs or repeated item-level `else` outputs
to remain visible to the receiving instruction instead of overwriting each
other.

Instructions may be marked critical.

If a critical instruction produces a failure and no usable `else` path handles
that failure, the current function fails and the failure propagates upward
through the call stack.

If a non-critical instruction produces a failure and no usable `else` path
handles that failure, the runtime logs the failure and execution continues.

## 9. Multiplexing

Some instructions may multiplex over the special `input` value.

Conceptually:

```cpp
if (instruction.multiplexes) {
    for (auto item : input) {
        instruction.run(item);
    }
} else {
    instruction.run(input);
}
```

The scheduler treats the instruction as one node, but the instruction may
internally apply its logic once per item.

A multiplexed instruction is atomic from the graph scheduler's point of view.
Downstream instructions do not become ready from partial item results. The
instruction completes only after all item attempts have completed or the
instruction has entered its final failure/cancellation state.

Multiplex item work may run in parallel inside the instruction. Its externally
visible results are committed only after the item results are merged into a
canonical order. Normal outputs, `else` outputs, failures, actor children, and
cache entries must not depend on item completion order.

For child function calls, each multiplex item produces an item result slot. The
slot can contain child outputs, failures, actor child deltas, and instancing
prototype updates. The parent instruction merges those slots in canonical item
order before graph publication.

When multiplex threading and instancing are both enabled, the runtime should
classify item candidates before dispatching heavy work. Each candidate computes
the fingerprint needed to decide whether it is a prototype item or can reuse an
existing prototype. Items that can be instanced must not be sent to worker
execution for the expensive graph work. Instead, they report an instance result
through the same per-item payload path used by regular work results, and the
canonical merge publishes those payloads in item order.

The first worker-backed multiplex implementation runs child invocation item
slots concurrently in bounded batches. To keep the initial path safe, cache
reads/writes and trace sinks remain excluded until those shared services have
explicit thread-safety contracts. Instancing is supported by classifying item
fingerprints before dispatch and sending only prototype work to workers.

In the current worker model, cache stores/writers are main-thread-only services.
A request that enables cache reads or writes must stay on the serial instruction
execution path until cache implementations declare their own thread-safety
contract. Trace sinks are also centralized services: instruction start and
publication records are emitted by the frontier owner, and multiplex item
threading is disabled when trace sinks are attached.

Regular handler instructions may still run on workers while instruction and
publication trace sinks are attached, because those trace records are emitted
before dispatch and during centralized publication. Multiplex child item
threading remains disabled with trace sinks attached because nested item work
can emit its own scope, instruction, or publication records.

Version one does not fully standardize what counts as an item beyond the general
idea of per-face or per-element geometry processing.

## 10. Reproducibility And Randomness

### 10.1 Global Reproducibility Rule

A function run is governed by a global seed.

Given:

- the same graph
- the same inputs
- the same instruction implementations
- the same configuration
- the same global seed

the final results must be identical.

### 10.2 Derived Instruction Seeds

Instruction randomness must be derived deterministically from the global seed.

The runtime should not allow thread timing or execution order to affect random
results.

Seeds are intended to initialize random number generators. This means the
runtime must guarantee that the same effective seed produces the same random
number sequence on each run, assuming the same generator algorithm and the same
instruction behavior.

A suitable model is:

- derive an instruction seed from the global seed and stable instruction identity
- use that derived seed whenever the instruction needs randomness

Conceptually:

```text
instruction_seed = derive(global_seed, function_call_path, instruction_id)
```

The exact derivation function is an implementation concern, but it must be
stable and deterministic.

The same requirement applies to RNG construction:

- the RNG algorithm used by an instruction must be fixed for reproducibility
- the RNG must be initialized from the derived seed in a deterministic way
- instruction code must consume random values deterministically for identical
  inputs and configuration

### 10.3 Multiplex Seed Modes

Multiplexing instructions support two seed modes:

- one seed for all
- one seed each

In `one seed for all` mode:

- every multiplexed item uses the same derived instruction seed

In `one seed each` mode:

- each multiplexed item gets a deterministic per-item derived seed

Conceptually:

```text
item_seed = derive(instruction_seed, item_key)
```

The `item_key` must be stable for reproducibility. Version one should prefer a
stable item index or another stable item identity derived from the input value.

This ensures that multiplexed execution can still recreate the same per-item
random streams on every run.

The execution frame exposes the effective instruction seed and a per-item seed
derivation helper. Instruction implementations should ask the frame for item
seeds rather than deriving them ad hoc.

### 10.4 Local Seeds

An instruction may also have its own local seed configuration.

When present, that local seed must still participate in deterministic seed
derivation rather than introducing nondeterministic runtime randomness.

A suitable model is:

```text
effective_instruction_seed =
    derive(global_seed, function_call_path, instruction_id, local_seed)
```

## 11. Actor Hierarchy

### 11.1 Root Actor

The top-level program produces exactly one root actor.

The root actor is the structural parent for all geometry and child actors
created during the top-function invocation.

Every function invocation belongs to exactly one actor context.

The top-level function belongs to the root actor. A non-actor-generating nested
function belongs to the current actor of its caller. An actor-generating nested
function creates a new child actor and belongs to that new actor for the duration
of its execution.

Actor ids are derived deterministically from function call paths in the current
runtime slice. The top-level actor uses the root call path, and child
actor-generating function calls derive child actor ids from their caller node and
callee function id.

### 11.2 Actor-Generating Functions

Some functions are marked as actor-generating functions.

When an actor-generating function runs, it creates one actor for that function
call. If the function call is multiplexed, it may create one actor per
multiplexed item.

Child actors produced by a multiplexed call are committed in canonical item
order. This applies whether the child actor came from a fresh function run,
instancing, or a cache hit.

The baseline semantics are equivalent to running the actor-generating function
once per item. Instancing may later optimize equivalent item runs, but it must
preserve the same observable actor hierarchy.

Actor hierarchy construction is function-driven:

- a parent actor-generating function creates the parent actor
- an inner actor-generating function creates a child actor under the current
  actor
- graph instructions do not generically assemble the hierarchy

### 11.3 Geometry Accumulation

While an actor-generating function is active, geometry produced by regular
instructions accumulates into the current actor.

When an inner actor-generating function runs, geometry produced inside that
function belongs to the child actor created by that function.

Generated geometry that leaves an actor-generating function keeps that child
actor as its accumulation owner. If later instructions in the caller graph
operate on that actor-owned geometry, the resulting geometry still accumulates
on the owning child actor, not on the caller's current actor. This prevents
inner actor geometry from merging into parent actor geometry just because
control has returned to the caller graph.

Said another way: an actor's geometry can be extended by downstream operations
acting on that actor's function outputs.

Geometry with no existing actor owner accumulates into the current actor
context naturally. Combining geometry from different actor owners is invalid
unless a later explicit ownership-changing operation defines otherwise.

The concrete runtime representation for actor-local geometry accumulation is
deferred until the geometry payload model is settled.

### 11.4 Actor Contents

In version one, an actor has:

- an id
- zero or one name
- exactly one transform
- one pivot
- zero or one geometry payload
- zero or more child actors

Materials are out of scope for version one.

The pivot belongs to the actor. Geometry is stored in actor-local space relative
to the actor pivot.

### 11.5 Actor Identity

Actor generation must be deterministic in final content and hierarchy order.

Running the same program with the same seed and input twice must produce an
identical geometric hierarchy.

Actor ids must remain stable for unaffected ancestors and siblings across
partial reruns. Rerun actors may receive new ids, although preserving ids for
retained logical actors is preferred when possible.

## 12. Instancing

Version one instancing means shared generated actor content with distinct
placements.

Instancing is an optimization. It may skip rerunning an actor-generating graph
only when the effective generation inputs are known to be equivalent, including
topology-relevant geometry identity once geometry keys are available.

The initial runtime slice uses explicit instance keys only. A multiplexed
actor-generating call may opt into instancing, and when multiple items provide
the same explicit key, the first item is generated normally and later matching
items become separate actors that reference the same prototype. If no explicit
key exists, no instancing occurs.

Rules:

- an actor may be generated once as a reusable prototype
- multiple instances may share the same generated actor content
- each placed instance has its own actor id
- each placed instance has its own transform
- version one instances do not support per-instance overrides
- instance placement is separate from actor generation

Instancing must preserve deterministic hierarchy order.

## 13. Partial Reruns And Scene Updates

### 13.1 Rerun Unit

The smallest rerun unit is one actor subtree.

A single changed instruction may dirty additional instructions and functions,
but rerun execution is organized around the affected actor subtree.

Initial workflows may be user-induced before full automatic minimal-scope
calculation exists. The architecture should support both user-selected and
automatically computed rerun scope.

### 13.2 Invalidation Rule

The canonical invalidation rule is:

- a change invalidates the directly affected instruction or actor subtree
- invalidation cascades through all downstream dependent instructions
- if an actor exposes outputs that feed ancestors or other parent-side work,
  invalidation continues through those dependent paths too

The initial invalidation planner operates within one function graph. Given
changed instruction ids, it marks those instructions and all downstream
instructions dirty, reports whether function outputs are affected, and reports
whether the actor subtree or parent-side work may need rerun.

Invalidation results also carry stable reason codes so later diagnostics can
explain whether a result is dirty because instructions changed, function outputs
are affected, actor subtree work is affected, or parent-side propagation is
required.

Partial rerun planning combines the invalidation result with cache identity. The
planner can report cache keys and cache-hit availability for dirty instructions,
the enclosing function call, and the affected actor subtree before any executor
or scene mutation occurs.

Version one partial reruns must support parameter changes, input geometry
changes, function body changes, graph wiring changes, and seed changes. Some
changes, such as a global seed change or top-level input geometry change, may
still require a full rerun.

### 13.3 Scene Update Rule

After a partial rerun, the scene is updated in place.

The runtime should make the minimum changes needed to reflect the new result:

- if only geometry changes and hierarchy does not, replace only the geometry
  payload
- if child actors are removed, they disappear
- if child actors are added, they receive new ids
- retained children should keep prior ids when they still represent the same
  logical actor, when possible

A partial rerun must produce the same final scene as a full rerun from scratch.

The first scene update implementation supports structural subtree replacement
by actor id. Replacing a subtree preserves unaffected ancestors and siblings,
including their ids and sibling order. Replacing the root actor is also valid
when the rerun scope is the whole scene root. Geometry-only patching remains
deferred until the concrete actor geometry payload model is settled.

Partial rerun application first supports the cache-backed case. If a partial
rerun plan reports an actor-subtree cache hit, the applier rechecks that cache
entry at apply time and replaces the matching scene subtree through the scene
updater. If no usable cached subtree exists, the applier reports that rerun work
is required and leaves the scene unchanged.

The first executor-backed partial rerun path may rerun a supplied full function
execution request for the affected actor scope. If execution completes and
returns an actor, that actor is applied as the replacement subtree. If execution
fails, the prior scene remains unchanged in this slice. Minimal dirty-node
subgraph execution remains a later refinement.

Rerun scope resolution starts from known scope data. Given a partial rerun plan,
the affected function descriptor, function inputs/defaults, call path, and
target actor id, the resolver builds the full `FunctionExecutionRequest` needed
by the executor-backed rerun path.

The scope index records executed function scopes and their actor ownership. It
can find a scope by actor id, by exact call path, or by the nearest
actor-owning scope for a dirty call path. Non-actor nested functions resolve to
their nearest actor-owning ancestor, because the rerun unit remains an actor
subtree.

The executor can optionally populate a scope trace sink while it runs. Scope
records include the invoked function, call path, actor id, parent scope index,
inputs, input defaults, and global seed. This produces a scope index for the
completed run without making partial rerun code part of the executor itself.

Execution tracing is level-controlled:

- `none`: no trace records
- `scope`: function/actor scope records
- `instruction`: compact instruction execution records
- `item`: reserved for multiplex item records without payload copies
- `value`: reserved for selected value/input/output diagnostics

Compact instruction records include function id, call path, node id,
instruction kind, and actor id. Publication records may also be emitted at
commit time; they include compact counts for produced outputs, failures, actor
child deltas, and whether the instruction result came from cache. They
intentionally do not include inputs, outputs, failures, item payloads, or
geometry payloads. This keeps ordinary partial-rerun discovery practical for
large graphs and multiplex-heavy runs.

Given a dirty call path, scope discovery queries the scope index for the nearest
actor-owning scope and produces the scope-resolution request needed for a
rerun. If a cached actor subtree is already available, discovery can report that
no executor scope resolution is required.

Dirty instruction discovery starts from a changed function/node pair and uses
the compact instruction index to find every executed call path for that
instruction. Each unique call path is resolved through scope discovery, so one
changed instruction can produce multiple actor rerun scopes when the same
function was invoked multiple times or through multiplexing. Partial discovery
is valid: resolvable call paths can produce rerun requests while unresolved
paths remain available for diagnostics.

If invalidation reports that parent-side propagation is required, scope
discovery promotes the affected child actor scope to the nearest parent actor
scope using recorded scope ancestry. This keeps parent-side dependent work from
being left stale after an inner actor change. If no parent actor exists, the
original discovered scope remains the rerun scope.

### 13.4 Failure During Partial Rerun

If a partial rerun fails because an exception is not handled, the result of that
configuration is failure.

For version one, the affected subtree is replaced with whatever state exists at
failure time.

## 14. Caching

Caching is required for partial runs in version one.

Cache keys must include all inputs that can affect deterministic results,
including:

- parameters
- geometry inputs
- seeds
- function body identity
- graph wiring identity
- relevant function call path or actor subtree identity

Cache reuse must not break determinism and must not preserve stale actor
hierarchy data after invalidation.

Version one should define caches at least for:

- function calls
- actor subtrees
- instruction outputs

Cache identity is separated from cache storage. Cache keys include the reusable
work kind, function identity, call path, graph/body revision, input fingerprint,
seed identity, and kind-specific fields such as instruction node id, actor id,
or explicit instance key.

The first cache identity builder derives deterministic strings from function
graph shape and runtime inputs. Geometry input fingerprints include the current
debug geometry identity and actor accumulation owner. This is sufficient for the
current lightweight geometry wrapper; topology-aware geometry identity remains a
later replacement once the concrete geometry model is settled.

The first cache store is an in-memory correctness implementation. Production
storage for heavy geometry payloads remains an implementation concern and should
avoid unnecessary deep copies.

The in-memory cache store supports explicit removal of individual entries and
identity-scoped clearing across all cache families. Identity-scoped clearing is
the first invalidation bridge: when a function/call/input/seed identity is known
dirty, cached instruction outputs, function call results, actor subtrees, and
actor prototypes for that identity can be removed before rerun work is planned
or applied.

The executor can optionally publish cache entries as it runs. Successful
instruction executions publish instruction-output entries. Completed function
invocations publish function-call entries and actor-subtree entries. Cache reads
can also be enabled during execution: function-call cache hits skip the whole
function invocation, and instruction-output cache hits skip the matching
instruction while continuing downstream graph execution.

Cache reuse is valid only when the derived identity matches the uncached work:
function graph revision, call path, input fingerprint, seed identity, and
kind-specific actor or instruction fields must agree. The current correctness
tests compare cached execution against full execution for observable outputs and
actor hierarchy shape. Deeper geometry equivalence remains tied to the later
topology-aware geometry identity work.

## 15. Validation Rules

A version one graph must satisfy at least the following:

- no feedback loops
- all connected ports are type-compatible
- all node ids are stable and unique within the function
- all required execution paths are structurally valid
- actor-function constraints are valid
- `else` port connections target instructions that expose `else`
- equilibrium cycles where every pending node depends on another pending node are
  rejected as invalid

Additional validation may be added by implementation.

## 16. Version One Implementation Notes

The following are intentionally left open for implementation:

- concrete mesh and geometry collection types
- literal storage layout
- exact internal representation of missing versus empty versus present states
- exact seed derivation algorithm
- exact virtual mesh materialization strategy
- exact actor id derivation strategy
- exact cache key encoding

These are not open behavioral questions. They are implementation choices that
must preserve the semantics defined in this document.
