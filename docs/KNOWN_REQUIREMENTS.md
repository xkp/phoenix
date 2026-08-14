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

### Labels

- Labels are fundamental geometry semantics, not optional display metadata.
- A top-level run owns one canonical label registry shared by every function
  invocation, actor, worker, cache operation, and partial rerun in that run.
- Persisted string label UIDs map one-to-one to compact run-local integer
  `LabelId` values.
- Ordinary registry labels use non-negative integer ids.
- A label id remains stable and valid for the entire run.
- Label definitions are immutable after registration.
- One UID resolves to exactly one semantic definition in one run.
- Identical definitions with the same UID are deduplicated.
- Differing definitions with the same UID are fatal before execution and should
  normally be caught as migration/linking errors.
- Functions may expose local label symbols and visibility, but geometry-carried
  label ids remain globally valid when geometry crosses function boundaries.
- A label that is unknown to a receiving function is absent from that
  function's local symbol table; its registry definition remains valid.
- Rename operations may replace labels unknown to the current function with a
  known label without repairing or changing registry identity.
- Version one eagerly discovers labels in all reachable functions, resolves
  label UIDs during linking, and freezes the registry before workers begin.
- Version one does not create labels during instruction execution.
- Label allocation must not depend on worker scheduling.
- The label registry must provide a semantic fingerprint for cache identity.
- Label provenance should retain enough project/function/source information to
  diagnose conflicting definitions.
- Initial reserved label values include:
  - `-1`: unassigned, absent, or default label
  - `-1000`: unbounded geometry
  - `-1001`: layout geometry
- Other negative values remain reserved. Kernel-private positional label values
  are not global registry labels unless a kernel boundary explicitly requires
  them.

### Geometry And Numeric Constraints

- Phoenix runtime geometry is a canonical immutable 3D representation.
- Runtime coordinates initially use `double`.
- Geometry that was formerly represented in two dimensions is embedded on the
  `y = 0` plane by mapping coordinates to `(x, 0, z)`.
- Version one runtime topology must represent vertices, directed halfedges and
  opposites, faces, orientation, disconnected components, and non-manifold
  relationships.
- Holes in faces are deferred and must produce structured failure when they
  cannot be represented safely.
- Faces and directed halfedges carry `LabelId` values.
- Opposite halfedges may carry different labels.
- Runtime geometry values are immutable after instruction publication.
- Memory/backing-store ownership, actor accumulation ownership, prototype
  sharing, and subgeometry/selection references are distinct concepts.
- Vertex, edge/halfedge, and face ids are globally unique over one run.
- New element ids are assigned during deterministic canonical publication, not
  in worker-completion order.
- Workers may return invalid/unassigned ids for newly created elements.
- Unchanged elements should retain ids through conversion when identity is
  proven; split, merged, and newly created elements receive new ids.
- Persistent element references must remain valid through selection, function
  calls, cache replay, conversion, and face-consumption publication.
- Geometry fingerprints include coordinates, topology, orientation, element
  identity as required by semantics, face labels, and directed-halfedge labels.
- Exact or otherwise kernel-specific CGAL working geometry exists only inside
  an instruction worker or equivalent isolated kernel invocation.
- Runtime geometry is promoted to the working representation required by a
  kernel and demoted back to canonical runtime geometry afterward.
- Working geometry and CGAL handles must not escape the worker boundary.
- Runtime geometry, rather than temporary working geometry, is the canonical
  serialized and cached representation.
- The runtime representation supports non-manifold topology, but each kernel
  declares the topology it accepts. A mismatch is an item-scoped failure that
  may route through `else`.
- Conversion performs mandatory validation for non-finite coordinates, broken
  element references, invalid orientation representation, and unrepresentable
  topology.
- Geometry-producing instructions run a versioned repair stage after producing
  runtime geometry.
- Initial repair may merge near vertices using a configurable absolute
  tolerance and remove zero-length edges and zero-area faces.
- The initial numeric tolerance is derived from production extrusion fixtures.
- Labeled degenerate topology may be removed with diagnostics rather than
  failing solely because it carried labels.
- Conversion and repair policy versions participate in cache identity.
- Exact number types must be used only where the selected kernel requires them;
  they must not be propagated throughout graph execution merely because a
  kernel uses them internally.
- Execution payloads, traces, cache identities, and invalidation records must
  not copy heavy geometry merely for bookkeeping.
- Cache storage should prefer immutable shared payloads or ownership-aware
  blobs over repeated deep copies.
- Threaded geometry work should return owned results and compact publication
  metadata rather than duplicating full payloads unnecessarily.

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
- The initial production-kernel port does not need to reproduce the legacy
  solver's exact random sequence for the same numeric seed.
- Initial production-comparison fixtures should avoid randomized operations.
- Legacy RNG sequence compatibility may be investigated after the initial port.

### Multiplexing

- There is a special input called `input`.
- `input` can multiplex work per input item.
- Multiplexing may run once per item or once for the whole input depending on
  instruction behavior.
- Major geometry kernels such as extrusion, partition, and inset multiplex once
  per input face unless their accepted kernel contract explicitly says
  otherwise.
- Inset follows the minimal-change production-port discipline for its source
  audit and compatibility oracle. Its intended Phoenix implementation reuses
  the extrusion kernel because the algorithms are equivalent, but reuse is
  accepted only after differential fixtures verify topology, labels, precision,
  failure, and consumption parity. Non-equivalent behavior remains behind a
  narrow inset-specific adapter rather than being discarded.

### Geometry Publication And Face Consumption

- Some instruction types consume input faces by replacing them with generated
  geometry for final actor/scene assembly.
- Consuming versus non-consuming behavior is static instruction-type metadata.
- Non-consuming instructions such as selection may return stable references to
  original faces without removing them from final geometry.
- Input geometry remains immutable. Consumption is explicit instruction-result
  metadata rather than in-place deletion.
- A multiplexed consuming instruction decides consumption per input item.
- A successfully processed item is consumed.
- A failed, skipped, or `else`-routed item is not consumed.
- A successful consuming item remains consumed even if a defective
  implementation unexpectedly emits no replacement; that condition should
  produce a diagnostic.
- Multiple branches may consume the same source face. The original is removed
  once and every successfully produced replacement remains.
- Other graph branches may continue reading the immutable original value even
  after a consuming result is computed.
- Consumption follows the source geometry's actor owner across nested function
  and actor execution boundaries.
- Generated geometry, assigned element ids, failures, and consumption effects
  commit through canonical publication order rather than worker completion
  order.
- Actor assembly must retain a publication ledger of geometry contributions and
  consumption effects by execution/rerun scope.
- Final actor geometry is derived from immutable contributions after applying
  successful consumption records, preventing overlap between originals and
  their replacements.

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
- Actor-local geometry uses the canonical immutable runtime representation and
  is assembled from published contributions and consumption effects.
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
- Version one does not support arbitrary graph feedback cycles.
- Production-compatible bounded iteration is required through a special loop
  runtime construct. It must not make the general instruction graph cyclic.
- The loop construct must support a deterministic fixed/ranged iteration count,
  early termination when the body emits no `loop` value, feedback of that value
  into the next iteration, accumulation from body `all`/`output`, a stable
  zero-based `$index`, deterministic per-iteration seeds, and a hard execution
  budget. Expression-updated loop variables depend on the scripting contract.
- Every behavior-affecting instruction option captured outside runtime input
  ports must contribute a stable `configuration_revision` to the instruction's
  graph/cache identity. Loop count, range, step, safety budgets, and body
  function identity are required parts of that revision.
- External process execution is postponed, but should remain architecturally
  possible.
- General POCO/object payloads are deferred.
- Materials remain deferred during the initial geometry-kernel port.
- Required initial build targets are:
  - Windows x64 with a currently supported MSVC toolchain
  - Linux x64 with GCC
  - macOS Apple Silicon with AppleClang
- Linux Clang and Intel macOS are initially best effort rather than release
  blockers.
- C++17 and vcpkg-managed CGAL form the initial porting baseline.

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
- Concrete geometry cache identity must also include the label-registry
  fingerprint, kernel/adapter version, and conversion/repair-policy version.
- Cache input identity must include geometry actor ownership so the same
  geometry label owned by different actors does not collide.
- The cache must support invalidating dirty deterministic identities without
  inspecting or copying heavy geometry payloads.
- Cache storage must account for heavy geometry payloads and should not assume
  cheap deep copies in the production implementation.
- Cached consuming instruction results must store generated canonical runtime
  geometry and consumed source-face identities.
- Cache replay must reproduce geometry contribution and consumption effects,
  not only output-port values.
- Temporary kernel working geometry is not cached in the initial port.
- Full cache invalidation is acceptable initially for graph, label, kernel,
  adapter, or conversion-policy changes.
- Parameter, input, and seed changes must still invalidate dependent identities
  so parameter-driven partial reruns remain supported.
- Before applying a partial rerun result, remove the prior rerun scope's
  geometry contributions and consumption effects, then apply the new result.
- If a new result no longer consumes a previously consumed face, actor assembly
  must be able to restore that original face from unaffected immutable input
  contributions.

## Open Requirement Follow-Ups

These are known-important questions that should be answered before the execution
architecture is finalized.

## Future Additions

Add new requirements here as they become known, even if the design decision is
still open.

### Scripted geometry

- The production-compatible `script` instruction must inspect, create, and
  modify canonical 3D geometry, including face and directed-edge labels; it is
  distinct from scalar expression evaluation.
- Script geometry edits are invocation-local and transactional. Canonical input
  geometry is immutable, and no edit is published or consumed until the entire
  script succeeds and every output validates.
- Script failure, cancellation, budget exhaustion, invalid topology, or invalid
  labels discards all pending outputs and preserves source geometry.
- Production 2D script geometry is not ported.
- Script mesh booleans are post-port work and do not gate completion of the
  production corpus port; their legacy label-transfer semantics require
  differential validation before adoption.
- Script includes are immutable resolved assets, not filesystem access.

### Scalar expressions

- Expressions reuse the sandboxed engine but expose only immutable scalar
  globals and instruction-local scalar bindings; geometry and script host APIs
  are unavailable.
- Expression source, language version, engine identity/version, configured
  globals, limits, and deterministic seed participate in evaluation identity.
- Expression diagnostics and budget/cancellation failures prevent the
  consuming instruction from publishing output.
- `$index` is a zero-based typed loop-body binding. Loop update expressions use
  `_index` for the next iteration and read an immutable snapshot of parent and
  previous-iteration variables; all updates commit atomically or none do.
- Loop variable names, initial values, update expression contracts, parent
  bindings, and engine identity participate in instruction cache identity.
- `case` expressions evaluate in persisted order and route through only the
  first truthy branch; `else` is used only when no branch matches.
- Geometry-derived expression bindings evaluate per canonical face or directed
  halfedge and route stable-ID selections backed by the original geometry.
  Missing selection IDs fail rather than being silently discarded.
- `choice` routes its input unchanged to an explicitly configured output or a
  deterministic seeded alternative. Item order and all published-choice
  metadata participate in configuration identity.
- `lod` routes its input unchanged to the requested low/normal/high output, or
  walks downward to the nearest connected level. LOD participates in execution
  and cache identity; no upward fallback occurs.
