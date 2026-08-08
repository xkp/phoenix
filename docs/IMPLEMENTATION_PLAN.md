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

## Phase 5: Implement Error And `else` Flow

Goal:

- support version one failure semantics

Tasks:

- implement instruction failure signaling
- implement `else` output routing
- propagate unhandled failures through nested function calls
- encode the rule that unhandled failure means the result of that run is
  failure

Deliverables:

- failure propagation behavior
- error-routing tests

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

## Phase 7: Implement Actor Generation

Goal:

- support actor-producing functions and hierarchy assembly

Tasks:

- implement root actor creation at top-function execution
- implement actor-generating function execution
- implement child actor creation from nested actor functions
- implement actor-local geometry accumulation
- implement actor transform and pivot storage
- implement actor id generation strategy consistent with current requirements
- implement actor prototype versus instance distinction

Deliverables:

- actor hierarchy runtime
- actor assembly tests

## Phase 8: Implement Instancing

Goal:

- support reusable actor prototypes and instance placement

Tasks:

- implement actor prototype creation from one actor-generating execution
- implement instance placement with distinct transforms
- allow multiple placed instances to share the same generated actor content
- keep instance placement separate from actor generation
- ensure deterministic hierarchy order under instancing

Deliverables:

- instance placement mechanism
- repeated-structure tests such as building/window scenarios

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

- Phases 1 through 4
- Result: a deterministic single-run graph executor without actors or partial
  reruns

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

Before writing code, the next best step is to review this plan against the
current requirements and decide whether the first implementation slice should
start at:

- Phase 1 directly
- or a short doc pass to tighten `EXECUTION_MODEL.md` against newer actor and
  partial-run requirements
