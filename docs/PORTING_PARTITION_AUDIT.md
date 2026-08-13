# Partition Port Audit

Status: initial source-boundary audit complete; kernel adaptation not started.

## Observed Production Path

The production instruction enters through `commands/partition_command.cpp`.
That file mixes VM/loading behavior with geometry preparation and result
assembly; it is not itself the trusted kernel boundary.

The execution path is:

1. load a partition model from JSON or binary persistence
2. build predicates, constraints, cuts, repeats, labels, and the solver plan
3. subdivide input geometry and optionally merge input faces
4. multiplex over each input face
5. for legacy 2D input, solve and tessellate directly in an arrangement
6. for 3D input, clean and project the face to 2D, run the same partition
   solver/tessellator, then reconstruct cuts and labels on the 3D mesh
7. tag successful replacement faces versus failed/`else` faces
8. publish the mutated geometry and rebuild IDs

Phoenix must replace steps 1, 3, 4, 7, and 8 with immutable registry,
canonical geometry, per-item failure, consumption, and publication services.
Legacy step 5 is implementation evidence, not a Phoenix runtime path. Phoenix
will preserve the solver/tessellator only as temporary projected working
geometry inside the canonical-3D path in step 6.

## Trusted Kernel Candidates

| Production file | Lines | SHA-256 |
|---|---:|---|
| `backend/partition/partition_solver.h` | 1305 | `f642705894f2ff886f3ce9a4e94f9747411aa90d0c33c0c60e7344ed29d948ab` |
| `backend/partition/partition_solver.cpp` | 1677 | `01a72a1a6a6d7a7c2d0f2397cf01cbf5a5e9ca1eebfa16320b3b6b83db6be13d` |
| `backend/partition/partition_solver_constraints.h` | 85 | `09c15d4cecd68e70bc8fd091a73199cb80267341ffbb18cceb618a630d46090d` |
| `backend/partition/partition_solver_constraints.cpp` | 901 | `ef8b1830997917196ec29c5d3be2f493c31410cd15a39f39e0641890a5d905fe` |
| `backend/partition/partition_solver_filters.h` | 100 | `b9ea13ec61d3a204e10950dc25f1c22e6d20e3a9a611338feb6aa5350315118f` |
| `backend/partition/partition_tesselator.h` | 245 | `704f68763130dc2bb97f92569d65bb21408230fbc02df7f91bfa7d9dff459cfc` |
| `backend/partition/partition_tesselator.cpp` | 1703 | `0ef7cdd235cf070ebc8a36ef6af8ccf6552dc3ed413dc33562f38605fb03662f` |
| `backend/partition/partition_errors.h` | 43 | `88ff56fcfba27753a38f0b86e42f50f69792bde78e433faf2fd185a3107c1e5b` |
| `backend/partition/partition_errors.cpp` | 75 | `65dbe35d49cb182636a6631a7726b99ff8ea25dafd90bd59b9d1ebd3177f6fb6` |

These files total 6,134 lines and depend heavily on production model,
randomizer, geometry, debug, and VM value types. They should be snapshotted
byte-for-byte before adaptation, as extrusion was.

## Adapter And Orchestration Code To Reimplement

- `commands/partition_command.{h,cpp}`
- `loaders/partition_loader.{h,cpp}`
- `loaders/binary_loader/binary_partition_loader.{h,cpp}`
- VM context, variables, randomizer acquisition, threading, outputs, and errors
- debug JSON and timers
- mutable geometry subdivision, tagging, notification, and global ID rebuilding

The loaders are valuable as a persistence-schema reference, not as runtime
code. Phoenix should link an immutable partition plan from persisted UIDs and
values before workers begin.

## Required Geometry Support

The command reveals additional trusted or compatibility-sensitive helpers:

- `cleanup_face`
- `simplify_face`
- `face3_to_face2`
- `arr_to_mesh`
- `merge_faces`

These require individual dispositions. They must not be copied wholesale until
the first deterministic projected planar-face cut proves which operations are actually needed at
the kernel boundary. In particular, cleanup merges only when current label,
opposite label, and edge ID agree; replacing it with generic cleanup would
change observable label and identity semantics.

## Label Contract

Partition depends more deeply on directed labels than extrusion. A cut can set:

- left and right result-face labels
- distinct labels on the two directions of the cut edge
- source-left/source-right current and opposite labels
- target-left/target-right current and opposite labels
- north/south/west/east and opposite west/east repeat labels
- separate final-repeat and margin labels

The tessellator explicitly writes both `edge.data.label` and
`edge.twin.data.label`. The 3D reconstruction similarly writes the current and
opposite halfedges independently and propagates labels across split pieces with
the same production edge ID. Phoenix's independent directed-halfedge labels are
therefore mandatory, not incidental.

Label definitions remain immutable and run-owned. The port must resolve every
persisted label UID to a stable `LabelId` before constructing the immutable
partition plan. Negative production values are absence/sentinel values at this
boundary and must not become ordinary registry labels.

## Identity And Consumption

Production mutates the input arrangement/mesh in place, replacing a successful
source face with result faces. Failed faces are tagged for `else` and retained.
Phoenix partition is therefore statically consuming:

- successful face: publish replacements and consume the original `FaceId`
- failed face: publish no replacement, retain the original, and route that face
  alone through `else`
- newly cut/split topology: invalid worker IDs, assigned canonically at
  publication
- unchanged boundary identity: preserve only when source mapping proves it
- split boundary pieces: do not reuse one globally unique Phoenix element ID;
  preserve source provenance separately and allocate unique published IDs

The last point differs from production, which deliberately shares an edge ID
across split pieces for label propagation. Phoenix must represent that as
provenance rather than violating run-wide ID uniqueness.

## Precision And Topology

Production topology is an extended CGAL arrangement, not an application-owned
face structure. Its DCEL payloads already carry vertex IDs/index/tag, directed
halfedge ID/label/tag, and face ID/label/tag. Phoenix must preserve that design:
the local projection envelope feeds an exact `CGAL::Arrangement_2` using
`Arr_extended_dcel`, and the preserved repository, solver, and tessellator work
through arrangement handles. Any provisional vector/map geometry used by the
compile spike is non-authoritative and must be removed when its behavior is
covered by the arrangement-backed adapter.
Function-level treatment and the recovery plan are binding in
`PORTING_PARTITION_SOURCE_MAP.md`.
The partition-used surface of production `segment_repository.h` is now directly
adapted and tested on the extended DCEL, including split-collinear ranges and
range-wide directed label mutation.
The foundational production solver types are also directly adapted under the
`trusted` namespace and tested against those repository handles.
Trusted branching, ancestry, filtering, candidate orientation, angle solving,
and five-segment cut-view construction are now directly adapted as well.

The solver is fundamentally planar and uses exact/inexact CGAL types through
production aliases. Canonical Phoenix geometry remains 3D `double`; Phoenix
will not implement a runtime `face2`, arrangement geometry value, or separate
2D partition handler. Every input is a canonical 3D face. Per-face work must:

1. validate planarity and supported topology
2. derive a stable local plane and orientation
3. project the 3D boundary into a kernel-local 2D arrangement
4. run the preserved planar solver and tessellator
5. lift generated points and topology back onto the source plane
6. demote to canonical 3D geometry with labels and provenance intact

The arrangement is instruction-local working geometry and never crosses
execution, publication, or cache boundaries. Exactness is upgraded only inside
this working phase where predicates and constructions require it.

### Precision-Reduction Intent

Partition must not reproduce production's system-wide exact-number storage or
carry exact numbers into Phoenix runtime geometry. However, the initial
compatibility port will preserve the production partition kernel's exact
working precision throughout its invocation-local solver, arrangement, and
tessellation phase. This is intentional: the first objective is a complete,
faithful port with the smallest possible number of simultaneous algorithmic and
numeric changes.

The initial port contract is:

- canonical input is projected from 3D `double` into the production-equivalent
  exact working representation
- preserved solver, arrangement, constraint, and tessellation operations retain
  their production exactness unless a current-CGAL API requires an equivalent
  formulation
- generated exact working geometry is lifted and demoted back to canonical 3D
  `double` only after the kernel completes
- exact values and handles remain invocation-local and never enter runtime
  values, cache entries, publication effects, or actor geometry
- the exact working boundary and demotion tolerance are versioned in cache
  identity

Only after the exact port satisfies the partition compatibility fixtures may a
separate optimization pass pursue the intended steady state:

- canonical input, output, cache entries, and publication geometry use 3D
  `double`
- projection, solver state, candidate scoring, distances, and other ordinary
  numeric work use inexact/double representations where compatibility permits
- exact arithmetic is introduced at narrow, named boundaries only for
  predicates or constructions that require it for robust topology
- exact values remain invocation-local and are demoted immediately after the
  protected operation or kernel phase
- no exact CGAL point, arrangement handle, or number type crosses into runtime
  values, labels, cache entries, publication effects, or actor geometry

The exact port should inventory the production aliases actually used by each
solver and tessellator operation without changing them. The later precision-
reduction pass will classify them as:

1. safely inexact
2. exact predicate with inexact construction
3. exact construction required
4. unresolved pending a focused fixture

Precision reduction is deliberate post-port optimization, not part of the
initial compatibility port and not an unreviewed rewrite of the tuned solver.
Each later reduction must be protected by deterministic fixtures covering topology, orientation, face labels,
current/opposite edge labels, failure routing, and coordinates within the
versioned tolerance. Any exact fallback must be explicit, measurable, and part
of the kernel/adapter cache version.

The exact port must first establish P8-style performance metrics separating
projection, exact solver/constraint work, tessellation, lifting, repair, and
publication. Later mixed-precision experiments must add separate inexact and
exact predicate/construction timings so comparisons are meaningful.

Version one should support planar canonical 3D faces with simple outer
boundaries and no holes. Multi-face input multiplexes per face, and faces may
lie on arbitrary planes; `(x, 0, z)` is one orientation, not a geometry type.
External `ext` geometry is a separate advanced path: production permits it only
with one main face and uses it only in the 3D workflow. Defer it until the basic
projected canonical-3D fixture passes.

## Randomness And Thread Safety

The solver randomizes repository candidates and uses random values for several
constraint solutions. Production clones a randomizer per face, but exact
sequence compatibility and scheduling behavior require more audit. The first
fixture must use a deterministic non-random plan. Later random paths must use
Phoenix per-item seeds.

The solver/model is reset and rebuilt before a command run, so sharing one
mutable production model across workers is unsafe without isolation. Initial
Phoenix execution should construct an immutable linked plan and create
invocation-local solver/view/repository/tessellator state. The legacy 2D
command currently processes faces serially; the relevant 3D path uses parallel face
execution with mutex-protected tag restoration. Do not infer thread safety from
that wrapper.

## Production Oracle Corpus

Existing backend fixtures provide a useful compatibility ladder:

- `should_cut`: smallest deterministic plan/input candidate, exercised through
  Phoenix's canonical-3D projection path
- `cut_length_test`: length constraint
- `cut_distance_1`: distance constraint
- `cut_many_1`: multiple cuts
- `cut_errors_repeat`: failure and repeat behavior
- `cut_O`: more complex polygon topology
- `should_cut_3d_faces`: projected 3D path
- `tutorial2`, `extrude_arch`, and `extrude_interplan`: downstream label usage

For each selected fixture, capture input geometry, partition definition,
resolved label UIDs, seed, semantic output topology, directed labels, face
labels, and failure routing. Do not compare raw production IDs or ordering.

## Recommended Port Order

1. **Complete:** record and hash-pin the trusted source boundary
2. **Complete (first-cut shape):** decode `should_cut` into a minimal immutable
   Phoenix partition-plan model, including all twelve label channels
3. **Complete (initial planar adapter):** define a canonical-3D-to-local-2D
   projection adapter preserving orientation,
   current/opposite labels, and source provenance
4. **In progress:** the exact repository candidate and mutable solver-view
   boundary needed by one deterministic projected cut is adapted. Production-
   style same-label collinear grouping and conjunctive current/opposite/
   opposite-face label matching and inclusive length ranges are covered. Linked
   scalar values enter the exact working representation only inside repository
   construction. The unconstrained sampled cut-point path, orientation checks,
   concave-face interception rejection, and five derived working segments are
   now adapted. Phoenix per-item seed derivation is connected to the production Boost
   engine shape and four-sample default. Fixed-line reference angles cover the
   parallel/perpendicular-style collapsed range. General wrapped cones, range
   intersection and compatibility-stream shuffling, ray clipping, opposite
   cones, and reverse fallback are now adapted. The first absolute segment-
   length restriction is also covered; percentage/reference length and
   distance constraints are next.
5. adapt tessellation, lifting, and canonical-3D demotion for that cut
6. integrate consumption, `else`, cache effects, and partial reruns
7. add constraints and repeat behavior incrementally
8. expand projection/lifting coverage to arbitrary planar orientations and the
   production `should_cut_3d_faces` fixture
9. add external geometry and optional merge/simplify paths

## Known Gotchas

- The solver quality calculation assumes a maximum 5000-unit input length.
- Cleanup equality includes both directed labels and the production edge ID.
- Production split pieces may intentionally share an edge ID.
- Repeat labeling has separate normal, last-repeat, and margin cases.
- Legacy 2D behavior is evidence only; porting it as a Phoenix runtime geometry
  representation would violate the accepted architecture.
- A successful solver view can still fail during tessellation.
- Simplification is optional command behavior and can change topology.
- Loader/model reset semantics must not become mutable run-global state.
- Initial compatibility preserves production exactness inside the partition
  kernel, but exactness remains working state and never becomes the runtime
  geometry model.
