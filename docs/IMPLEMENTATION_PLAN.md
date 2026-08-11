# Implementation Plan

This document sequences the implementation work for the geometry runtime based
on the current known requirements.

It is intentionally phased. The goal is to reduce risk by building the runtime
around the most constraining requirements first:

- graph execution
- determinism and seed handling
- actor hierarchy generation
- partial reruns
- caching
- performance constraints around CGAL exact numbers

## Principles

- Preserve deterministic final results.
- Prefer immutable shared context and isolated execution state.
- Build for partial reruns from the beginning.
- Keep actor hierarchy and scene update semantics explicit.
- Avoid premature commitment to concrete CGAL-heavy data layouts.

## Phase 0: Stabilize The Specification

Goal:

- confirm that the current docs are sufficient to begin implementation

Tasks:

- review `KNOWN_REQUIREMENTS.md` for missing requirement-level gaps
- review `EXECUTION_MODEL.md` against the latest requirements
- add any missing notes about actor prototypes, subtree replacement, and cache
  invalidation
- freeze a version one scope boundary before writing core runtime code

Deliverables:

- updated requirements docs
- updated execution model doc
- explicit list of deferred features

Current status:

- `KNOWN_REQUIREMENTS.md` includes actor hierarchy, instancing, partial rerun,
  invalidation, failure, and caching requirements.
- `EXECUTION_MODEL.md` now includes version one actor hierarchy, instancing,
  partial rerun, scene update, and cache semantics.
- The remaining low-level choices are implementation details, not behavioral
  requirement gaps.

## Phase 1: Define Core Runtime Interfaces

Goal:

- define the runtime-facing C++ types and boundaries without committing yet to
  all storage details

Tasks:

- define graph, node, port, edge, and function descriptors
- define runtime execution state enums and lifecycle
- define actor model interfaces
- define seed and RNG ownership interfaces
- define value-envelope abstractions for geometry, literal, missing, empty, and
  default states
- define cache and invalidation interfaces at the boundary level

Deliverables:

- header skeletons for core runtime types
- interface notes for execution, actor creation, and caching

Current status:

- initial header skeletons exist for graph descriptors, runtime values,
  execution state, seed derivation, actor nodes, and cache entries
- graph validation and runtime value tests already exist

Remaining Phase 1 work:

- align the existing headers with the full execution model
- add missing interfaces for instruction execution, function invocation,
  output collection, actor assembly, invalidation records, and cache key
  construction
- decide which interfaces are abstract boundaries and which are concrete value
  types for slice one

## Phase 2: Build The Static Graph Model

Goal:

- load and validate function graphs before execution

Tasks:

- implement node and port definitions
- implement edge wiring
- implement graph validation
- validate:
  - unique stable node ids
  - type-compatible port connections
  - no feedback loops in version one
  - actor-function constraints
  - `else` port availability
  - equilibrium-invalid dependency cycles

Deliverables:

- graph model
- validation pipeline
- validation diagnostics

## Phase 3: Implement Runtime Value Infrastructure

Goal:

- create the minimum runtime value system needed for execution

Tasks:

- implement literal value support:
  - integer
  - floating point
  - boolean
  - string
  - arrays of literals
- implement geometry value wrapper abstractions
- model missing versus empty versus default explicitly
- implement port fulfillment state
- define how virtual geometry aggregation is represented at runtime

Deliverables:

- runtime value types
- port-state model
- geometry wrapper abstraction

Current status:

- completed for the first deterministic executor slice
- runtime values distinguish missing, empty, present, and defaulted states
- literal values, geometry wrappers, port fulfillment, and virtual geometry
  aggregation scaffolding are implemented
- geometry values now carry optional actor accumulation ownership
- virtual geometry aggregation reports cross-actor owner conflicts instead of
  treating incompatible contributions as one payload

## Phase 4: Implement Deterministic Execution Core

Goal:

- execute acyclic graphs deterministically in terms of final results

Tasks:

- implement instruction state tracking:
  - idle
  - pending
  - ready
  - executing
  - completed
- implement promised-input tracking
- implement ready-queue scheduling
- implement equilibrium detection
- implement equilibrium forced-run selection:
  - no pending predecessors
  - smallest node id
- implement output propagation
- implement function output collection through the output node

Deliverables:

- single-run execution engine
- deterministic result test cases

Current status:

- implementation started with a single-threaded deterministic executor
- instruction handler registration, ready-node execution, output propagation,
  equilibrium force-running, seed derivation, and output-node collection are in
  place
- parallel execution remains intentionally deferred to Phase 11

## Phase 4.5: Implement Function Invocation And Call Stack

Goal:

- support nested function calls before adding failure routing, actor generation,
  partial reruns, and cache identity

Tasks:

- define a function library for resolving nested function calls
- define call frames and call-stack ownership
- push a new frame for each nested function invocation
- derive stable child call paths from caller path, caller node id, and callee id
- preserve the actor-context hook on call frames for Phase 7
- return child function outputs as the function-call instruction outputs
- propagate child execution failure status to the caller

Deliverables:

- function invocation runtime
- call stack tests

Current status:

- implementation started with `FunctionLibrary`, `CallFrame`, `CallStack`, and
  `InstructionDescriptor::called_function_id`
- nested function-call execution, child call-path derivation, and call-stack
  frame propagation are in place

## Phase 5: Implement Error And `else` Flow

Goal:

- support version one failure semantics

Tasks:

- implement instruction failure signaling with support for multiple failure
  records per instruction execution
- implement item-scoped failure records for multiplexing instructions
- implement `else` output routing for failed instruction or item contexts
- allow normal output ports and `else` to emit during the same instruction
  execution
- implement critical-instruction failure semantics
- propagate critical unhandled failures through nested function calls
- encode the rule that non-critical unhandled failures are logged and execution
  continues

Deliverables:

- failure propagation behavior
- mixed success/failure multiplex behavior
- error-routing tests

Current status:

- completed for the deterministic executor slice
- structured instruction failure records support multiple failure records from
  one instruction execution
- item-scoped multiplex failures preserve stable item keys
- normal outputs and `else` outputs can both be emitted from one instruction
  execution
- repeated geometry emissions to the same downstream input are accumulated as a
  geometry collection
- handled failures route failed context through `else`
- non-critical unhandled failures are logged and execution continues
- critical unhandled failures fail the function while preserving already emitted
  outputs
- nested function failures can be handled by the parent call node's `else`

## Phase 6: Implement Seed And RNG Semantics

Goal:

- guarantee reproducible random behavior across runs

Tasks:

- implement global-seed entry point for a run
- implement deterministic seed derivation for instructions
- implement local-seed participation in derivation
- implement multiplex seed modes:
  - one seed for all
  - one seed each
- ensure instruction RNG construction is stable and deterministic
- validate that scheduling order does not change final random-driven outputs

Deliverables:

- seed derivation utilities
- RNG integration rules
- reproducibility tests

Current status:

- implementation started with deterministic instruction seed derivation
- instruction seed derivation includes global seed, function call path, node id,
  and local seed
- instruction descriptors can select multiplex seed mode:
  - one seed for all
  - one seed each
- execution frames expose per-item seed derivation for multiplex handlers
- per-item seed derivation includes the item key only in `one seed each` mode

Remaining Phase 6 work:

- define the concrete RNG engine construction policy used by instruction
  implementations
- add direct RNG sequence reproducibility tests once handlers start consuming RNG

## Phase 7: Implement Actor Generation

Goal:

- support actor-producing functions and hierarchy assembly

Tasks:

- implement root actor creation at top-function execution
- implement the rule that every function invocation has exactly one actor
  context
- implement actor-generating function execution
- implement child actor creation from nested actor functions
- implement actor-local geometry accumulation
- implement actor transform and pivot storage
- implement actor id generation strategy consistent with current requirements
- implement actor prototype versus instance distinction

Deliverables:

- actor hierarchy runtime
- actor assembly tests

Current status:

- implementation started with actor context on every function invocation
- top-level execution creates a deterministic root actor
- non-actor nested functions inherit the caller's current actor
- actor-generating nested functions create deterministic child actors
- multiplexed actor-generating calls create one deterministic child actor per
  input item
- failed multiplex items route through the call node's `else` without committing
  a child actor for that failed item

Remaining Phase 7 work:

- implement concrete transform and pivot behavior
- define actor-local geometry payload and accumulation rules after geometry
  representation is settled
- connect owned geometry values to the eventual concrete actor-local geometry
  payload representation
- define explicit ownership-changing operations, if any are needed
- define actor naming policy
- refine actor id policy for later partial-rerun retention
- decide how actor outputs interact with parent-side graph dependencies

## Phase 8: Implement Instancing

Goal:

- support reusable actor prototypes and instance placement
- treat instancing as an optimization over the baseline multiplex actor
  generation semantics

Tasks:

- implement actor prototype creation from one actor-generating execution
- implement instance placement with distinct transforms
- allow multiple placed instances to share the same generated actor content
- only share a prototype when the effective generation inputs are known to be
  geometrically/topologically equivalent
- keep instance placement separate from actor generation
- ensure deterministic hierarchy order under instancing

Deliverables:

- instance placement mechanism
- repeated-structure tests such as building/window scenarios

Current status:

- implementation started with an explicit-key instancing slice
- actor-generating call instructions can opt into instancing
- multiplexed actor calls may split an `instance_key` literal array alongside
  the special `input`
- matching explicit `instance_key` values reuse the first generated prototype
  reference and skip rerunning the child graph
- actor hierarchy still contains one child actor per input item
- items without an explicit key are never instanced

Remaining Phase 8 work:

- replace the temporary local prototype table with cache-backed prototype
  storage
- define topology-aware geometry identity inputs for prototype keys
- implement transform/placement data for instances
- define how prototype contents are stored and inspected
- prove instanced execution remains equivalent to full per-item execution

## Phase 9: Implement Partial Runs And Invalidation

Goal:

- support subtree-level reruns and in-place scene updates

Tasks:

- implement dirty marking for changed instructions and actor subtrees
- implement invalidation cascading across dependent work
- implement rerun scoping around one actor subtree
- implement in-place subtree update
- implement geometry-only patching when hierarchy is unchanged
- preserve unaffected ancestor and sibling actor ids
- propagate invalidation upward when actor outputs affect parent-side work

Deliverables:

- partial rerun engine
- scene patch/update logic
- invalidation tests

Current status:

- implementation started with invalidation scaffolding
- `InvalidationPlanner` expands changed instruction ids through downstream
  graph dependencies
- invalidation results report dirty instruction ids, function-output impact,
  actor-subtree impact, and parent-propagation need
- invalidation results include stable reason codes for diagnostics
- multiple changed instruction ids merge into one dirty set
- leaf changes that do not reach function outputs do not require parent
  propagation
- invalid changed instruction ids are ignored
- `PartialRerunPlanner` combines invalidation scope with cache identity and
  cache-hit information
- partial rerun plans report dirty instruction cache keys, function-call cache
  availability, and actor-subtree cache availability
- `SceneUpdater` can replace an actor subtree in place by actor id
- scene subtree replacement preserves unaffected ancestor and sibling ids and
  ordering
- root actor replacement is supported for full-root rerun/update cases
- `PartialRerunApplier` can apply a cached actor subtree to a scene through
  `SceneUpdater`
- partial rerun application distinguishes cache-miss rerun requirements,
  missing planned cache entries, invalid requests, and scene update failures
- cache-miss application can optionally run a supplied `FunctionExecutor`
  request and apply the returned actor subtree to the scene
- the first executor-backed rerun path reruns a full function scope, not a
  minimal dirty-instruction subgraph
- `PartialRerunScopeResolver` can turn a known dirty actor/function scope into
  a `FunctionExecutionRequest`
- scope resolution binds the execution request to the target actor id and call
  path so the rerun result can replace the intended scene subtree
- `PartialRerunScopeIndex` records known executed function scopes with function
  id, call path, actor id, parent scope, inputs, defaults, and seed
- scope-index lookup can find scopes by actor id, exact call path, and nearest
  actor-owning scope for a dirty call path
- non-actor nested functions resolve to the nearest owning actor scope rather
  than becoming independent rerun subtrees
- `FunctionExecutor` can optionally populate a scope trace sink as function
  invocations run
- executor trace population records root scopes, nested actor scopes, non-actor
  helper scopes, and multiplexed actor item scopes
- execution tracing now has explicit levels:
  - `none`
  - `scope`
  - `instruction`
  - `item`
  - `value`
- `FunctionExecutor` emits scope records only at `scope` level and above
- `FunctionExecutor` emits compact instruction records only at `instruction`
  level and above
- `PartialRerunInstructionIndex` stores one compact record per instruction
  execution and supports indexed lookup by function/node and call-path/node
- `PartialRerunScopeDiscovery` connects a dirty call path to the nearest
  actor-owning scope in the scope index
- discovered scopes can be materialized as `PartialRerunScopeRequest` values
  for the existing resolver/executor-rerun path
- `PartialRerunDirtyInstructionDiscovery` connects a dirty function/node pair to
  the compact instruction trace index and then discovers every affected
  actor-owning rerun scope
- dirty instruction discovery handles multiple executed call paths for the same
  function/node, dedupes repeated traces by call path, and preserves partial
  discovery results for diagnostics
- scope discovery can promote a dirty child actor scope to the nearest parent
  actor scope when invalidation requires parent propagation
- parent-actor scope lookup is based on recorded scope ancestry rather than
  parsing call-path strings

Remaining Phase 9 work:

- surface changed instruction identity from editor/diagnostics into dirty
  instruction discovery
- define and implement `item` and `value` trace payload policies
- define geometry-only scene patch operations once geometry payloads settle
- define minimal dirty-instruction/subgraph execution within a rerun scope

## Phase 10: Implement Caching

Goal:

- satisfy the requirement that partial runs depend on caching

Tasks:

- define cache keys for:
  - function calls
  - actor subtrees
  - instruction outputs
- incorporate all relevant invalidation inputs:
  - parameters
  - geometry inputs
  - seeds
  - function body identity
  - graph wiring identity
- implement cache storage and lookup
- ensure cached reuse does not break determinism
- ensure cache reuse does not accidentally preserve stale actor hierarchy data

Deliverables:

- cache subsystem
- invalidation-aware cache policy
- cache correctness tests

Current status:

- implementation started with deterministic cache identity scaffolding
- `CacheKeyBuilder` creates keys for instruction outputs, function calls, actor
  subtrees, and actor prototypes
- keys include function identity, call path, graph revision, input fingerprint,
  seed identity, and kind-specific fields
- actor prototype keys include explicit instance keys
- `MemoryCacheStore` provides in-memory put/find behavior for instruction
  outputs, function calls, actor subtrees, and actor prototypes
- cache families are stored separately to avoid cross-kind collisions
- partial rerun planning can query cache availability without executing a rerun
- `MemoryCacheStore` supports explicit eviction for individual cache entries
  and identity-scoped clearing across all cache families
- `CacheIdentityBuilder` derives deterministic graph revision and input
  fingerprint strings from function graph shape, call path, inputs, and seed
- input fingerprints include geometry debug identity and actor accumulation
  ownership, so actor-owned geometry does not collide with similarly labeled
  geometry owned by another actor
- `PartialRerunPlanner` can derive cache identity from the rerun request when a
  prebuilt `CacheIdentity` is not supplied
- `FunctionExecutor` can optionally publish cache entries through `CacheWriter`
  while running
- successful instruction executions publish instruction-output cache entries
  keyed by derived invocation identity, node id, and effective seed
- completed function invocations publish function-call cache entries and actor
  subtree cache entries
- `FunctionExecutor` can optionally read cache entries through `CacheStore`
- function-call cache hits can skip an entire function invocation
- instruction-output cache hits can skip individual instruction execution while
  continuing downstream graph execution
- execution cache-hit tests compare cached reuse against uncached full execution
  for observable outputs and actor hierarchy shape
- execution ignores stale function-call cache entries when identity inputs such
  as seed identity differ

Remaining Phase 10 work:

- define production storage for heavy geometry payloads
- replace v1 debug-label geometry fingerprints with topology-aware geometry
  identity
- extend cache/full-rerun equivalence validation once concrete geometry payloads
  and topology-aware identity are available

## Phase 11: Introduce Parallel Execution

Goal:

- add concurrency on top of the deterministic runtime model

Tasks:

- introduce worker-task execution for ready instructions
- keep the shared run context immutable
- isolate per-instruction execution state
- centralize result publication and graph-state updates
- verify that scheduling order does not affect final results
- verify compatibility with caching and partial reruns

Deliverables:

- concurrent scheduler
- concurrency safety tests
- reproducibility-under-parallelism tests

## Phase 12: Address CGAL Performance Strategy

Goal:

- keep the runtime practical under CGAL cost constraints

Tasks:

- audit where exact-number types are truly required
- minimize copying of CGAL-heavy geometry values
- prefer lightweight wrappers and references where safe
- separate algorithmic correctness needs from blanket exactness
- measure memory and CPU impact of key geometry flows

Deliverables:

- numeric strategy notes
- performance baselines
- targeted optimization backlog

## Phase 13: Tooling, Diagnostics, And Developer Workflow

Goal:

- make the runtime observable and usable during iteration

Tasks:

- add structured execution diagnostics
- add graph validation error reporting
- add partial-rerun tracing
- add seed and cache debug visibility
- add actor hierarchy inspection helpers

Deliverables:

- developer diagnostics
- runtime trace tools

## Suggested Initial Delivery Slices

### Slice 1

- finish Phase 1 interface alignment
- complete Phase 2 graph model and validation
- complete Phase 3 runtime value infrastructure
- complete Phase 4 deterministic single-run executor
- Result: a deterministic single-run graph executor without actor generation,
  partial reruns, caching, or parallel execution

### Slice 2

- Phases 5 through 7
- Result: actor-generating functions with failure routing and reproducible RNG

### Slice 3

- Phases 8 through 10
- Result: instancing, partial reruns, and cache-backed subtree updates

### Slice 4

- Phases 11 through 13
- Result: parallel execution, performance tuning, and developer tooling

## Risks To Watch Early

- value representation becoming too CGAL-heavy too early
- partial rerun invalidation rules becoming inconsistent with actor hierarchy
- cache keys missing function-body or seed identity
- instance identity and actor id behavior drifting across reruns
- parallel execution introducing hidden nondeterminism
- `else` and failure propagation interacting badly with subtree replacement

## Immediate Next Step

Start implementation by finishing the Phase 1 interface alignment against the
current headers.

Recommended first code task:

- audit `include/phoenix/*.hpp` against `EXECUTION_MODEL.md`
- add the missing slice-one execution boundaries
- keep actor, cache, and partial-rerun types skeletal but compatible with their
  later requirements
- then implement the deterministic single-run execution engine behind those
  interfaces
