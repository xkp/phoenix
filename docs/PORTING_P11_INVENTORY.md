# P11 Production Instruction Inventory

## Decisions

- Merge is the highest-priority remaining production instruction.
- Overlay is not production-ready and will not be ported. It is deferred to the
  post-port phase for a possible clean reimplementation.
- The production `share` instruction is deprecated and will not be ported.
  Migration must diagnose it explicitly rather than recreate its shared mutable
  geometry behavior. This does not remove external geometry `instance`, which
  remains in scope.
- Profiles are the most important persisted asset after labels. Treat profile
  identity, resolution, migration, and cache behavior as a first-class Phoenix
  contract rather than an implementation detail of extrusion.
- Styles and materials may be retired. No P11 kernel or profile design may make
  them required dependencies; any future support remains optional until that
  product decision is settled.
- Phoenix will not port or persist production 2D arrangement geometry. For
  remaining instructions, only canonical 3D behavior is in scope; a temporary
  2D projection is permitted only inside a kernel invocation when required.
- Production commands and VM plumbing are behavioral evidence, not port
  targets. Preserve proven kernel bodies and express their contracts through
  Phoenix handlers, canonical geometry, stable labels, and run-scoped IDs.

## Priority Inventory

| Priority | Capability | Classification | Port decision |
|---|---|---|---|
| 1 | Merge | Preserved kernel family plus Phoenix consuming handler | Port next, one option at a time |
| 2 | Profile assets and construction | First-class immutable asset contract plus Phoenix-native value preparation | Define identity/persistence early; extend the existing extrusion profile implementation without importing the VM builder wholesale |
| 3 | Select | Phoenix-native non-mutating value transformation | Port after merge; return references/copies without consuming sources |
| 4 | Rename | Phoenix-native immutable label transformation with production condition semantics | Port after select; never mutate shared label definitions or geometry in place |
| 5 | Smooth/subdivision | Preserved OpenSubdiv and hard-edge kernels behind canonical adapters | Port after label transformations; audit as two distinct modes |
| 6 | External instancing | Phoenix runtime transformation and asset adapter | Port 3D face placement, orientation-label behavior, transforms, and optional source consumption |
| 7 | Scripting/expression runtime | Cross-cutting Phoenix runtime subsystem | Define the behavioral and security contract before enabling script-dependent instruction modes |
| 8 | Script-dependent instruction modes | Phoenix runtime/control-flow and value transformations | Complete conditional select/rename/profile/instance behavior and control instructions after scripting is stable |
| 9 | Exporters | Boundary adapter | Implement from canonical geometry after runtime geometry behavior is stable |
| Deferred | Overlay | Post-port clean implementation candidate | Do not port the current production kernel or loader |

Control-flow instructions (`choice`, `case`, `if`, `loop`, function calls,
input/output, and LOD) belong to Phoenix runtime/migration work rather than the
geometry-kernel queue. Their expression-dependent modes are blocked on the
scripting contract below. The deprecated `share` instruction is excluded from
both queues.

## Merge Production Boundary

Trusted behavioral sources:

- `commands/merge_command.h`
- `loaders/merge_loader.h`
- `loaders/binary_loader/binary_merge_loader.h`
- `backend/operations/merge_faces.h`
- `backend/operations/merge_borders3.h`
- `backend/operations/join_vertexs.h`
- `backend/operations/cleanup_face.h`
- `vm/variant.cpp` and `vm/threads.cpp`, only to recover aggregation order and
  the behavior hidden behind `subdivide`

The user-facing production instruction exposes five switches:

| Option | Production behavior | Phoenix port slice | Principal risk |
|---|---|---|---|
| `mergeBorders` | Rebuilds multiple input faces into one 3D mesh and reconciles coincident borders | M1 | Directed edge labels, vertex reuse, non-manifold or duplicate facets |
| `joinVertexs` | Allows coincident vertices to share identity while aggregating borders; production tolerance defaults to `1e-5` | M2, layered on M1 | Stable ID ownership and accidental welding |
| `mergeFaces` | Removes compatible boundaries between coplanar/compatible mesh faces | M3 | Face-label selection, holes/thin faces, invalid topology |
| `mergeFacesLabels` | Changes whether labels constrain face merging | M4 policy on M3 | A surviving face must have deterministic label provenance |
| `joinColineal` | Simplifies collinear face-boundary vertices through `cleanup_face3` | M5 | Both directed halfedge labels must match; antenna and tolerance behavior |

The legacy `method == "edges"` spelling defaults both `mergeBorders` and
`joinVertexs` to true. Migration must preserve that persisted shorthand, but the
Phoenix runtime configuration should store the resolved booleans explicitly.

### Scope and semantics

- Port only the 3D mesh paths. The `merge_faces` arrangement overload,
  `merge_borders.h`, and `cleanup_face2` are excluded from runtime scope.
- Merge is producing and consuming: on complete success, every source face used
  to construct the merged result is replaced. A failed item consumes nothing.
- Inputs and outputs cross the instruction boundary only as canonical 3D
  geometry. A CGAL mesh is invocation-local working geometry.
- No existing canonical object or label record is mutated. Results receive new
  run-scoped element IDs except where an audited one-to-one survivor rule
  explicitly preserves an identity.
- Directed halfedge labels are first-class. Simplification may remove a vertex
  only when the production predicate's current and opposite labels permit it.
- Temporary production tags and negative sentinel IDs are kernel-local and must
  not escape into canonical geometry.

### Recommended implementation sequence

1. M0: localize a production merge oracle and record source hashes/minimal
   compatibility edits.
2. M1: port 3D border aggregation without vertex welding or face merging.
3. M2: enable production vertex joining behind its own option and fixtures.
4. M3: port mesh face merging with label matching enabled.
5. M4: add the `mergeFacesLabels` policy variants and deterministic survivor
   label rules.
6. M5: add collinear cleanup with directed-label preservation.
7. M6: compose option combinations in the Phoenix handler, publication ledger,
   cache, partial rerun, and platform suites.

Each slice requires production fixtures before integration. Initial fixtures
must include disconnected faces, exactly shared borders, near-coincident
vertices on both sides of the tolerance, same/different face labels,
same/different directed edge labels, collinear boundary vertices, duplicate
facets, open borders, and a deliberate invalid/non-manifold failure.

M0 source hashes, compile-boundary rules, excluded 2D paths, fixture order, and
known risks are recorded in `PORTING_MERGE_SOURCE_MAP.md`.

## Remaining Capability Notes

### Select

Production traverses faces or directed edges, routes matches to named outputs
or `else`, and can apply seeded count/range/step and percentage limits. The
Phoenix version is non-consuming and 3D-only. Selection must not mutate the
source geometry or labels.

### Rename

Production supports manual maps and conditional face/edge renaming by length,
opposite labels, border state, expression, and edge count, including
largest/smallest/any relations. Port simple label maps first. Conditions that
depend on V8 expressions must be isolated from the geometry/label core and may
be deferred to migration/runtime expression work. Renaming creates a new
canonical geometry value while preserving stable element IDs; label registry
definitions remain immutable.

### Smooth

Production contains two materially different kernels: OpenSubdiv subdivision
and hard-edge rounding. OpenSubdiv carries face and edge labels through
face-varying channels; hard-edge rounding depends on `merge_faces`,
`join_vertexs`, label metadata/classes, angle thresholds, and smoothing groups.
This makes merge a prerequisite for the hard-edge mode, but not necessarily for
basic subdivision.

### Profiles

Profiles are a core persisted asset, second only to labels. A profile needs a
stable asset identifier and immutable definition; resolving or evaluating it
must never mutate that definition or change what its ID means during a run.
Cache identity must include the profile asset version/fingerprint and all
evaluation inputs.

The existing Phoenix extrusion profile already owns the resolved,
kernel-facing representation. Remaining production value is the asset and
evaluation layer: interpolation, repeat expansion, Bezier tessellation,
variable resolution, explicit sign, validation, deterministic randomness, and
migration from stored production descriptors. These should be ported as
deterministic value preparation rather than as exact geometry stored in the
runtime.

Profiles resolve labels through stable `LabelId` values. They must not depend on
styles or materials. If those asset systems are retired, profile geometry and
label behavior remain complete; if they survive, they attach only as optional
metadata outside the kernel-facing profile contract.

### External instancing

Production places external 3D assets on input faces using face or axis-aligned
orientation, an optional directed edge label, bounding-box or centroid
position, randomized transforms, attributes, and an optional `remove_input`
flag. Asset loading belongs behind an adapter; source consumption follows only
the resolved `remove_input` behavior.

### Scripting and expressions

Scripting is a major cross-cutting runtime subsystem, not an incidental command
adapter. Production expression behavior is used by control instructions and by
geometry/value preparation, including `if`, `choice`, `case`, loop conditions,
select predicates, conditional rename, profile variables, randomized ranges,
instance transforms, and attribute evaluation.

Production V8 code is behavioral evidence, not an implementation requirement.
Before selecting an engine, Phoenix must define:

- the available scalar, label, geometry-metadata, attribute, and collection
  value types
- variable scope, function inputs, and instruction-local bindings
- conversion, comparison, missing-value, and error semantics
- deterministic random access through the Phoenix seed contract
- cache identity for source text, compiled form/version, bindings, and seeds
- execution budgets, interruption, recursion/loop limits, and memory limits
- filesystem, network, clock, process, and host-object isolation
- thread-safety and whether compiled programs may be shared across workers
- stable diagnostics and migration behavior for unsupported production APIs

Port scripting in two stages. First implement the engine-independent expression
contract and fixtures. Then bind the chosen sandboxed engine and enable
script-dependent instruction modes one family at a time. Simple non-scripted
select, rename, profiles, and instancing may ship earlier; their expression
variants remain explicitly unsupported until this phase passes its exit gate.

### Exporters

The Three.js exporter is a persistence/output boundary, not a geometry kernel.
Reimplement it against canonical geometry and stable labels/material metadata;
do not port VM variants or production mesh ownership.

## Deferred Overlay

Production overlay sources and loaders remain useful only as defect and feature
inventory. They must not become a dependency of another P11 port. A future
post-port overlay effort starts with a fresh contract and fixture corpus, then
selects or implements an appropriate modern algorithm independently of the
current production code.

## Deprecated Share Instruction

The production `share` command and loader are retained only as migration-input
evidence. Phoenix must not reproduce their shared geometry handles or mutable
face-list views. A migrated project containing `share` receives a specific
unsupported/deprecated-instruction diagnostic. If a project needs equivalent
fan-out, it must use Phoenix immutable geometry values and ordinary graph edges.
