# Extrusion Port Record: Production Audit

Status: Phase P1 audit complete. No production source has been copied.

Production source inspected at:
`C:\Users\emili\source\repos\threedee.io\threedee.solver`.

This record fixes the first trusted boundary and production oracle before any
Phoenix adaptation. It is intentionally about observed behavior, not a license
to carry the legacy VM into Phoenix.

## Scope Decision

Production's `extrusion` command multiplexes more than one implementation:

- amount-only extrusion normally uses the command-local
  `simple_extrude_faces3`; it does not exercise the large profile kernel
- profile and label-map extrusion use `extrude::run`
- `hybrid_extrusion` in `backend/extrusion/extrusion.h` is not on the observed
  active command path
- `new_extrusion.h` is included through legacy interop, but no active call to
  its `run` path was found

The first preserved-kernel port therefore targets profile extrusion through
`extrude::run`. Amount-only extrusion is a separate behavior to reimplement or
port after this kernel path is established. This distinction must remain
visible in descriptors and tests; a simple amount test is not proof that the
profile kernel was ported.

## Observed Call Path

1. `loaders/extrusion_loader.cpp` parses persisted command data and resolves
   labels and profiles through legacy assets.
2. `commands/extrusion_command::operator()` obtains input geometry, rejects
   shared input, calls legacy subdivision, selects the simple or profile path,
   and clusters faces when required.
3. `extrusion_command::extrude_faces3` converts source points to doubles,
   resolves a profile for each directed edge, supplies source vertex/edge IDs,
   and invokes `extrude::run(input, mesh)`.
4. `backend/extrusion/extrude.h` builds the profile extrusion into the target
   polyhedron and assigns face/directed-edge labels and result tags.
5. Legacy simplify, merge, hard-edge, and tag helpers may mutate the result.
6. `collect_results` classifies cap, bottom, and side faces by tags. Command
   options may then overwrite face labels for `all`, `cap`, `sides`, or
   `bottomFaces` outputs.
7. The legacy context builds final IDs, converts output coordinates to double,
   publishes geometry, and emits output/category events.

The Phoenix adapter and handler will replace steps 1, 2, 5, 6, and 7. The
initial trusted algorithm is step 4 plus only the support proven necessary to
compile and run it.

## Loader Contract To Preserve During Migration

The JSON loader currently recognizes:

- profile by `profile.id` or `profile`
- `amount` (default `0`), `range` (default `-1`), and `step` (default `-1`)
- `merge`, `join`, `reduce_precision`, `bottom`, and `joinColineal`
- label overrides `cap`, `skirt`, `sides`, `bottomFaces`, and `all`
- directional labels `bottom`, `top`, `left`, and `right`, defaulting to the
  south, north, west, and east compass labels
- `method: "map"` entries containing `from`, `to`, and `mirror`
- legacy misspellings `hardEgesEnabled` and `hardEgesAmount`

Observed defaults are `false` for `merge`, `join`, `reduce_precision`, and
`bottom`, and `true` for `joinColineal`. These persisted spellings and defaults
belong in the migration layer, not in the Phoenix kernel API.

## Source Disposition

### Trusted kernel candidate

- `backend/extrusion/extrude.h`: active profile extrusion algorithm and kernel
  input/output construction
- `backend/extrusion/extrude_plan_builder.h`: direct plan-building dependency
- `backend/extrusion/extrusion_errors.h` and `.cpp`: error identities to map to
  Phoenix diagnostics; the legacy exception plumbing itself is not trusted

These files are candidates, not yet copied. Preserve internal algorithms while
allowing namespace, include, type, ownership, and current-CGAL compatibility
changes that have no behavioral effect.

### Shared support to isolate behind Phoenix types

- `geometry.h` and its geometry data/CGAL macros
- profile representation and label access used by the command's profile
  provider
- polyhedron builder/data types carrying face, halfedge, vertex, ID, label,
  and tag fields

Do not copy these wholesale. P2 through P4 must provide narrow label, profile,
runtime-geometry, and working-geometry equivalents first.

### Legacy adapter behavior to reimplement

- `loaders/extrusion_loader.h` and `.cpp`
- `commands/extrusion_command.h` and `.cpp`
- `commands/extrusion_interop.h`
- cleanup/simplify/merge/tag/hard-edge helpers invoked around the kernel
- legacy ID allocation, geometry notification, category-output events, and
  subdivision/removal notification
- `vm/threads.h` and `.cpp`

The behavior is evidence for Phoenix descriptors, adapters, repair,
publication, and consumption. These files are not port boundaries.

### Excluded from the first kernel slice

- `backend/extrusion/extrusion.h` and `extrusion_traits.h`
- `backend/extrusion/new_extrusion.h`, `new_bsp.h`, `plan_builder.h`,
  `extrusion_types.h`, and collision support headers
- command-local `simple_extrude_faces3`
- binary/JSON VM command construction beyond migration parsing
- exporter and server code

An excluded file may be reconsidered only when a fixture or compile-time call
graph proves it is required.

## Precision And Topology Boundary

Production globally stores `Exact_predicates_exact_constructions_kernel`
geometry, but the active adapter explicitly converts source points to doubles
before constructing `extrude::input`. `extrude.h` uses `DoubleKernel` for major
plan types while also using exact CGAL types for selected predicates and
intersections, and writes into the production exact polyhedron. The command
then demotes its output with `to_double()`.

This supports the Phoenix decision: canonical geometry remains double-based;
the adapter promotes only the working structures the preserved kernel needs
and demotes before publication. Exact CGAL objects and handles must not cross
that lifetime boundary.

The observed path expects polygonal faces with valid orientation and directed
boundary edges. The first fixture is a closed single face with a label-selected
open profile. Holes and unsupported/non-manifold topology remain adapter
failures until explicitly proven.

## Labels

The kernel input has bottom, right, top, left, skirt, and cap defaults. Each
input corner additionally carries its selected profile, cap label, source
vertex ID, and source edge ID. The kernel assigns:

- a face label to generated side faces
- labels independently to directed halfedges according to profile/directional
  position
- the cap label to generated cap faces
- cap boundary labels derived from corner/profile rules

Later legacy cleanup can depend on equality of current and opposite halfedge
labels, so Phoenix must preserve directed-halfedge labels independently. The
command's `all`, `cap`, `sides`, and `bottomFaces` options are deliberate
post-kernel face-label overrides.

Production passes integer labels through the kernel. Phoenix will retain this
shape with an immutable run-owned `LabelId`; no kernel or adapter may mutate
the definition behind an integer.

## Element IDs And Consumption

Production geometry carries integer IDs, defaulting to `-1`. The kernel copies
source vertex/edge IDs at identifiable continuation points, while new topology
is finalized by legacy `build_ids`. Phoenix must preserve source IDs only when
the adapter proves identity and leave new/split/merged elements unassigned for
canonical main-thread allocation.

The command is consuming. In the multithreaded legacy path,
`parallel_foreach_face` independently copies selected faces, runs callbacks,
erases the selected originals, and joins replacements. Phoenix must express
this as explicit successful per-face consumption; failed or `else`-routed
items remain unconsumed. The kernel itself must not own actor geometry.

## Randomness

Amount range/step selection and label-map/profile selection can consume the
legacy randomizer. Sharing that randomizer across parallel face work may make
consumption order schedule-dependent despite locking. The first oracle uses no
random parameters. Later randomized compatibility tests must use
Phoenix-controlled, per-item deterministic streams.

## Thread-Safety And Defect Backlog

- `extrude.h` contains a `static int DEBUG_COUNTER` under `DEBUG_EXTRUDE`; it is
  a data race and must be removed or made invocation-local when debugging is
  enabled.
- The legacy parallel wrapper catches unknown exceptions, prints a message,
  and continues. Phoenix must produce a structured item failure instead.
- Assertions enforce several runtime preconditions. Adapter validation must
  turn user-data failures into diagnostics without changing algorithmic cases.
- Debug output and hard-coded debug paths are observable shared side effects
  and are excluded from the port.
- Parallel legacy profile selection may share a randomizer, as noted above.
- Cleanup and simplification are interleaved with command behavior. Their
  compatibility effects require separate fixtures before deciding whether
  they become adapter repair or preserved support.
- `EXTRUDE_MAX_EDGE_LEN2` silently encodes a 5000-unit antenna threshold. Keep
  it unchanged for initial comparisons and investigate it only after golden
  coverage.
- A substantially different collision-detection system predates the active
  `brute_force_collisions` implementation. Preserve brute force for the initial
  port, then recover and evaluate the earlier system as a separately versioned
  post-port experiment. The still-active exact arrangement plan builder serves
  topology reconstruction after collision/profile events and should not be
  mistaken for the obsolete collision search.

Outside debug facilities, `extrude::run` appears invocation-local in this
audit. Thread safety is not accepted until a Phoenix stress test runs separate
kernel instances concurrently under sanitizers or equivalent diagnostics.

## CGAL And Boost Compatibility

Phoenix currently pins vcpkg baseline
`ea1a7396b05637a53bf23c078647ecc0edee4b80`, requests `cgal`, uses C++17, and
links `CGAL::CGAL`. Production uses older Visual Studio toolsets and legacy
CGAL APIs/macros. The active `extrude.h` path does not directly require the
Boost-based `extrusion.h`; Boost is therefore excluded from the first slice.

Source inspection cannot prove current-CGAL compatibility. A minimal compile
spike containing the trusted candidate files and narrow Phoenix substitutes is
required at the start of P4/P6. Expected friction includes legacy polyhedron
modifiers/builders, exact-number access, Cartesian conversion, and custom
geometry macros. Compatibility edits must be catalogued and golden-tested.

## Production Oracle

Initial checked-in oracle:

`C:\Users\emili\source\repos\threedee.io\threedee.backend\tests\extrude_horizontal_profile`

It is non-random, contains one drawing feeding one profile extrusion, and has
checked-in `input` and `output` directories. The output records the original
drawing, removal of source face ID `4`, and the extrusion result. Current
output SHA-256 values are:

- `0.json`: `827D6C920960BA98C9AADDE029D85D5D94B8CE9C6ADFF26DC845016AD8CBA66D`
- `1.json`: `FD5789F2F8B3108E8586631595EE41ACA492C1B718F98988EC7181A060BABB4D`
- `2.json`: `90450066029CDAF46F0E01D4E8D1B8E49A2333F8B2A0AACE78DE1E428D38D2CF`

The extrusion event contains 78 exported vertices, 40 triangles, and face IDs
`5` through `23`. Export times are observational and must be ignored in
semantic comparison.

When a production backend executable is available, reproduce with:

```powershell
threedee.backend.exe test "C:\Users\emili\source\repos\threedee.io\threedee.backend\tests\extrude_horizontal_profile"
```

or write a disposable output directory with `run <fixture> --save <output>`.
Do not use interactive `create_test` to overwrite the checked-in baseline
during porting. No backend executable is present in the inspected checkout, so
this audit validated the captured oracle and its hashes but did not regenerate
it.

The smaller `extrude_horizontal` fixture also reaches profile behavior but has
no checked-in output. It is the preferred seed for a future minimal fixture
once the production runner is available; until then, the checked-in profile
fixture is the authoritative P1 baseline.

## Compatibility Contract For The First Port

Compare semantic geometry, not serialized event timing or incidental order:

- consumed source face identity
- vertex positions within the explicitly versioned demotion tolerance
- face topology and orientation
- face and directed-halfedge labels exactly
- preserved source IDs where identity is proven
- deterministic new IDs under Phoenix publication
- cap/side/bottom classification
- stable structured error category

No production defect listed above is to be fixed inside the initial kernel
port. Deviations require a focused fixture and an explicit recorded decision.

## P1 Exit Assessment

- Every file observed on the first active profile-extrusion path has a trusted,
  support, adapter, or excluded disposition.
- A stable checked-in production result exists and is fingerprinted.
- Live regeneration remains an environment prerequisite before declaring P6
  compatibility, because this checkout contains no backend executable.

P1 is complete. P2 may begin; no kernel source should be copied before P2-P4
establish labels, canonical runtime geometry, and the narrow adapter boundary.
