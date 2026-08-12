# Extrusion Port: Phase P6 Work Log

Status: implementation complete; production compatibility signoff provisional.

## Preserved Baseline

The four audited production files have been copied byte-for-byte into
`src/legacy/extrusion`. Their SHA-256 hashes are recorded beside the snapshot
and match the production checkout. The normal Phoenix build does not include
them yet.

No production loader, command, VM, threading wrapper, exporter, cleanup
command, or global geometry header was copied.

## First Compile-Boundary Findings

The active `extrude::run` source has only three direct includes, but depends
transitively on a large implicit interface supplied by production macros:

- `CGAL_TYPES`, `CGAL_EXACT_TYPES2`, and `CGAL_EXACT_TYPES3`
- exact 2D arrangement types and observer-backed plan construction
- the custom mutable polyhedron/HDS builder
- vertex, halfedge, and face data carrying IDs, labels, and tags
- projection, intersection, face-building, and mesh utility aliases
- `solver_error` and numeric extrusion error codes
- the legacy profile interface used by each input corner

Copying `geometry.h` would pull legacy storage and VM-era ownership into
Phoenix, defeating P3 and P4. The adapted kernel will instead receive a narrow
compatibility header that supplies only the types and operations named by the
preserved source.

## Adaptation Rules

Initial permitted changes remain:

- namespace and include changes
- replacing production macros with explicit aliases
- replacing the legacy output polyhedron with an invocation-local working mesh
- replacing `solver_error` construction with typed kernel failures
- removing debug printing, hard-coded paths, and the debug static counter
- ownership modernization that does not change geometry decisions
- converting integer labels to and from `LabelId` at the boundary

No collision, plan advancement, profile advancement, tessellation, labeling,
or face-generation logic may change during the initial compatibility pass.

## Integration Order

1. Express the production profile and per-corner input contract in Phoenix
   types.
2. Replace hidden geometry macros with explicit current-CGAL aliases.
3. Adapt `extrude_plan_builder` against current CGAL arrangement APIs.
4. Adapt the output builder and retain source-ID/label/tag data.
5. Compile the preserved algorithm without invoking it.
6. Run the smallest direct face/profile fixture without publication.
7. Connect repair and `GeometryItemEffect` publication.
8. Compare against the captured production oracle and expand golden fixtures.

## Current Progress

- The arrangement-based plan builder has been adapted to explicit CGAL 6.2
  types in `include/phoenix/extrusion/plan_builder.hpp` and
  `src/extrusion/plan_builder.cpp`.
- Its production observer behavior is retained: created/split edges carry
  corner IDs, coincident endpoints produce collision remaps, interior edges
  are removed, and bounded face cycles reconstruct the next plans.
- Focused square and clear/reuse reconstruction tests pass.
- The immutable profile/corner boundary is implemented in
  `include/phoenix/extrusion/profile.hpp`. It exposes precisely the preserved
  kernel operations (`size`, `direction`, `sign`, segment labels, and skirt
  labels) without importing the mutable legacy profile object.
- Direct `ExtrusionWorkingFace` conversion preserves source vertex, halfedge,
  edge, face, and label identities in `KernelExtrusionInput`.
- Reusable invocation-local staging and `CGAL::Surface_mesh` construction live
  in `WorkingGeometryBuilder` (`include/phoenix/working_geometry_builder.hpp`
  and `src/working_geometry_builder.cpp`). The thin extrusion `OutputAdapter`
  retains only the production-facing construction vocabulary and extrusion's
  cap/side classification constants.
- Directed halfedge labels and imported halfedge IDs are keyed by the target
  vertex, matching the legacy Polyhedron facet circulator used by `add_face`
  and `close_plan`. Generated vertex, halfedge, edge, and face IDs come from
  the run-scoped allocator; opposite halfedges share one generated edge ID.
- Face labels and the exact production tag constants are retained. The working
  mesh schema now includes `f:tag`; `CAP_TAG` is `-872348234` and `SIDE_TAG`
  is `CAP_TAG + 2`.
- The builder rejects nested/open/short facets and reports CGAL face-insertion
  failures as adapter diagnostics. Focused construction, metadata, directed
  label, demotion, and state-error tests pass.
- The complete Debug build and every `phoenix_*_tests.exe` regression target
  pass after the working-mesh schema extension.

The full 2,195-line production kernel has now been materialized as the isolated
adaptation unit `src/extrusion/kernel_port.cpp`; the byte-identical snapshot in
`src/legacy/extrusion/extrude.h` remains the authority. Its public boundary is
fixed in
`include/phoenix/extrusion/kernel.hpp`: immutable `KernelExtrusionInput` in,
invocation-local `WorkingGeometry` plus typed diagnostics out. It deliberately
has no executor, publication, repair, command, or cache dependency.
- The preserved brute-force collision, plan advancement, skirt handling,
  tessellation, face generation, and arrangement reconstruction body now
  compiles as `phoenix_extrusion_kernel_compile_tests` under CGAL 6.2.
- Legacy macro types and the face projector are replaced by explicit aliases
  and a narrow projector in `kernel_support.hpp`; solver exceptions map to
  typed `KernelErrorCode` diagnostics.
- The smallest direct positive-profile triangle produces the expected open
  triangular prism replacement: six vertices, three side faces, one cap, and
  no original bottom face. The result demotes to valid canonical geometry.
- The fixture verifies preservation of three imported vertex IDs, three
  directed halfedge IDs, and three shared edge IDs. Generated topology uses the
  supplied run allocator. Callers must supply the run allocator already
  advanced beyond all imported IDs, as required by the shared run ID namespace.

- Kernel entry now rejects non-finite coordinates, missing profiles, zero
  signs, and inconsistent per-corner profile signs before invoking preserved
  geometry decisions. The direct fixture proves the exact bottom/right/top/
  left directed-label distribution and three distinct cap-edge labels.
- `RunElementIdAllocator` is now the thread-safe run service used by handlers.
  Top-level execution seeds it past every ID in input geometry and nested calls
  share the same allocator, preventing imported/generated ID collisions under
  worker execution.
- `make_instruction_handler` provides the Phoenix extrusion instruction
  boundary. It prepares and runs each face independently, demotes and repairs
  successful output, publishes a `GeometryItemEffect`, and consumes the source
  face only on success. Failures remain item-scoped for `else` handling.
- The executor/publication integration fixture replaces one source triangle
  with exactly three sides and one cap. The original face ID is absent from the
  assembled actor, and the publication ledger records it as consumed.
- Multi-face execution produces one independent replacement per source face.
  Item-scoped kernel failure routes only the failed face to `else`, leaves that
  face unconsumed, and retains successful replacements from the same input.
- Two consuming extrusion branches remove their shared source face once while
  retaining both replacement contributions.
- Repeated execution with requested worker counts of one and four produces the
  same final canonical fingerprint. Face items currently execute serially
  inside the handler because allocating IDs concurrently would make ID order
  depend on scheduling; deterministic parallel ID-range reservation is a
  follow-up optimization, not a compatibility prerequisite.
- Direct kernel goldens exercise the current brute-force collision path, a
  horizontal profile transition, and a real skirt insertion caused by
  differing profiles at a collinear boundary vertex. Repeated runs are
  fingerprint-stable.
- The captured production oracle has been reconstructed from its exact drawing
  and profile inputs. Phoenix produces 24 unique vertices and 19 semantic
  faces; triangulating those faces yields the production export's 40 triangles,
  with 18 side-tag faces and one cap-tag face.
- The complete Debug build and every test executable pass at this checkpoint.

P6 implementation is complete. Full compatibility signoff remains provisional
because no production backend executable is available to regenerate the oracle
and expose a canonical semantic result for coordinate and label comparison.
The next implementation phase is P7 cache and partial-rerun integration.

## Open Compatibility Risks

- production was built against older Visual Studio/CGAL APIs; Phoenix currently
  resolves CGAL 6.2
- the production output uses a custom Polyhedron HDS rather than
  `CGAL::Surface_mesh`
- plan construction uses arrangement observer callbacks whose signatures may
  have changed
- output tag semantics must survive before cap/side/bottom classification
- the production backend executable is unavailable, so the checked-in oracle
  remains the initial comparison source
- source attribution/licensing for the internal production files needs an
  explicit repository-level decision before external distribution

## Historical Collision Implementation

Before the current `brute_force_collisions` path, production contained a
substantially different collision-detection implementation. The current kernel
still uses the exact arrangement-based `extrude_plan_builder`, but for planar
topology reconstruction after collision/profile events—not as a replacement
for brute-force collision searching.

P6 must preserve `brute_force_collisions` as the production compatibility
baseline. The earlier collision system is a post-port investigation candidate:

- locate its last complete production revision and dependent files
- determine why it was replaced
- compare correctness, pathological cases, asymptotic behavior, memory use,
  determinism, and thread safety against the brute-force implementation
- build focused golden fixtures before enabling either implementation behind a
  versioned kernel policy

Do not revive or combine the earlier collision implementation during the
initial extrusion port. Compatibility coverage must exist first.
