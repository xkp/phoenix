# Known Requirements

This file collects requirements that are already known, even when the final
design or implementation choice is still undecided.

The goal is to preserve constraints and truths early, so later design decisions
can be made with the full context available.

## Usage

- Add facts that are already known to be true.
- Do not force implementation choices too early.
- If something is undecided, record the requirement and leave the design open.
- Keep entries short and concrete.

## Current Known Requirements

### Runtime Model

- The application is a command-line utility.
- It runs geometric-producing programs.
- A program is a function.
- A function has its own graph, inputs, and outputs.
- Results are taken from the connections into a special output node.
- Output ports are named.
- Output order does not matter.

### Instructions And Scheduling

- Instructions are organized as a graph.
- Regular instructions are intended to be independent and parallelizable when
  ready.
- An instruction can run when all promised incoming inputs have been fulfilled.
- A promised input is an input the instruction is expected to receive during the
  current invocation.
- An unconnected input is optional by absence of connection.
- An instruction with zero received inputs is not pending.
- A pending instruction has received some promised inputs but not all promised
  inputs.
- Instructions run at most once per function invocation.
- If the system reaches equilibrium, the runtime may force-run a pending
  instruction.
- The equilibrium preference is a pending instruction with no pending
  predecessors.
- If multiple such instructions exist, choose the smallest node id.
- If equilibrium is reached and every pending instruction has a pending
  predecessor, that is a graph validation error in version one.

### Data And Ports

- Ports are named and typed.
- Geometry inputs may accept multiple upstream geometry contributions.
- Multiple geometry contributions can be treated as a virtual mesh.
- Non-geometry ports are typed.
- Arrays should be supported as types.
- Version one should support at least basic literal values.

### Geometry And Numeric Constraints

- The implementation must be careful with CGAL exact number types.
- CGAL exact number types are useful, but they are very expensive in memory and
  CPU cost.
- The system should avoid unnecessary use, copying, or propagation of exact
  numeric representations.
- Numeric strategy should be designed with explicit awareness of precision
  versus performance tradeoffs.

### Missing / Empty / Default

- Missing means no value was dispatched on that port.
- Missing is not actual data.
- Empty means a real value was dispatched, but it represents empty geometry or
  an empty container.
- Default means the engine injected a configured literal value because no
  upstream value was connected or available.

### Control Flow And Errors

- Every instruction has an `else` output port in version one.
- Throwing routes control through `else`.
- `else` carries the instruction input context.
- If a throw is not handled, it propagates upward through the call stack.
- If no higher-level function handles the throw, the program ends in its current
  state.
- For now, handled-failure policies beyond explicit `else` handling can be
  deferred.
- The core version one rule is that an unhandled exception means the result of
  that run/configuration is failure.

### Parallelism And Determinism

- Multiple ready instructions may run in parallel.
- Deterministic final results matter more than deterministic execution order.
- Scheduling order must not change final observable results.
- Parallel execution should assume the existence of a shared execution context.
- The shared execution context should be immutable.
- Parallel instruction execution should not rely on mutable shared state.
- Any per-instruction execution state should be isolated from other concurrently
  running instructions unless explicitly modeled as immutable input data.
- Parallelism design should favor immutability first, to reduce synchronization
  complexity and protect reproducibility.

### Randomness And Reproducibility

- Each run is governed by a global seed.
- The same graph, inputs, configuration, instruction behavior, and global seed
  must produce the same final result.
- Seeds are used to initialize random number generators.
- The same effective seed must produce the same random number sequence on each
  run.
- Each instruction may have its own seed.
- Instruction-level randomness must be derived deterministically from stable
  identities plus the governing seed.
- Multiplex instructions support two modes:
  - one seed for all
  - one seed each
- In `one seed each`, per-item randomness must also be reproducible.

### Multiplexing

- There is a special input called `input`.
- `input` can multiplex work per input item.
- Multiplexing may run once per item or once for the whole input depending on
  instruction behavior.

### Actors And Hierarchy

- The top function contains the root of the actor hierarchy.
- Some functions are marked as actor-generating functions.
- An actor-generating function produces one actor per call.
- A multiplexed actor-generating function may produce multiple actors from one
  instruction call, one per multiplexed item.
- Each actor may contain its own child hierarchy.
- The top-level program produces exactly one root actor.
- Parent actor-generating functions create parent actors, and child
  actor-generating functions create child actors.
- Actor hierarchy construction is function-driven rather than assembled by
  generic graph instructions.
- Every function invocation belongs to exactly one actor context.
- Non-actor-generating functions belong to the current actor of their caller.
- Actor-generating functions create a new actor context for their own execution.
- Every actor has a pivot point.
- The pivot belongs to the actor, not the geometry, although it may be derived
  from the input geometry.
- Geometry is stored in actor-local space relative to the actor pivot.
- Every actor has exactly one transform.
- Every actor has zero or one geometry payload in version one.
- Materials are out of scope for version one.
- Every actor has zero or more child actors.
- While an actor-generating function is active, geometry produced by regular
  instructions is accumulated into the current actor.
- When an inner actor-generating function runs, it creates a child actor under
  the current actor.
- Geometry produced inside an inner actor-generating function belongs to that
  child actor.
- Non-actor-generating functions may still produce geometry.
- Geometry produced outside a child actor function still accumulates into the
  current actor geometry.
- Actor hierarchy is purely structural in version one.
- Actors may or may not have names.
- Every actor has an id.
- Actor generation must be deterministic in both final content and hierarchy
  order.
- Running the same program with the same seed and input twice must yield an
  identical geometric hierarchy.
- Instancing in version one means shared geometry/material identity with
  different transforms.
- Version one instances do not support per-instance overrides.
- The system should support generating an actor once and placing it multiple
  times as instances instead of rerunning the same actor-generating function for
  every repeated element.
- A generated actor may act as a reusable prototype/definition.
- Multiple instances may share the same underlying generated actor content.
- Instance placement is separate from actor generation.
- Each placed instance may have its own actor id.

### Version One Scope

- Version one is in-process only.
- Version one does not support feedback loops.
- A special loop instruction may be defined later.
- External process execution is postponed, but should remain architecturally
  possible.
- General POCO/object payloads are deferred.

### Partial Runs And Incremental Update

- The system must support partial runs.
- If a change only affects an inner actor or a parameter within that actor
  generation path, the system should be able to rerun only the affected
  actor-generating functions and then update the resulting scene.
- Full rerun should not be required for every edit.
- Partial rerun behavior must be considered early, before implementation
  begins, because it affects execution, identity, invalidation, and scene
  update semantics.
- Scene update must preserve the overall hierarchy structure except where the
  affected rerun changes that specific subtree.
- Version one partial runs must support at least:
  - parameter changes
  - input geometry changes
  - function body changes
  - graph wiring changes
  - seed changes
- Some changes may still require a full rerun, including changes such as the
  global seed or top-level input geometry affecting the entire program.
- The smallest rerun unit is one actor subtree.
- A single changed instruction may dirty additional instructions and functions,
  but the rerun is organized around the affected actor subtree.
- Canonical invalidation rule:
  - a change invalidates the directly affected instruction or actor subtree
  - invalidation cascades through all downstream dependent instructions
  - if an actor exposes outputs that feed ancestors or other parent-side work,
    invalidation continues through those dependent paths too
- When something changes inside an inner actor, the affected actor and its
  descendants are invalidated as needed by dependency propagation.
- If actor outputs affect parent-side work, invalidation must continue upward
  through those dependent paths.
- After partial rerun, version one updates the scene in place.
- Scene updates should make the minimum changes needed to reflect the new
  result.
- If only geometry changes and hierarchy does not, only the geometry payload
  should be replaced.
- Unaffected sibling actor ids must remain stable across partial reruns.
- Unaffected ancestor actor ids must remain stable across partial reruns.
- Rerun actors may receive new ids, although preserving ids when possible is
  preferred.
- If a rerun changes the number of generated child actors:
  - removed children disappear
  - added children get new ids
  - retained children should keep prior ids when they still represent the same
    logical actor, when possible
- If a parameter changes on an inner actor, invalidation affects that actor,
  dependent work, and any ancestor-side work affected through actor outputs.
- If a seed changes, rerun the affected instruction and propagate invalidation
  through all dirtied dependent work.
- If a function body changes, rerun that function and propagate invalidation
  through whatever it dirties.
- If input geometry changes, invalidate the affected instructions and then
  propagate through the dependency cascade.
- A partial rerun must produce the same final scene as a full rerun from
  scratch.
- If a partial rerun fails because an exception was not handled, the result of
  that configuration is failure.
- The system should support both:
  - automatically computed minimal rerun scope
  - user-selected rerun scope
- Initial workflows may be user-induced before full automatic scope computation
  exists.
- Caching is required for partial runs in version one.

## Open Requirement Follow-Ups

These are known-important questions that should be answered before the execution
architecture is finalized.

## Future Additions

Add new requirements here as they become known, even if the design decision is
still open.
