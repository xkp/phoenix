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

The exact container type is an implementation concern.

## 4. Ports And Typing

All instruction ports are named and typed.

Rules:

- each incoming edge targets one specific input port
- non-geometry ports are strongly typed
- arrays are valid types
- geometry inputs may accept multiple upstream geometry contributions that are
  treated as one virtual geometry input at execution time

Output node ports are named. Output order does not matter.

## 5. Scheduler

### 5.1 General Rule

Any instruction whose promised inputs are all fulfilled is ready to run.

Ready instructions may run in parallel.

The runtime does not require deterministic execution order. It requires
deterministic final results.

Therefore:

- instruction behavior must not depend on thread timing
- random behavior must be seed-driven
- scheduling order must not affect final observable outputs

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

Geometry produced outside the child actor function continues to accumulate into
the current parent actor.

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
