# Production Merge Source Map

## Scope

This record defines merge M0. Phoenix ports only production's 3D mesh behavior.
The arrangement overloads in `merge_faces`, `merge_borders.h`,
`cleanup_face2`, and all other persistent 2D geometry paths are excluded.

## Trusted Sources

| Production source | SHA-256 | Role | Initial disposition |
|---|---|---|---|
| `commands/merge_command.h` | `054B73EEAD5BF32FCD57CC1DF4534B5090D6D90ED4F121278E857588F21F01F6` | Option composition and success behavior | Behavioral oracle only; do not port VM command |
| `backend/operations/merge_borders3.h` | `4280A1B30A208FECA1740ED484B38CB5D6D91282C4051CE217E66B717200B10A` | Rebuild multiple face boundaries into a mesh | Preserve algorithm body; first compile/oracle slice |
| `backend/operations/join_vertexs.h` | `53325F5106AC09C54601C2FA0169D2618C311C2A1A848249D5677BCB2577CB4E` | Coincident/short-edge vertex joining | Preserve required 3D entry points only; M2 |
| `backend/operations/merge_faces.h` | `1216905602C1E25CF2ABF0C7997697049638049C788C5007C0F2C24EEE178188` | Compatible coplanar face merging and thin-face repair | Preserve 3D body; exclude arrangement functions; M3/M4 |
| `backend/operations/cleanup_face.h` | `56CF4D40BA647055640A3BD09A876CA2404F2CDC9EAE582E9F933FFBEE4C0A9E` | Collinear 3D boundary cleanup | The same production file already localized for inset; audit and expose only `cleanup_face3` for M5 |
| `backend/operations/simplify_face.h` | `B42B9996932DBAB2A99948CE600A82777E5EE2A2CD63C77CC2BF9BCC232916D7` | Direct `merge_faces` dependency | Preserve only if reached by accepted 3D entry points |
| `vm/variant.cpp`, `vm/threads.cpp` | not ported | Hidden `subdivide` aggregation order | Behavioral evidence for M1/M2 fixtures |

## Compile Boundary

The production templates expect localized geometry data and utilities already
present under `phoenix/partition/ported`. Merge will reuse that compatibility
foundation while it remains inside the quarantined production oracle. It must
not introduce a second geometry/DCEL definition.

Permitted mechanical changes during localization:

- redirect includes to Phoenix-local compatibility headers
- remove debug timers, file exporters, diagnostic JSON, and VM convenience
  overloads that are not called by the oracle
- replace production context ID callbacks with explicit vertex/edge ID
  generators
- qualify or adapt APIs removed or renamed by CGAL 6.2
- isolate 3D entry points from template members that mention arrangement types

Not permitted without a separately approved compatibility change:

- changing tolerances or geometric predicates
- changing face, edge, or vertex traversal order
- changing label or ID propagation
- repairing invalid/non-manifold cases differently
- substituting a new merge algorithm

## Production Composition

The command first materializes its input face collection through `subdivide`.
`mergeBorders` and `joinVertexs` affect that materialization step. It then runs
`mergeFaces`, followed by `joinColineal`. Phoenix must reproduce this order even
though it will express the steps explicitly rather than through a VM variant.

Resolved options are:

- `mergeBorders`
- `joinVertexs`
- `mergeFaces`
- `mergeFacesLabels`
- `joinColineal`

Persisted `method: "edges"` is loader shorthand that defaults the first two
options to true. It is a P12 migration concern, not a runtime option.

## Ownership, Labels, and IDs

- Merge is consuming only after the complete requested option pipeline
  succeeds. Every contributing source face is replaced by the produced mesh.
- A failure or invalid output consumes no source face.
- Face labels and both directed halfedge labels are compatibility data.
- Collinear cleanup may merge segments only under the production directed-label
  predicate; equal current labels with unequal opposite labels are not equal.
- Vertex welding must choose survivor identity deterministically. Production
  traversal behavior is the oracle until an explicit Phoenix ID policy is
  approved.
- Temporary tags and negative IDs are invocation-local and must be translated
  before canonical publication.
- All newly allocated IDs come from the run-scoped Phoenix allocator.

## M0 Oracle Fixtures

The first oracle target will isolate `merge_borders3` with `joinVertexs=false`:

1. two disconnected triangles remain two components
2. two triangles sharing an exact border become one valid connected mesh
3. current and opposite directed labels survive boundary reconstruction
4. duplicate input face references are handled exactly as production
5. an invalid/non-manifold combination reports failure without publication

M2 then repeats the boundary fixtures with exact, inside-tolerance, and
outside-tolerance vertex positions around the production `1e-5` threshold.

Current checkpoint: the complete production `merge_borders3` template is
localized at `include/phoenix/merge/ported/production/merge_borders3.h` and
compiles under CGAL 6.2. Mechanical changes are limited to explicit localized
includes, removal of the VM convenience overload and debug timer calls, and
routing the unavailable legacy context error through the existing error
callback with a local message. The oracle currently passes empty input, two
disconnected triangles, and two triangles with one exact shared border, with
vertex welding disabled.

The expanded oracle also pins directed halfedge-label reconstruction and an
important production quirk: repeating the same face reference produces two
output faces rather than deduplicating it. Phoenix adapters may reject duplicate
source references before invoking the kernel, but must not silently describe
deduplication as production behavior. M2 tolerance fixtures confirm that two
vertices separated by `0.5e-5` weld while vertices separated by `2e-5` remain
distinct under the production `1e-5` window.

M3 checkpoint: the complete production `merge_faces` and its direct
`simplify_face` dependency are localized under
`include/phoenix/merge/ported/production`. Changes remain mechanical: Phoenix
compatibility includes replace production paths, the unused VM/context wrapper
is removed, and hard-coded debug filesystem writes are disabled. The accepted
3D entry point compiles under CGAL 6.2. Oracle fixtures prove that adjacent
coplanar triangles with the same face label merge into one face when label
matching is enabled, while otherwise identical triangles with different face
labels remain two valid faces.

M4 checkpoint: production's persisted `mergeFacesLabels` value is passed
directly to the kernel's `match_labels` policy. When true, a shared boundary is
removed only if the adjacent face labels match. When false (including the
legacy loader default), different-label coplanar faces may merge. Production
does not synthesize or reconcile a new label in that mode: CGAL's surviving
facet retains the label of the first face reached by production facet
traversal. The oracle pins this behavior with labels 77 and 78 and observes 77
on the merged facet.

Phoenix will preserve that rule without making container iteration an implicit
public contract: the adapter must submit faces in deterministic source order,
defined by the input item order and then stable `FaceId`. Thus the earliest
contributing face is the label survivor when label matching is disabled. A
future label-combination policy would be a versioned behavior change, not part
of this port.

M5 checkpoint: merge reuses the already-localized production `cleanup_face3`
implementation from `phoenix/inset/ported/cleanup_face.h`; it does not copy or
rewrite the cleanup algorithm. Only the 3D entry point is invoked. The adjacent
legacy `cleanup_face2` template remains outside merge runtime scope and no 2D
geometry crosses or persists at the instruction boundary.

The `joinColineal` predicate is preserved verbatim: two consecutive segments
may collapse only when their current-direction labels match and their opposite-
direction labels also match. Oracle fixtures use a five-vertex polygon with
one collinear boundary vertex. Matching label pairs reduce it to four vertices;
changing either all current labels or all opposite labels around the boundary
keeps all five. The production cleanup tolerances remain `1e-6` for degenerate
edges and `1e-4` for collinearity.

M6 composition checkpoint: `phoenix/merge/production_pipeline.hpp` now owns the
single ordered 3D kernel sequence. It validates that at least one producing
operation is selected, copies the input into an invocation-local candidate,
runs border reconstruction (and optional vertex joining), then face merging,
then collinear cleanup, validates the final CGAL mesh, and returns it only on
success. This establishes the transactional kernel boundary: neither the input
mesh nor any publishable result is mutated on rejection.

The composed oracle enables all five options and produces one valid simplified
face from a labeled two-face input. A second fixture proves the legacy-invalid
empty option set returns no mesh and leaves the input topology unchanged. The
M6 is now complete. The canonical adapter aggregates ordered face references
into an invocation-local exact mesh, executes the transactional pipeline, and
demotes successful output to canonical 3D double geometry with new run-scoped
IDs. Its result records all contributing `(owner actor, FaceId)` identities only
after successful demotion. Failure effects publish no geometry and consume
nothing.

The instruction handler accepts either one geometry value or an accumulated
geometry collection, preserves contribution order and face-index order, and
emits one merge item. End-to-end fixtures cover publication and consumption of
both source faces, cache replay without reinvoking the handler, and partial
rerun restoration of both original faces when a changed merge fails. Linux GCC
debug/release and Apple Silicon Clang debug/release coverage is declared in
`.github/workflows/merge-platforms.yml`.

The invalid/non-manifold publication boundary is not yet claimed complete.
Production reconstructs some topologically conflicting inputs by duplicating
vertices, including repeated face references, so those cases are not reliable
failure fixtures. M4 must identify a production-rejected input or place an
explicit preflight validator in the Phoenix adapter, then prove that failure
leaves source geometry unconsumed and publishes no partial mesh.

## Known Risks

- Production merge code is substantially larger than the command wrapper and
  contains repair behavior mixed with primary merging.
- `merge_faces.h` contains active debug file writes on invalid meshes; these
  must become diagnostics, not filesystem effects.
- Several routines use mutable tags and negative sentinel IDs as scratch state.
- Production spelling (`join_vertexs`, `joinColineal`) is retained only when
  naming localized oracle symbols or persisted legacy fields; Phoenix public
  APIs should use corrected names.
- Hard-edge smoothing directly depends on selected `merge_faces` and
  `join_vertexs` helpers, so the accepted merge boundary will later be shared
  with smooth rather than copied again.
