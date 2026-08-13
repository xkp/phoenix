# Smooth Port Record

## Two Independent Production Modes

Smooth is not one algorithm. Phoenix preserves two separately testable modes:

1. OpenSubdiv uniform subdivision
2. production hard-edge rounding

They share an instruction name only for legacy persistence. Their adapters,
fixtures, versioning, and failure boundaries remain independent.

## Trusted Sources

| Source | SHA-256 | Role |
|---|---|---|
| `commands/smooth_command.cpp` | `354BB9371C59111CFEF1DCC4C5D4B08B3FD99CC6D4E843BAFF07EEC252AC1AED` | OpenSubdiv topology adapter, interpolation, output builder, and command composition |
| `commands/smooth_command.h` | not copied | Persisted option structure and two-mode boundary |
| `backend/operations/hard_edges.h` | `B2E937B27713910C5A8B6C9AB4BCE000875CD6D528DD4D3BCB5928868FA96F7B` | Hard-edge geometry algorithm |
| `loaders/smooth_loader.h` and binary equivalent | behavioral evidence | Defaults and persisted spelling |

Production algorithm bodies are ported with minimal compatibility changes.
Commands, VM values, mutable context, materials, and styles are not ported.

## Subdivision Scope

Persisted options:

- scheme: bilinear (`linear`), Catmull-Clark (`catmark`), or Loop
- maximum refinement level, production loader default 2
- boundary interpolation: none, edge-only, or edge-and-corner
- face-varying interpolation modes
- connect input geometry and connect boundary
- optional non-planar quad triangulation
- output smoothing group

The production implementation uses uniform refinement with full topology in
the final level and interpolates vertex positions through OpenSubdiv's
`PrimvarRefiner`. Phoenix therefore declares OpenSubdiv as a direct dependency;
it will not substitute an unrelated subdivision implementation.

Production assigns each refined face the label of its coarse top-level ancestor.
The apparent directed-edge face-varying label code is commented out in the
production path, so edge-label propagation is not yet claimed. Fixtures must
pin actual production results before Phoenix defines generated edge labels.

## Hard-Edge Scope

Hard-edge rounding depends on the already-ported merge family and selected
vertex-joining helpers. It additionally uses:

- per-face amount, default `0.01`
- minimum/maximum dihedral angles, defaults 15/110 degrees
- label smoothing metadata/classes and smoothing groups
- production tolerances `JOIN_DISTANCE=0.0005` and `COLINEAR_TLR=0.001`

Profiles, styles, and materials must not become required dependencies. If label
classes or smoothing masks survive, they enter through an immutable Phoenix
label-metadata view.

## Ownership And Precision

Both modes are producing and consuming: complete success replaces all source
faces used by that item; failure consumes nothing. Canonical input/output is 3D
double geometry. OpenSubdiv uses double vertex primvars. Exact CGAL geometry,
where required by the hard-edge production kernel, is invocation-local.

All generated elements receive run-scoped IDs. Source IDs are preserved only
where an audited one-to-one mapping exists. Labels remain stable integer IDs.

## Implementation Order

1. S0: dependency, source record, defaults, and production fixtures
2. S1: localize OpenSubdiv topology adapter and compile oracle
3. S2: canonical adapter, ancestor face-label policy, IDs, and publication
4. S3: cache, partial rerun, and platform matrix
5. H0: localize hard-edge kernel and dependency audit
6. H1: label metadata/amount adapter and focused oracle
7. H2: canonical integration, transactional publication, and platform matrix

No legacy 2D arrangement geometry is in scope.

## Current Checkpoint

S0-S2 kernel integration is implemented in `phoenix::smooth::subdivide` using
OpenSubdiv's `TopologyRefiner` and `PrimvarRefiner`; no replacement smoothing
algorithm was introduced. The canonical adapter accepts arbitrary valid face
loops for bilinear/Catmull-Clark and requires triangles for Loop. Uniform
refinement uses full final-level topology, walks `GetFaceParentFace` to recover
the coarse label, assigns fresh run-scoped IDs to all generated topology, and
publishes through the common working-geometry adapter. A level-zero request is
an immutable identity copy. The initial fixture pins a level-one bilinear quad
at nine vertices/four faces, ancestor face-label inheritance, and production's
current lack of generated directed-edge labels.

S3 runtime integration is complete. The consuming instruction replaces every
face in each successful geometry contribution, keeps ownership with that
contribution's accumulation actor, and emits no consumption or replacement for
a failed item. End-to-end fixtures cover publication, cache replay without a
second kernel invocation, and partial-rerun source restoration after a changed
kernel fails. The Linux GCC and Apple Silicon Clang debug/release matrix is
declared in `smooth-platforms.yml`.
The Linux job explicitly installs the `libxinerama-dev` and `libxxf86vm-dev`
system packages required by the vcpkg OpenSubdiv port before dependency
resolution. Phoenix links only the portable CPU target and enables none of the
optional graphics or accelerator backends.

Remaining subdivision compatibility work is persisted option migration in P12
and localization of production's optional connect-input, connect-boundary, and
non-planar-quad preprocessing. Those flags remain unexposed until production
fixtures pin their behavior.
