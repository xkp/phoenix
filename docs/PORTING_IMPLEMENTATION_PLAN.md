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

Status: implementation complete; compatibility signoff is provisional. The
audited kernel candidates are preserved byte-for-byte
under `src/legacy/extrusion` with verified source hashes and are quarantined
from the build. See `PORTING_EXTRUSION_P6_LOG.md` for the explicit compatibility
boundary and adaptation sequence.

Current checkpoint: the full preserved geometry body compiles under CGAL 6.2
behind the immutable kernel boundary and is connected to execution, repair,
failure routing, consumption, and canonical publication. Fixtures cover a
direct labeled triangle, multi-face multiplexing, item-scoped failure and
`else`, two consuming branches, brute-force collision, horizontal profile
transitions, actual skirt insertion, and deterministic results across repeated
runs and requested worker counts. The checked-in production fixture matches
Phoenix's semantic topology (24 unique vertices, 19 faces, and 40 export
triangles) and cap/side classification. Final compatibility signoff still
requires a runnable production backend to regenerate the oracle and compare
coordinates and labels, not just its captured semantic topology.

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

Status: complete. Instruction cache entries now retain canonical
`GeometryItemEffect` records as well as outputs. Consuming extrusion cache hits
replay generated runtime geometry and consumed source-face identities through
the publication ledger; no working CGAL geometry is cached. Cache identity now
includes the label-registry fingerprint and explicit kernel, adapter, and
repair-policy versions in addition to graph, input geometry, call path, and
seed identity. A changed identity replaces the prior publication scope and a
non-consuming replacement restores the original face. Focused uncached/cache
replay/scope-replacement coverage and all regression executables pass.

Whole-function cache snapshots are bypassed when a consuming function is
executing with a live publication ledger, because returning only its actor
snapshot would skip consumption replay. The partial-rerun planner likewise
rejects function-call and actor-subtree snapshot shortcuts for consuming
graphs. Resolved rerun scopes now carry the shared cache, publication ledger,
run ID allocator, label fingerprint, and kernel/adapter/repair versions into
execution, allowing instruction-level cache hits to replay consumption safely.
Non-consuming function-call and actor-generation caching retain their existing
behavior. An end-to-end fixture now runs the real extrusion kernel, plans and
resolves a public partial rerun, replays the cached canonical effects without a
second kernel invocation, applies the actor replacement to the scene, and
matches the full-run geometry fingerprint. A changed kernel identity then
reruns the same scope with no successful consumption, removes the stale
replacement, restores the original face, and updates the scene. This proves
full, cached, and changed partial-run publication equivalence for the initial
extrusion milestone.

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

Status: repository work complete; external evidence pending. Windows x64/MSVC Debug with CGAL 6.2 is verified by the
complete test suite. Windows Release now uses a genuinely separate single-
configuration build tree and passes all 22 test executables. Extrusion exposes
optional aggregate stage metrics for preparation, kernel, demotion, and repair
without retaining geometry payloads. Explicit Apple Silicon and Intel macOS CMake/vcpkg presets
now complement the existing Windows and Linux presets. Source inspection found
no mutable global state in the adapted kernel, and a focused stress fixture
runs 16 independent kernel invocations concurrently with invocation-local
inputs, builders, meshes, and ID allocators. All results demote successfully.
Platform evidence, attribution findings, and remaining performance/thread-
safety work are tracked in `PORTING_EXTRUSION_P8_HARDENING.md`.
Linux GCC, Apple Silicon AppleClang, and Linux Clang/TSan CI jobs are defined,
but their results cannot be claimed from this Windows workspace. Final P8 exit
also requires representative production projects, a runnable production
solver, and an explicit internal-source redistribution decision.

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

Commit hygiene: the accumulated port is split by the binding review plan in
`PORTING_COMMIT_PLAN.md`. The production import, compatibility boundary, and
future end-to-end adapter are separate commits; shared build/test/documentation
files require hunk-level staging.

C6 end-to-end adapter status: the first production-backed runtime path is
complete. `production_adapter.{hpp,cpp}` accepts a canonical 3D face reference,
an already-linked production `partition_model`, immutable scalar values,
label-based base-segment bindings, and a deterministic seed. It projects the
face into an invocation-local exact production arrangement, runs the production
repository/plan/solver/tessellator, lifts result faces back to canonical 3D,
preserves face and directed-halfedge label integers, allocates fresh published
geometry IDs, and emits a publication effect consuming the source face only on
success. The integration fixture covers an empty-model round trip, a real
deterministic one-cut model, face/cut label propagation, and publication-ledger
replacement without retaining the overlapping original.
Extended C6 parity coverage now runs the same adapter with production-shaped
models for recursive cuts, repeat-by-count distribution with a secondary gap,
Bezier insertion, and a production `segment_distance_constraint`. These are
runtime executions of the imported production plan/solver/tessellator, not
parallel Phoenix implementations. Each fixture uses a fresh model so its
cached plan cannot leak between cases.
The Phoenix instruction boundary is now implemented in
`partition/instruction.{hpp,cpp}`. It fans canonical input faces into
independent partition items, derives deterministic per-face seeds, publishes
successful replacement geometry, routes failed source faces through structured
item failures without consuming them, and returns a geometry collection. Its
linked-model contract is deliberately a factory: every item receives a fresh
production `partition_model`, preventing the production model's lazy plan cache
from becoming shared mutable state across faces or workers. End-to-end tests
cover publication/consumption, execution-cache reuse, failure context, and
deterministic two-face fan-out for one- and four-worker requests.

The production support-header include graph is now localized under
`partition/ported`. Production geometry constants, types, utility bodies, and
error contracts are hash-pinned in the manifest. SVG/file output and timers are
dropped as diagnostics-only code. `PHOENIX_PRODUCTION_SOLVER_ROOT` has been
removed from CMake, and the compiler dependency database confirms that no file
from the production checkout participates in either partition target. Platform
matrix execution remains the final external verification step.

Status: source-boundary audit and extended-DCEL adapter slice complete; solver
port reset to the audited production boundary, and tessellation is not started.
Correction checkpoint: the later `trusted_*`, `adapted_*`, `PlanExecutor`,
`StraightCutTessellator`, and repeat-distribution implementations are rewritten
behavioral scaffolding, not accepted kernel ports. They are quarantined and
must not be extended or linked into the runtime. The replacement port now lives
under `partition/ported`, beginning with a mechanically adapted, hash-pinned
copy of production `partition_solver_filters.h`; its tests have switched away
from the rewritten filter. Solver, constraints, and tessellator must follow the
same copied-source pattern before their scaffolding is removed.
The complete authoritative production partition set is now hash-pinned in
`PORTING_PARTITION_PRODUCTION_MANIFEST.md` and wired as the read-only,
excluded-from-default `phoenix_partition_production_probe` object target. This
probe compiles solver, constraints, tessellator, and errors together so the
compiler—not manual decomposition—defines the compatibility adapter backlog.
Legacy `debug_json` is explicitly dropped. A source-compatible null diagnostics
object preserves production signatures and fluent diagnostic calls without
importing serialization, VM state, or debug behavior.
The ported production foundation now also includes `partition_view` with its
copy isolation, segment replacement, reset, angle restriction/intersection,
and first-error behavior intact. Its only ownership substitutions are non-
owning invocation repository/RNG references, removal of VM context, structured
first-error evidence, and an optional cut-middle-line sentinel.
The trusted boundary is hash-pinned under
`src/legacy/partition`, and the production path, solver/tessellator candidates,
directed current/opposite label
contract, split-piece identity issue, 2D versus projected-3D paths, randomness,
thread-safety risks, and existing production fixture corpus are recorded in
`PORTING_PARTITION_AUDIT.md`. Phoenix will not port legacy 2D runtime geometry:
all partition inputs and outputs remain canonical 3D. The first implementation
slice is a deterministic single cut on a planar 3D face, using an invocation-
local exact-2D working face/arrangement only as preserved-kernel geometry,
before
constraints, repeats, or broader arbitrary-plane coverage are introduced.
Architecture correction: production already uses `CGAL::Arr_extended_dcel`
with vertex, directed-halfedge, and face payloads sufficient for partition.
Phoenix now constructs the same arrangement-backed working topology and stores
source IDs, directed labels, face labels, and provenance on its CGAL DCEL.
`ExactWorkingFace` is only the projection/lifting envelope used to populate the
arrangement; it is not a replacement face or topology implementation. The
provisional vector/map solver types must be migrated to arrangement handles and
removed where they duplicate CGAL topology before P9 compatibility signoff.
The binding function-level port policy and recovery sequence are recorded in
`PORTING_PARTITION_SOURCE_MAP.md`. The detached repository, solver, angle, and
constraint implementations are quarantined compile spikes and do not count as
P9 solver progress. Accepted progress resumes with a direct adaptation of
production `segment_repository.h` against the extended CGAL DCEL, followed by
function-corresponding solver, constraints, and tessellator ports.
Recovery step 3 is complete for the partition-used production repository
surface: exact CGAL halfedge ranges, same-current-label collinear grouping,
linked predicate matching, known/unknown lookup semantics, unmatched/all-edge
collections, concavity, range label writes, and production randomization are
adapted directly. Overlay-only combination iterators and debug JSON are not P9
dependencies. Next is recovery step 4: direct ports of `repo_segment_id`,
`cut_segment_id`, `segment_info`, `angle_range`, and `partition_view` over these
arrangement-backed repository handles.
Recovery step 4 is now complete for the foundational production solver types:
repository/cut segment IDs and result arithmetic, `segment_info` exact endpoint
state and line restriction, `angle_range`, and `partition_view` branch-copy,
reset, angle, edge-identity, and error semantics. They live under the `trusted`
namespace and depend on the accepted arrangement repository, not the
quarantined spike. Next is recovery step 5: directly port
`partition_model::branch_simple`, the segment branch overloads, candidate
orientation, and `view_for_segment(s)` before adding cut-point solving.
  That branching slice is complete, including filter behavior, derived-segment
root lookup, hierarchy-side candidate checks, production quality calculation,
and midpoint cut line. Direct `angle_solution` and `view_for_cut` ports are also
  complete and produce the five production working segments on trusted views.
  Before extending that transcription, P9 now requires a production-shaped
  compatibility environment and source-diff ledger. Exact CGAL and arrangement
  aliases live in `partition/compat/geometry_types.hpp`, directly over the
  existing extended DCEL. The current `trusted_*` foundation and branching are
  retained as behavioral comparison fixtures, but will be superseded by copied,
  mechanically adapted production sources before runtime integration. Every
  changed production function is tracked in
  `PORTING_PARTITION_DIFF_LEDGER.md`.
The remaining body of `partition_model::branch(view, cut, result)` is now
ported with production variation count, endpoint sampling order,
validity/interception checks, angle-range iteration, reverse-endpoint fallback,
and accepted-view emission preserved. Its mechanical changes are recorded in
the diff ledger. Next is to consolidate the foundation and branching slices
into source-shaped adapted files, then port the production constraint workers
without changing their algebra.
The first production constraint worker slice is now adapted in
`adapted_constraints.{hpp,cpp}`: absolute and percentage segment-length
restriction plus relative-angle restriction. VM resolution, debug JSON, and
mutable plan registration remain outside the workers; production orientation,
cumulative endpoint restriction, missing-reference behavior, and angle-range
intersection are retained. Distance and extra-distance workers remain next.
Absolute and percentage distance workers and the immediate parent/child
extra-distance worker are now adapted as well. Their perpendicular half-plane
construction, cut-reference direction correction, ordered clipping, collapsed-
angle gate, child-segment fallback, and extreme-point selection follow the
production bodies. The remaining constraint work is plan/link scheduling and
the production filter predicates, not new geometry algebra.
Production base-length-percentage and base-angle filters are now adapted over
arrangement repository ranges with their squared comparison, angle folding,
and tolerance intact. The immutable plan also records production's exact
priority values and stable insertion order as data-only scheduled steps. The
next execution slice is the production `partition_plan::advance` branching/
worker/evaluator flow over these linked step descriptors.
`partition_plan::advance` is now adapted in `plan_executor.{hpp,cpp}`. It keeps
production's instruction-index increment, selection and apply-cut fan-out,
proposal-empty failure, evaluator rejection, worker continue/fail, view reset,
and terminal success behavior. Immutable typed payloads replace Boost variants;
linked constraint and evaluator functions are invocation-local and do not enter
the plan, cache, or publication state.
The first tessellation slice now covers one non-collapsed straight cut directly
in the exact extended-DCEL arrangement. It splits both selected boundary edges,
labels all four directed boundary pieces and their opposites, inserts and labels
both directions of the cut, copies the original face payload, and applies left/
right face labels. Recursive child cuts, collapsed-side handling, repeats, and
Bezier insertion remain explicit subsequent slices.
Straight-cut tessellation now also follows the immutable cut tree recursively.
Each child resolves its endpoints on the boundary of the parent face produced
for its side, avoiding stale repository handles after earlier edge splits.
Traversal preserves production left/right ownership and returns only leaf
faces. Collapsed-side handling, repeats, and Bezier insertion remain pending.
Collapsed straight-cut sides are now handled: individual zero-length boundary
pieces are valid, both sides collapsed is rejected, and one fully collapsed
side reuses its coincident boundary edge rather than inserting overlap. The
absent face is suppressed from recursion/result collection and the surviving
side receives its directed cut and face labels. Repeats and Bezier insertion
remain pending.
Repeat count and length distribution are now extracted behind immutable linked
inputs. Production margin defaults, primary/secondary adjustment modes,
maximum-cut clamping, asymmetric side lengths, and insufficient-space failures
are retained. Variable lookup and range randomization occur before this pure
kernel step. Repeated arrangement cutting and compass-label application are the
remaining repeat slice.
The fixed-input interpolated repeat path now applies that distribution to the
exact arrangement in production south-to-north order. It advances each side
independently, splits the current face boundary, inserts cumulative repeat
chords, identifies the remaining face for the next cut, and labels emitted
primary/secondary/margin strips plus both chord directions. Full compass-label
fallback across outer boundary ranges and source/target/cut-parallel repeat
directions remain pending before repeat compatibility signoff.
Interpolated repeat compass labeling is now complete for non-collapsed strips:
each emitted face boundary is classified against its lower/upper chords and
source/target sides, with independent current and opposite/twin labels. Linked
inputs supply the production fallback-resolved values, keeping label ownership
immutable. Non-interpolated repeat directions remain pending.
The immutable plan seam now represents source/target/result segment references
and all twelve production cut-label channels without importing the mutable
legacy cut tree. The exact face adapter projects a canonical planar 3D face,
retains source topology and directed labels, and lifts exact working points back
to canonical 3D coordinates. Its first tilted-plane round-trip test passes.
The invocation-local repository now groups adjacent collinear boundary edges
when their current labels agree, matching the production repository rule, and
maps immutable conjunctive current/opposite/opposite-face label predicates to
exact segment candidates with source provenance and directed labels. Ambiguous
matches remain multiple candidates and fail unique selection rather than being
silently resolved. The solver view
selects unique source/target candidates and owns resettable exact endpoint
state, matching the production separation between repository candidates and
mutable branch state. Inclusive minimum/maximum length predicates are linked as
ordinary immutable scalars and upgraded to exact squared-length comparisons
only inside repository construction. VM evaluation remains outside the kernel:
its results must be linked before plan construction.
The first unconstrained cut solver now consumes an externally generated ordered
sample sequence, performs exact interpolation, production-equivalent oriented-
boundary and near-line checks, and rejects boundary interceptions on concave
faces. Each accepted variation produces the production five-segment working
state: cut, source-left, source-right, target-left, and target-right. This state
remains invocation-local and carries no published IDs. Angle restrictions,
constraints, quality ranking, and tessellation are not yet adapted.
Phoenix per-item seed derivation now feeds a compatibility sampler preserving
production's Boost `minstd_rand`/uniform-real engine and four-variation default;
only the derived seed's low 32 bits enter that legacy-compatible engine. The
fixed-line angle case used by parallel/perpendicular-style restrictions is also
adapted, including reference-segment angle calculation, endpoint tolerance,
current-CGAL intersection extraction, and reverse source/target fallback.
The sampler is now stateful for restricted cuts so range shuffling, endpoint
sampling, and clipped-cone sampling consume one compatibility stream in
production order. General wrapped angle ranges, intersections with earlier
restrictions, symmetric/opposite cones, ray/segment clipping, and reverse
source/target fallback are adapted. The first absolute segment-length worker
also narrows source or target endpoints with production left/right orientation
correction and cumulative-range behavior. Percentage/reference length and
distance constraints remain pending.
The initial partition compatibility port preserves production-equivalent exact
precision throughout the invocation-local partition kernel for completeness.
Canonical input/output and cached/published geometry remain 3D `double`, and no
exact value or handle may escape the adapter. Reducing partition precision is a
separate post-compatibility optimization, attempted only after the exact port
passes its golden fixtures and establishes a performance baseline.

Goal:

- port partition using the proven label, runtime geometry, adapter, publication,
  cache, and fixture infrastructure

Tasks:

- repeat the Phase P1 source-boundary audit for partition
- enforce `PORTING_PARTITION_SOURCE_MAP.md`: every trusted function retains a
  production-source correspondence and every changed line is classified
- preserve partition solver algorithms and dependent predicates/constraints
- audit every partition number/kernel alias and preserve its production-
  equivalent exact behavior during the initial port
- record candidates for later classification as safely inexact, exact-
  predicate-only, exact-construction-required, or unresolved; do not apply
  those reductions until exact-port compatibility signoff
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
- exact partition working-boundary inventory and performance baseline
- deferred mixed-precision candidate inventory

Exit gate:

- partition meets the same determinism, label, consumption, cache, and platform
  requirements as extrusion
- no exact partition number, point, arrangement handle, or working mesh escapes
  into canonical runtime geometry, cache entries, or publication effects
- no quarantined compile-spike solver/repository/constraint implementation is
  reachable from the Phoenix partition runtime path
- each accepted solver and tessellator function maps directly to audited
  production source with only classified mechanical/boundary changes

## Phase P10: Port Inset

Implementation decision: use the same minimal-change philosophy as partition.
First establish the production inset source, inputs, outputs, labels, precision,
failure behavior, and fixtures as the compatibility oracle. Do not redesign the
operation while discovering its boundary.

The intended Phoenix implementation is reuse of the extrusion implementation,
because production inset and extrusion are understood to perform equivalent
geometric work under different instruction configuration. This is a planned
kernel consolidation, not permission to approximate inset behavior. Production
inset remains the oracle until fixtures prove the extrusion-backed path matches
its topology, orientation, distances, labels, degenerate handling, failures,
and face-consumption behavior. Any behavior that cannot be expressed through
the extrusion kernel must remain in a narrow inset-specific adapter or retain
the corresponding production code.

P10 oracle checkpoint: the active production kernel and its face-cleanup helper
are localized and compiled in `phoenix_inset_production_oracle`. The first
deterministic exact fixture covers a labeled convex rectangle and verifies one
center face, four side faces, and result/side face-label separation. The source
hashes, excluded command/debug dependencies, cleanup tolerances, directed label
roles, and preliminary extrusion-reuse constraint are recorded in
`PORTING_INSET_SOURCE_MAP.md`.
The first runtime boundary is complete: `inset/production_adapter.{hpp,cpp}`
accepts only a canonical 3D face reference, scalar amount, labels, and the
run-scoped ID allocator. Projection to the exact arrangement and all CGAL
handles are invocation-local; the result is lifted immediately into canonical
3D `double` geometry. The fixture verifies a tilted plane, center/side topology,
directed labels, success-only consumption, and failure preservation. The inset
instruction handler fans out canonical faces and emits only canonical 3D
geometry collections and publication effects. The handler now also passes
through `FunctionExecutor` and the publication ledger: a successful inset
replaces the source face on its owning actor with five generated faces. This
executor fixture intentionally uses the actor derived from the call path so
source ownership, consumption, and replacement remain in one publication
scope.
The compatibility corpus now also covers a tilted concave L-face, production's
default face and directed-edge label inheritance, instruction-cache replay with
a single kernel invocation, and partial-rerun replacement. The partial-rerun
fixture verifies both cached reproduction of the inset and restoration of the
original source face when a changed kernel version fails. The existing platform
workflow discovers and executes both inset test binaries through its
`phoenix_*_tests` sweep on Linux and Apple Silicon debug/release builds.

Goal:

- preserve production inset behavior while implementing the accepted operation
  through the shared extrusion kernel wherever equivalence is demonstrated

Tasks:

- repeat the trusted-boundary and production-oracle audit
- move the production inset code and its required support with only the minimum
  compatibility edits needed to compile as a local oracle
- map inset parameters, axes, winding, labels, and result roles onto extrusion
  inputs explicitly
- build differential fixtures that execute production inset and the proposed
  extrusion-backed implementation on identical inputs
- reuse the extrusion kernel only for fixture-proven equivalent behavior
- retain a narrow inset-specific compatibility path for any non-equivalent case
- implement the Phoenix adapter and consuming handler
- validate generated topology, labels, repair, failures, and actor publication
- add inset-specific production, cache, partial-rerun, and platform fixtures

Deliverables:

- localized production inset oracle and source/diff manifest
- documented inset-to-extrusion parameter and result-role mapping
- extrusion-backed inset kernel with any required narrow compatibility adapter
- Phoenix inset handler and adapter
- inset golden fixtures and compatibility report

Exit gate:

- inset meets the established kernel-port acceptance contract
- production-oracle fixtures demonstrate equivalence of the extrusion-backed
  implementation for every supported inset mode
- no production inset behavior is silently dropped merely to force kernel reuse

## Phase P11: Expand The Kernel And Instruction Inventory

Status: inventory complete. See `PORTING_P11_INVENTORY.md`. Merge M0-M6 and
the first-class non-scripted profile asset/resolution layer are complete.
Overlay is explicitly excluded from the port because the
production implementation is not production-ready; it is deferred to P13 for
possible clean reimplementation. Profiles are classified as the most important
persisted asset after labels and receive an independent immutable identity,
persistence, migration, and cache contract. Styles and materials are not
required dependencies because they may be retired.

The profile checkpoint is recorded in `PORTING_PROFILE_SOURCE_MAP.md`.
Immutable versioned assets, conflict-safe registry ownership, deterministic
fingerprints, interpolation, repeat expansion, named scalar bindings, Bezier
tessellation, stable labels, and explicit sign validation now resolve into the
existing extrusion kernel profile. Production JSON/binary migration remains in
P12, and evaluation of expressions remains deliberately blocked on the P11
scripting contract. Non-scripted 3D face select is now implemented and recorded
in `PORTING_SELECT_SOURCE_MAP.md`. It preserves production label routing,
edge/opposite/border/length predicates, deterministic count/range/step and
percentage limits, and `else`, while remaining immutable and non-consuming.
Directed-edge selection is pending an explicit stable halfedge runtime value;
it is not approximated as face selection. Non-scripted rename is now
implemented and recorded in `PORTING_RENAME_SOURCE_MAP.md`: manual maps,
seeded alternatives, face and directed-edge length/opposite/border/edge-count
conditions, owning-face label inheritance, and global fallbacks create an
immutable canonical copy while preserving every element ID. Expression and
binding-dependent relation modes remain gated on scripting. Smooth and
non-scripted instancing are the next core slices.
Smooth S0-S3 subdivision integration is now defined in
`PORTING_SMOOTH_SOURCE_MAP.md`. Subdivision and hard-edge rounding are separate
preserved production modes. Phoenix declares OpenSubdiv directly. Canonical 3D
topology now passes through OpenSubdiv uniform refinement, refined faces inherit
their coarse ancestor's stable label, generated topology receives run-scoped
IDs, and directed-edge labels remain unassigned exactly where production's
face-varying propagation is disabled. Transactional instruction publication,
cache replay, partial-rerun restoration, and the Linux/Apple platform matrix
are covered. Persisted option migration remains in P12, while production's
optional preprocessing flags require fixtures before exposure. The legacy
hard-edge rounding mode is not production-ready and is excluded from the
compatibility port. It is deferred to P13 for possible repair, replacement, or
retirement as an independent round-edge/bevel operation.

Goal:

- port remaining production value systematically rather than by copying the
  legacy VM wholesale

Tasks:

- prioritize remaining functionality by production usage and dependency:
  - geometry cleanup and merge operations
  - profile assets and deterministic profile resolution
  - select and rename
  - smooth
  - external geometry instancing
  - bounded loop and non-scripted control-flow core
  - scripting/expression runtime and script-dependent instruction modes
  - exporters
- keep overlay out of the port inventory and record it as a post-port clean
  implementation candidate
- exclude the deprecated production `share` instruction from the port and emit
  an explicit migration diagnostic when encountered
- classify each item as:
  - preserved kernel
  - Phoenix-native value transformation
  - Phoenix runtime/control-flow behavior
  - persistence adapter
  - deferred feature
- maintain per-kernel golden fixtures and compatibility reports
- add materials only if the product retains them, after geometry, labels, and
  profiles are stable
- treat profiles as first-class assets after labels, independent of whether
  styles and materials are retained
- do not make styles or materials prerequisites for profile or kernel ports
- revisit legacy RNG compatibility only if existing randomized projects require
  identical historical output

Deliverables:

- prioritized port inventory
- incremental kernel/handler ports
- material integration plan
- explicit deferred-feature list

### P11 scripting checkpoint

Scripting is a dedicated cross-cutting runtime workstream after the core
geometry/profile ports and before migration can claim broad production-project
coverage. Production V8 integration is an oracle for observable behavior, not a
required engine choice. Define an engine-independent expression contract,
deterministic seed/cache semantics, sandbox and resource limits, diagnostics,
and portability requirements before implementing script-dependent control
instructions, selection, rename, profile variables, instancing, or attributes.

The expected order is:

1. merge
2. profile asset identity and non-scripted resolution
3. non-scripted select and rename
4. smooth and non-scripted instancing
5. bounded loop and non-scripted control-flow core
6. scripting/expression contract and sandboxed runtime
7. expression-dependent instruction modes and control flow

### P11 loop checkpoint

Loop L0-L3 are implemented and recorded in `PORTING_LOOP_SOURCE_MAP.md`. The
engine-independent bounded runtime preserves fixed/ranged/stepped iteration
selection, ordered feedback and accumulation, zero-based index delivery,
deterministic per-iteration seeds, early termination, hard iteration limits,
and transactional failure that discards accumulated results. It does not add
cycles to the general graph. Each iteration can now invoke a complete acyclic
Phoenix function with typed `$index`, unique call-path identity, feedback
routing, and `all`/`output` precedence. Iteration geometry effects now remain in
a loop-owned private ledger and collapse into one outer replacement only after
complete success; failure exposes and consumes nothing. The collapsed outer
instruction is cacheable, loop options/body identity participate in the graph
revision, full partial-rerun replay/restoration is covered, failures identify
their zero-based iteration, and trace/work budgets cover nested execution.
Linux and Apple Silicon CI run all loop suites. Loop migration participates later in the
general P12 persistence design without prescribing JSON or binary as Phoenix's
format. Expression-updated variables wait for scripting.
7. project migration, exporters, and post-port work

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
- migrate script sources and bindings only through the versioned Phoenix
  expression contract; report unsupported production host APIs explicitly
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

- define and, if justified by production needs, reimplement overlay from a new
  contract; do not treat the legacy overlay kernel as the implementation base
- decide whether to repair, replace, or retire the non-production-ready legacy
  hard-edge rounding mode; if retained, expose it independently from subdivision
- evaluate `float` runtime storage using measured workloads
- evaluate exact working-set reuse and adjacent kernel islands
- reduce precision inside kernels where production history or new tests support
  it
- evaluate partition's deferred mixed-precision inventory only after the exact
  P9 port passes compatibility fixtures; introduce inexact operations one
  classified boundary at a time behind versioned golden comparisons
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

Begin P11 merge slice M0: localize the production 3D merge kernel family as a
compatibility oracle, audit its shared support, and capture option-isolated
fixtures before adapting it to canonical Phoenix geometry. Do not port the 2D
arrangement paths or introduce overlay as a dependency.

M0 audit checkpoint: `PORTING_MERGE_SOURCE_MAP.md` records the trusted source
hashes, production option order, permitted localization changes, ownership and
directed-label requirements, excluded 2D paths, and initial oracle corpus.
The production 3D border-rebuild template now compiles locally under CGAL 6.2;
its initial oracle passes empty, disconnected, and exact-shared-border meshes
with vertex welding disabled.
Directed-label reconstruction and duplicate-reference behavior are now pinned.
The first M2 fixtures also bracket production's vertex-welding threshold:
`0.5e-5` joins and `2e-5` remains separate for the `1e-5` window.
The production `merge_faces` body and its `simplify_face` dependency are now
localized with only compatibility includes, removal of the unused VM/context
wrapper, and removal of debug filesystem side effects. M3 oracle fixtures pass:
same-label coplanar neighbors merge, while different-label neighbors remain
separate when label matching is enabled. The remaining merge boundary work is
to pin or introduce adapter-level rejection of invalid/non-manifold input and
prove transactional no-consumption/no-publication on that failure path before
canonical integration.
M4 now pins both `mergeFacesLabels` variants. The persisted boolean maps
directly to production label matching: true preserves boundaries between
different labels; false allows their coplanar faces to merge. In the latter
case production retains the first traversed facet's label. Phoenix adapters
must make that traversal deterministic by ordering inputs by item order and
stable `FaceId`, making the earliest contributing face the survivor rather
than depending on incidental container order.
M5 reuses the localized production `cleanup_face3` path and preserves the
production `joinColineal` predicate exactly. A collinear boundary vertex is
removed only when both consecutive current-direction labels and both
opposite-direction labels match. Oracle cases now prove successful removal and
preservation for independent current-label and opposite-label mismatches. Only
the 3D cleanup entry point participates in merge; the legacy 2D cleanup template
is not part of the runtime pipeline.
M6 composition began with a transactional production pipeline that fixes
the option order in one place: border reconstruction/vertex joining, face
merging with its label policy, and collinear cleanup. All mutations occur on a
candidate mesh and the result is returned only after final topology validation.
An all-options oracle passes, and an empty-option rejection proves the source
mesh remains unchanged and no result escapes. Canonical aggregation, handler,
publication, cache/partial rerun, and platform integration remain before M6 is
complete. The canonical adapter and instruction handler are now integrated:
ordered canonical faces promote into the exact candidate, successful results
demote to canonical 3D geometry, and consumption is emitted only after complete
success. Publication, cache replay, and partial-rerun failure restoration pass
end to end. A dedicated merge workflow covers GCC on Linux and Clang on Apple
Silicon in debug and release configurations. Merge M0-M6 is complete.
