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
- If multiple upstream edges target the same input port, each edge is a
  promised contribution and the instruction should wait for all such
  contributions before becoming ready.
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
- Geometry contributions with different actor ownership must not be merged as
  one geometry payload.
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
- CGAL-specific runtime implementation is deferred until concrete geometry
  payload types exist; until then, CGAL work should remain strategy and
  requirement documentation.
- Execution payloads, traces, cache identities, and invalidation records should
  not copy future heavy geometry payloads merely for bookkeeping.
- Future cache storage for concrete geometry should prefer immutable handles,
  shared blobs, or other ownership-aware storage over repeated value copies.
- Future threaded geometry work should return compact metadata and owned deltas
  rather than duplicating full geometry payloads between workers.

### Missing / Empty / Default

- Missing means no value was dispatched on that port.
- Missing is not actual data.
- Empty means a real value was dispatched, but it represents empty geometry or
  an empty container.
- Default means the engine injected a configured literal value because no
  upstream value was connected or available.

### Control Flow And Errors

- Every instruction has an `else` output port in version one.
- Output ports and `else` are independent output channels.
- An instruction may emit normal outputs and `else` outputs during the same
  execution.
- Throwing or item-level failure routes failed input context through `else`
  when a usable `else` route exists.
- `else` carries the failed instruction input context.
- For multiplexing instructions, `else` carries the failed item contexts so
  downstream instructions can provide fallback behavior per failed item.
- A multiplexing instruction may generate multiple failures during one
  execution while still producing successful normal outputs for other items.
- Repeated item-level geometry emissions to the same downstream input are
  accumulated rather than overwritten.
- Multiplexing failures are item-scoped; non-multiplexing failures are
  instruction-scoped.
- Instructions may be marked critical.
- A critical instruction failure that is not handled by `else` fails the current
  function and propagates upward through the call stack.
- A non-critical instruction failure that is not handled by `else` is logged and
  execution continues.
- For now, handled-failure policies beyond explicit `else` handling can be
  deferred.
- The core version one rule is that only critical unhandled failures make the
  result of that run/configuration a failure.

### Parallelism And Determinism

- Multiple ready instructions may run in parallel.
- Parallel graph scheduling should use a dynamic ready frontier rather than a
  required fixed wave boundary.
- When an instruction completes, propagation may immediately make downstream
  instructions candidates for execution.
- Deterministic final results matter more than deterministic execution order.
- Scheduling order must not change final observable results.
- Parallel execution should assume the existence of a shared execution context.
- The shared execution context should be immutable.
- Parallel instruction execution should not rely on mutable shared state.
- Any per-instruction execution state should be isolated from other concurrently
  running instructions unless explicitly modeled as immutable input data.
- Shared graph state, actor state, cache writes, and diagnostics should be
  updated through canonical result publication rather than worker completion
  order.
- The first worker-backed execution path may be constrained to ready regular
  instruction handlers, leaving function calls, force-runs, actor-generating
  instructions, and multiplex item parallelism on the serial path.
- A multiplexed instruction remains one atomic graph node from the scheduler's
  point of view.
- Multiplex item work may run in parallel internally, but downstream graph work
  waits until all item attempts have completed or the instruction reaches its
  final failure/cancellation state.
- Multiplex normal outputs, `else` outputs, failures, actor children, and cache
  entries must commit in canonical item order.
- Multiplex child function calls should collect per-item result slots and merge
  those slots canonically before parent instruction publication.
- When multiplex threading and instancing are both enabled, item fingerprints
  must be computed before dispatching heavy work so items that can reuse an
  instance are not sent to worker execution.
- Instanced multiplex items should report reused-instance payloads through the
  same per-item result slot path as regular worker results.
- Initial multiplex item threading may use bounded batches and exclude shared
  services such as cache stores and trace sinks until their thread-safety
  contracts are explicit.
- Cache stores and cache writers are main-thread-only in the current worker
  model; cache-backed requests should fall back to serial instruction execution
  until cache thread-safety is explicitly defined.
- Trace sinks are centralized services in the current worker model; multiplex
  item threading should be disabled when trace sinks are attached.
- Regular handler threading may still run with trace sinks attached when trace
  records are emitted before worker dispatch and during centralized
  publication.
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
- Instruction implementations should receive seed values from the runtime
  rather than deriving ad hoc random seeds internally.

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
- Multiplexed actor generation is the baseline behavior that instancing later
  optimizes.
- Each actor may contain its own child hierarchy.
- The top-level program produces exactly one root actor.
- Parent actor-generating functions create parent actors, and child
  actor-generating functions create child actors.
- Actor hierarchy construction is function-driven rather than assembled by
  generic graph instructions.
- Every function invocation belongs to exactly one actor context.
- Non-actor-generating functions belong to the current actor of their caller.
- Actor-generating functions create a new actor context for their own execution.
- Actor ids are deterministic for identical graph structure and call paths.
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
- Generated geometry that leaves an actor-generating function keeps that actor
  as its accumulation owner.
- An actor's geometry can be extended by downstream operations acting on that
  actor's function outputs.
- Operations on actor-owned geometry continue accumulating results on that
  geometry's owning actor, even when the operation runs after returning to the
  caller graph.
- Non-actor-generating functions may still produce geometry.
- Geometry that has no existing actor owner accumulates into the current actor
  context naturally.
- Combining geometry from different actor owners is invalid unless a later
  explicit ownership-changing operation defines otherwise.
- The concrete actor-local geometry payload representation is deferred until
  the geometry model is settled.
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
- Instancing can only reuse generation for effective inputs that are known to be
  geometrically/topologically equivalent.
- Until topology-aware geometry identity is available, instancing must require
  explicit equivalence keys.
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
- The first invalidation slice can operate inside one function graph before
  cache-backed reruns exist.
- Invalidation should be explainable with stable reason codes for diagnostics.
- Partial rerun planning should be able to determine dirty work and cache
  availability before mutating execution or scene state.
- When something changes inside an inner actor, the affected actor and its
  descendants are invalidated as needed by dependency propagation.
- If actor outputs affect parent-side work, invalidation must continue upward
  through those dependent paths.
- After partial rerun, version one updates the scene in place.
- Scene updates should make the minimum changes needed to reflect the new
  result.
- Structural actor subtree replacement can be implemented before geometry-only
  patching, because it only depends on actor identity and hierarchy.
- Cache-backed partial rerun application should recheck cache availability at
  apply time before mutating the scene.
- Executor-backed partial rerun application may start by rerunning a full
  function scope for the affected actor before minimal dirty-node rerun exists.
- Rerun scope resolution may start from known function, call path, input, and
  actor id data before fully automatic dirty-scope discovery exists.
- Partial rerun scope lookup must account for non-actor nested functions by
  resolving them to their nearest actor-owning scope.
- Function execution should be able to emit scope trace records so partial rerun
  discovery can use actual executed function/actor ownership.
- Execution tracing must be level-controlled so large graphs and multiplexed
  operations do not always pay for detailed item/value diagnostics.
- Compact instruction tracing should record instruction identity and ownership
  without copying inputs, outputs, failures, items, or geometry payloads.
- Compact publication tracing should record commit-time counts for outputs,
  failures, actor deltas, and cache hits without copying heavy payloads.
- Execution diagnostics should be opt-in and separate from tracing.
- Compact diagnostics should record timing, execution mode, force-run state,
  worker count, cache-hit state, output/failure counts, actor delta counts, and
  multiplex item/prototype/reused-instance counts without copying geometry or
  full value payloads.
- Dirty call paths should resolve through the scope index to the nearest
  actor-owning scope before an executor rerun request is built.
- Dirty instruction discovery must account for one function/node being executed
  at multiple call paths and may produce multiple actor rerun scopes.
- Dirty instruction discovery should preserve partial success: resolvable call
  paths can produce rerun scopes while unresolved paths remain available for
  diagnostics.
- When invalidation requires parent propagation, dirty scope discovery should
  promote an affected child actor scope to the nearest parent actor scope so
  parent-side dependent work can be recomputed.
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
- Cache identity must include function/call identity, graph/body identity,
  input identity, and seed identity before cached work can be reused.
- Cache input identity must include geometry actor ownership so the same
  geometry label owned by different actors does not collide.
- The cache must support invalidating dirty deterministic identities without
  inspecting or copying heavy geometry payloads.
- Cache storage must account for heavy geometry payloads and should not assume
  cheap deep copies in the production implementation.

## Open Requirement Follow-Ups

These are known-important questions that should be answered before the execution
architecture is finalized.

## Future Additions

Add new requirements here as they become known, even if the design decision is
still open.
