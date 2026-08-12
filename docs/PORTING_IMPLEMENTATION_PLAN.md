# Production Solver Porting Implementation Plan

This document sequences the migration of mature geometry functionality from
`threedee.solver` into Phoenix. It complements `IMPLEMENTATION_PLAN.md`: the
existing plan describes the general runtime, while this plan governs concrete
geometry, labels, preserved kernels, face-consumption publication, and legacy
project compatibility.

The accepted decisions and their rationale are recorded in
`PORTING_QUESTIONS.md`. If this plan and that document disagree, the accepted
decisions in `PORTING_QUESTIONS.md` take precedence until the discrepancy is
resolved explicitly.

## Outcome

The first complete porting milestone is a deterministic Phoenix program that:

- loads or constructs labeled polygonal input
- represents that input as immutable Phoenix runtime geometry
- multiplexes extrusion once per input face
- promotes each item to the working geometry expected by the preserved
  production extrusion kernel
- demotes and validates the generated result
- preserves face and directed-halfedge labels
- assigns globally unique run-local element IDs during canonical publication
- publishes generated geometry and consumed source-face identities
- assembles final actor geometry without overlapping consumed originals
- routes item failures through `else`
- produces results comparable with production golden fixtures

This milestone deliberately excludes materials and randomized
production-compatibility fixtures.

## Governing Principles

- Preserve the internal algorithms of extrusion, partition, and inset.
- Reimplement legacy command and VM integration around Phoenix interfaces.
- Keep label definitions immutable and run-owned.
- Store only stable non-negative `LabelId` values for ordinary labels; preserve
  named negative sentinel semantics at kernel boundaries.
- Move immutable `double` runtime geometry through the graph.
- Keep instruction-specific CGAL working geometry inside worker execution.
- Preserve topology, orientation, face labels, and directed-halfedge labels
  across every conversion.
- Treat input-face consumption as explicit result metadata, never in-place
  mutation.
- Assign new geometry element IDs during deterministic canonical publication.
- Cache canonical runtime geometry, not temporary working geometry.
- Prefer broad, safe cache invalidation during the port over unsafe reuse.
- Compare non-random production behavior first; Phoenix RNG must remain
  deterministic but need not reproduce legacy sequences initially.
- Document production defects without changing trusted algorithms during the
  compatibility port.

## Scope

### Initial scope

- eager loading/linking of all reachable functions and labels
- immutable run-scoped label registry
- function-local label visibility tables
- one canonical 3D runtime geometry model
- former 2D coordinates mapped to `(x, 0, z)`
- `double` runtime coordinates
- non-manifold-capable runtime topology
- kernel-specific topology acceptance
- exact/inexact working-geometry adapters
- versioned validation and repair policy
- extrusion as the first preserved kernel
- explicit face-consumption publication
- actor-local geometry assembly
- cache and partial-rerun integration after uncached extrusion works
- Windows x64/MSVC, Linux x64/GCC, and macOS Apple Silicon/AppleClang

### Deferred scope

- material assembly and material-output compatibility
- exact legacy RNG sequence compatibility
- lazy function and label loading
- runtime `float` storage
- exact working-set caching
- adjacent exact-kernel execution islands
- full incremental cache retention across graph or label changes
- holes in faces
- changes to trusted kernel algorithms
- Intel macOS as a release requirement
- Linux Clang as a required toolchain

## Definition Of Compatibility

For the initial non-random fixtures, production and Phoenix results must have:

- equivalent connected components and topology
- matching orientation
- identical face and directed-halfedge labels
- coordinates within the accepted, versioned comparison tolerance
- equivalent face-consumption/final-publication effects

Production element ordering, Phoenix element ordering, and raw element ID values
may differ unless downstream behavior observes them. Phoenix IDs must still be
unique over one run and deterministic under Phoenix publication rules.

## Workstream Dependencies

```text
source audit + production fixture harness
                 |
                 v
       label registry and linker
                 |
                 v
 immutable runtime geometry + element identity
                 |
                 v
 promotion/demotion + validation/repair
                 |
                 v
 face-consumption publication and actor assembly
                 |
                 v
        preserved extrusion kernel
                 |
                 v
 cache/partial rerun + platform hardening
                 |
                 v
        partition, then inset
```

The phases below may overlap only where this dependency chain remains intact.

## Phase P0: Synchronize The Specification

Goal:

- promote accepted porting decisions into the authoritative requirements and
  execution model before concrete geometry interfaces are frozen

Tasks:

- update `KNOWN_REQUIREMENTS.md` with:
  - immutable run-scoped labels
  - function-local label visibility
  - global run-local geometry element IDs
  - immutable `double` runtime geometry
  - `(x, 0, z)` former-2D mapping
  - kernel-local working geometry
  - non-manifold runtime support with kernel-specific rejection
  - consuming versus non-consuming instruction behavior
- update `EXECUTION_MODEL.md` with:
  - label linking and registry freeze point
  - runtime geometry ownership and element references
  - instruction result consumption records
  - canonical ID assignment and result publication
  - actor geometry contribution/consumption ledger
  - cache identity additions
- reconcile `IMPLEMENTATION_PLAN.md` Phase 3, Phase 7, Phase 9, Phase 10, and
  Phase 12 with this active porting track
- keep `PORTING_QUESTIONS.md` as the review history rather than duplicating all
  rationale in the authoritative documents

Deliverables:

- updated requirements
- updated execution model
- reconciled main implementation plan

Exit gate:

- no authoritative document contradicts the accepted porting decisions

Current status:

- completed on 2026-08-11
- accepted label, runtime geometry, working geometry, element identity,
  face-consumption, cache, validation, and platform contracts are now normative
  in `KNOWN_REQUIREMENTS.md` and `EXECUTION_MODEL.md`
- affected placeholder/deferred phases in `IMPLEMENTATION_PLAN.md` now point to
  the active production port track

## Phase P1: Audit The First Kernel And Build A Production Oracle

Status: complete. See `PORTING_EXTRUSION_AUDIT.md`. The captured production
oracle is fingerprinted there; live regeneration awaits an available
production backend executable and is required before P6 compatibility signoff.

Goal:

- identify the exact trusted source boundary for extrusion and establish a
  repeatable production comparison path before changing or copying the kernel

Tasks:

- trace the complete production extrusion path:
  - loader parameters
  - command defaults and multiplex behavior
  - label and profile resolution
  - working-geometry preparation
  - kernel entry and result structures
  - cleanup and conversion helpers
  - generated IDs
  - subdivision/face-removal notifications
- classify every dependency as:
  - trusted kernel code
  - shared kernel-support code
  - legacy adapter behavior to reimplement
  - unrelated legacy VM behavior
- inventory global/static state, ID generation, debug facilities, and
  thread-safety hazards
- verify CGAL and Boost API differences under Phoenix's vcpkg baseline
- define a production fixture runner or reproducible manual oracle command
- capture at least one smallest-valid non-random extrusion result before kernel
  adaptation begins
- record legacy defects and unsafe behavior without fixing algorithmic behavior

Deliverables:

- extrusion source-boundary inventory
- dependency and thread-safety notes
- production oracle procedure
- first captured production result
- post-port defect backlog

Exit gate:

- every source file required for the first extrusion path has an explicit
  disposition
- production can generate a stable expected result for the smallest fixture

## Phase P2: Implement Immutable Label Infrastructure

Status: complete. `include/phoenix/labels.hpp` and `src/labels.cpp` provide the
strong IDs, immutable definitions, deterministic frozen registry, provenance,
function-local tables, eager reachable-function linker, diagnostics, and
semantic fingerprint. Focused and full regression suites pass.

Goal:

- make every geometry label stable, immutable, and unambiguous for an entire
  run

Tasks:

- introduce a strong `LabelId` integer type or equivalently constrained value
  type accepted by preserved kernel adapters
- implement immutable `LabelDefinition` values
- implement one run-owned `LabelRegistry` with:
  - persistent UID to `LabelId` lookup
  - `LabelId` to immutable definition lookup
  - identical-definition deduplication
  - conflicting-definition diagnostics
  - deterministic non-negative allocation
- retain label origin/provenance for diagnostics
- implement function-local label symbol/visibility tables
- eagerly discover labels in every reachable function
- resolve persisted UIDs during linking
- freeze the registry before instruction workers begin
- make conflicting definitions for one UID fatal before execution
- define named sentinel constants initially covering:
  - unassigned/default: `-1`
  - unbounded: `-1000`
  - layout: `-1001`
- keep `hard_edges` positional values `-2` through `-7` private to that
  algorithm unless the extrusion audit proves they cross its boundary
- implement a deterministic registry semantic fingerprint for cache identity

Tests:

- identical UID and identical definition deduplicates
- identical UID and differing definition fails linking
- one UID always maps to one integer
- function-local visibility does not invalidate a geometry-carried label
- unknown-label rename can distinguish labels absent from a function table
- registry allocation is unchanged by worker scheduling
- registry is immutable after freeze

Deliverables:

- label types and registry
- eager label linker
- label diagnostics
- registry fingerprint

Exit gate:

- no executable graph contains unresolved label UIDs
- no instruction can mutate a registered definition

## Phase P3: Implement Canonical Runtime Geometry

Status: complete. Phoenix now owns an immutable, contiguous indexed halfedge
representation in `include/phoenix/geometry.hpp`. It preserves independent
directed-halfedge labels, reciprocal opposites, radial non-manifold incidence,
stable face references, strong run-wide element IDs, deterministic
serialization/fingerprinting, and shared backing-store ownership. Its array
layout is explicitly designed for count reservation and direct index-to-CGAL-
handle mapping without geometric-equality reconstruction.

Goal:

- replace placeholder geometry values with an efficient immutable topology that
  does not carry exact CGAL numbers through the graph

Tasks:

- define one canonical 3D runtime payload using `double` coordinates
- support:
  - vertices
  - directed halfedges and opposites
  - faces
  - orientation
  - disconnected components
  - non-manifold relationships
  - face labels
  - distinct labels on opposite halfedges
- reject holes initially with structured diagnostics
- map former-2D coordinates to `(x, 0, z)`
- separate:
  - immutable payload/backing-store ownership
  - actor accumulation ownership
  - prototype sharing
  - subgeometry/selection references
- define run-wide unique vertex, halfedge/edge, and face ID types
- define invalid/unassigned IDs returned by workers for new elements
- define stable element references used by selections and consumption records
- define deterministic canonical serialization
- define a topology-aware geometry fingerprint including labels and orientation
- preserve geometry values as immutable after instruction publication
- update virtual geometry aggregation without copying heavy payloads

Tests:

- labeled polygon round-trip through runtime serialization
- opposite halfedges retain distinct labels
- disconnected and non-manifold payloads can be represented
- holes produce the selected version-one failure
- published geometry cannot be mutated through shared values
- fingerprints change when coordinates, topology, orientation, or labels change
- element IDs remain unique over one run

Deliverables:

- runtime geometry data model
- immutable storage and reference model
- serialization and fingerprinting
- geometry value integration

Exit gate:

- graph execution, actor ownership, selection references, and cache identity can
  use concrete runtime geometry without CGAL handles

## Phase P4: Build Working-Geometry Adapters And Repair Policy

Status: complete. `include/phoenix/working_geometry.hpp` defines the versioned
adapter/repair contract, a CGAL `Surface_mesh` working boundary with explicit
source-index, ID, face-label, and directed-halfedge-label property maps, and a
direct extrusion-face preparation path. Post-kernel repair merges near
vertices, removes zero-length edges and zero-area faces, diagnoses labeled
removal, and invalidates IDs when identity changes. The initial absolute
tolerance is `1e-6`, selected one order below the approximately five-decimal
coordinate resolution visible in the captured production extrusion oracle;
P6 production comparisons must confirm or revise it as a versioned policy.

Goal:

- provide controlled conversion between canonical runtime geometry and the
  instruction-specific CGAL structures expected by trusted kernels

Tasks:

- define a kernel-adapter contract with:
  - accepted topology declaration
  - runtime-to-working promotion
  - working result invocation boundary
  - working-to-runtime demotion
  - validation and repair policy version
  - label and source-element mapping
- implement mandatory validation for:
  - non-finite coordinates
  - broken element references
  - invalid orientation representation
  - unrepresentable topology
- implement post-producing-instruction repair outside trusted kernels:
  - merge near vertices using a configurable absolute tolerance
  - remove zero-length edges
  - remove zero-area faces
  - allow removal of labeled degenerate topology with diagnostics
- derive the initial repair tolerance from production extrusion fixtures
- preserve unchanged source element IDs where the kernel and mapping prove
  identity
- return invalid IDs for new/split/merged elements for later publication
- preserve every face and directed-halfedge label through conversion
- treat a kernel topology mismatch as item-scoped failure eligible for `else`
- version conversion and repair policy for cache identity
- keep all working geometry inside worker lifetime

Tests:

- runtime-to-working-to-runtime labeled topology round-trip
- nearly coincident input under the selected tolerance
- zero-length and zero-area repair
- non-finite rejection
- non-manifold runtime input rejected only by adapters whose kernels require it
- no CGAL handle survives adapter destruction

Deliverables:

- generic adapter boundary
- extrusion working-geometry adapter foundation
- validation and repair pipeline
- versioned conversion policy

Exit gate:

- the smallest fixture can be converted to and from the extrusion working shape
  without running the kernel and without losing topology or labels

## Phase P5: Implement Geometry Publication And Face Consumption

Status: complete. Instructions now declare `consumes_geometry` statically and
may return ordered `GeometryItemEffect` records containing generated immutable
geometry, consumed owner/face identities, and item success. A thread-safe,
rerun-scope-keyed publication ledger commits effects in canonical scope/item
order, assigns globally unique IDs to unassigned or colliding elements,
removes successful consumed originals once, retains all successful branch
replacements, preserves failed/select inputs, propagates consumption by source
actor identity across nested calls, and restores originals when a scope is
replaced. Consuming instructions bypass the legacy output-only instruction
cache until P7 makes cache entries consumption-aware.

Goal:

- make generated geometry and replacement of original faces deterministic,
  actor-aware, cacheable, and reversible by partial reruns

Tasks:

- extend instruction metadata with static consuming/non-consuming behavior
- extend per-item work results with:
  - generated runtime geometry
  - consumed source-face identities
  - item failures
  - source-element mapping/provenance where available
- consume an item only after successful processing
- leave failed, skipped, and `else`-routed faces unconsumed
- consume successful items even if a defective consuming implementation returns
  no replacement, while emitting a diagnostic
- keep input geometry immutable and readable by other fan-out branches
- allow multiple successful branches to consume the same source face:
  - remove the original once
  - retain every successful replacement
- propagate consumption to the actor owning the source geometry, including
  across nested function and actor execution boundaries
- assign globally unique IDs to generated elements during canonical
  main-thread publication
- commit generated geometry, assigned IDs, failures, and consumption effects in
  canonical item/instruction order
- implement an actor publication ledger keyed by execution/rerun scope
- derive final actor geometry from immutable contributions and consumption
  effects
- support non-consuming `select` returning stable references to original faces

Tests:

- consuming instruction removes originals from final actor geometry
- select observes and returns originals without consuming them
- select and consuming fan-out coexist
- two consuming branches retain both replacements and remove the original once
- partial item failure consumes only successful items
- nested function consumption updates the original owner actor
- worker completion order does not change published IDs or final geometry

Deliverables:

- consumption-aware instruction results
- canonical geometry publication
- actor publication ledger
- final actor geometry assembler

Exit gate:

- synthetic consuming and non-consuming handlers prove every required
  replacement behavior before extrusion is introduced

## Phase P6: Port Extrusion End To End

Goal:

- run the preserved production extrusion algorithm as a Phoenix instruction and
  match non-random production fixtures

Tasks:

- introduce the audited extrusion kernel and shared support files as new Phoenix
  source while preserving internal algorithms
- make only approved changes initially:
  - namespace/include changes
  - build/toolchain compatibility
  - equivalent type renaming
  - ownership modernization with unchanged behavior
- do not fix documented algorithmic defects during this phase
- implement a Phoenix extrusion descriptor/handler rather than porting the
  legacy command
- resolve extrusion parameters, profiles, and labels through Phoenix inputs and
  the immutable registry
- multiplex once per input face
- provide Phoenix-controlled deterministic random services if a selected path
  requires randomness, but exclude it from initial production comparisons
- use the adapter pipeline to prepare instruction-specific working geometry
- run isolated face items on workers when the audited kernel is thread-safe
- translate per-item kernel errors into Phoenix failure records and `else`
- demote, validate, and repair successful outputs
- publish IDs, labels, geometry contributions, and consumed faces canonically
- produce final actor geometry and a simple exchange/debug output suitable for
  comparison

Initial golden fixtures:

- simplest labeled extrusion
- extrusion with distinct cap/side/directed-edge labels
- extrusion with multiple input faces
- select plus extrusion fan-out
- two extrusion branches consuming one source face
- nearly coincident/degenerate input demonstrating repair
- one failure/`else` case
- one conflicting-label migration/link case

Deliverables:

- preserved extrusion kernel integrated into the Phoenix build
- Phoenix extrusion handler and adapter
- production/Phoenix comparison harness
- initial golden fixture suite
- documented compatibility results and known deviations

Exit gate:

- all non-random initial fixtures satisfy the compatibility contract
- final output contains no overlapping consumed original faces
- labels survive exactly
- repeated runs and worker counts produce identical Phoenix results

## Phase P7: Integrate Extrusion With Cache And Partial Reruns

Goal:

- make concrete extrusion results participate safely in Phoenix cache and
  actor-subtree rerun behavior

Tasks:

- replace placeholder geometry identity in cache keys with runtime geometry
  fingerprints
- add to cache identity:
  - label-registry fingerprint
  - parameters and seed
  - graph/body/wiring identity
  - kernel/adapter version
  - conversion/repair-policy version
- cache canonical generated runtime geometry and consumed source-face identities
- never cache temporary working geometry initially
- store publication-ledger effects needed to reproduce a cache hit
- verify that cached replay assigns or restores element IDs deterministically
- invalidate the full cache initially on graph, label, kernel, adapter, or
  conversion-policy changes
- preserve parameter/input/seed-driven dependency invalidation
- remove an old rerun scope's contributions and consumption effects before
  applying its replacement
- restore an original face when the new rerun no longer consumes it
- prove cached, partial, and full uncached execution produce equivalent final
  actor geometry

Deliverables:

- concrete geometry cache entries
- consumption-aware cache replay
- publication-ledger partial reruns
- full/cache/partial equivalence tests

Exit gate:

- an extrusion parameter change can rerun its actor scope without stale
  replacement geometry or lost/restored faces

## Phase P8: Harden Extrusion And Platform Support

Goal:

- establish extrusion as a dependable template for subsequent kernel ports

Tasks:

- expand from synthetic fixtures to representative production projects
- benchmark against the production solver:
  - peak memory
  - runtime geometry storage
  - conversion time
  - kernel time
  - repair time
  - total run time
  - cache size
  - partial-rerun latency
- validate Windows x64/MSVC
- validate Linux x64/GCC
- add macOS Apple Silicon/AppleClang configuration and validation
- test Linux Clang and Intel macOS as best effort
- audit third-party attribution for newly introduced files
- confirm kernel execution is isolated and free of mutable global state
- triage deviations into:
  - adapter defect
  - fixture/comparison defect
  - accepted compatibility difference
  - documented production kernel defect deferred for later

Deliverables:

- cross-platform build/test results
- performance baseline
- expanded fixture suite
- prioritized post-extrusion backlog

Exit gate:

- extrusion passes required platforms and representative non-random fixtures
- the team agrees that the adapter/publication architecture can be reused

## Phase P9: Port Partition

Goal:

- port partition using the proven label, runtime geometry, adapter, publication,
  cache, and fixture infrastructure

Tasks:

- repeat the Phase P1 source-boundary audit for partition
- preserve partition solver algorithms and dependent predicates/constraints
- map extensive current/opposite edge and face label semantics explicitly
- port only the necessary model/configuration adapter behavior
- validate split/merge element identity and label propagation
- reuse common kernel-support code only where the extrusion and partition audits
  demonstrate a stable shared boundary
- add partition-specific production fixtures, failures, fan-out, cache, and
  partial-rerun cases

Deliverables:

- preserved partition kernel
- Phoenix partition handler and adapter
- partition golden fixtures and compatibility report

Exit gate:

- partition meets the same determinism, label, consumption, cache, and platform
  requirements as extrusion

## Phase P10: Port Inset

Goal:

- port inset while respecting its instruction-specific precision and topology
  behavior

Tasks:

- repeat the trusted-boundary and production-oracle audit
- retain inset's existing kernel choice unless an approved compatibility change
  is required
- implement the Phoenix adapter and consuming handler
- validate generated topology, labels, repair, failures, and actor publication
- add inset-specific production, cache, partial-rerun, and platform fixtures

Deliverables:

- preserved inset kernel
- Phoenix inset handler and adapter
- inset golden fixtures and compatibility report

Exit gate:

- inset meets the established kernel-port acceptance contract

## Phase P11: Expand The Kernel And Instruction Inventory

Goal:

- port remaining production value systematically rather than by copying the
  legacy VM wholesale

Tasks:

- prioritize remaining functionality by production usage and dependency:
  - overlay
  - geometry cleanup and merge operations
  - select and rename
  - smooth
  - profiles and straight skeleton support
  - external geometry instancing
  - exporters
- classify each item as:
  - preserved kernel
  - Phoenix-native value transformation
  - Phoenix runtime/control-flow behavior
  - persistence adapter
  - deferred feature
- maintain per-kernel golden fixtures and compatibility reports
- add materials after geometry and labels are stable
- revisit legacy RNG compatibility only if existing randomized projects require
  identical historical output

Deliverables:

- prioritized port inventory
- incremental kernel/handler ports
- material integration plan
- explicit deferred-feature list

## Phase P12: Migrate Existing Projects

Goal:

- reuse the existing production project/function library without importing
  drifting label definitions or legacy VM coupling

Tasks:

- inventory required JSON and binary source versions
- define a versioned Phoenix persisted graph schema
- build migration tooling rather than embedding legacy loader assumptions in
  runtime types
- resolve and canonicalize label definitions during migration
- make conflicting definitions for one UID a migration error
- preserve function-local label visibility and rename semantics
- map supported production instructions and options to Phoenix descriptors
- report unsupported options explicitly
- validate migrated projects through the accumulated production fixture suite

Deliverables:

- persisted Phoenix schema
- legacy project migration pipeline
- migration diagnostics and reports
- production library compatibility results

## Phase P13: Post-Port Optimization And Kernel Repair

Goal:

- optimize only after compatibility coverage makes changes safe

Tasks:

- evaluate `float` runtime storage using measured workloads
- evaluate exact working-set reuse and adjacent kernel islands
- reduce precision inside kernels where production history or new tests support
  it
- improve cache retention across graph/label changes if feasible
- implement lazy loading with deterministic label/link behavior
- add hole support
- address documented production kernel defects one at a time
- require focused before/after fixtures and explicit approval for algorithmic
  kernel changes

Deliverables:

- measured optimization proposals
- versioned compatibility changes
- resolved kernel defect backlog

## Required Documentation During Every Kernel Port

Each kernel receives a short port record containing:

- trusted source files
- shared support dependencies
- legacy adapter behavior reimplemented in Phoenix
- permitted source changes
- topology accepted by the kernel
- precision/working geometry used
- label inputs and generated-label rules
- source/new element-ID mapping behavior
- consuming/non-consuming behavior
- random inputs, if any
- thread-safety findings
- cache version
- golden fixtures
- known production defects
- accepted deviations

## Immediate Next Step

Begin Phase P6 using the audited profile-extrusion boundary, direct extrusion
working input, immutable labels, and consumption-aware publication ledger.
