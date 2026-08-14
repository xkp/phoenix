# Production Script/CGAL Binding Inventory

## Trusted Sources

| Source | SHA-256 | Role |
|---|---|---|
| `threedee.v8/v8_api.cpp` | `73C99294E9ED72FB24EC510BCA268F20533C938B534E76EDC792946B8C7CA42F` | global JavaScript API, context, input/output collection |
| `threedee.v8/visitor.h` | `20D657778E76FCE8247BE7AB24D6C94B2D7F00245128499748A793E0E8F134A6` | object properties/methods and native wrapper recovery |
| `threedee.v8/op_cgal_mesh.cpp` | `6E63B209B0DEAD86DFE32E3F7A66E5C58D282FA32EB2D83327426FDC003318C7` | mesh inspection, labels, construction, topology mutation |
| `threedee.v8/op_cgal_3d.cpp` | `254926DF2D8466E725CF669A2824A55377233B965DAC314A2ECFCC6D71F575DC` | exact 3D primitives and predicates |
| `threedee.v8/op_boolean.cpp` | `FB0926DBCBA02E663A428D524645CFA3E24E66BEBC25E8253E3B03C3D3014686` | mesh boolean operations |

This project is a compatibility oracle and implementation source. Its V8
templates, native pointer wrappers, singleton isolate, and old CGAL handle
lifetimes are not ported.

## Compatibility Tiers

- **Preserve**: keep the JavaScript name and behavior in the first geometry
  script API.
- **Adapt**: preserve observable behavior through Phoenix handles or temporary
  working geometry; do not expose the original native object.
- **Defer**: valuable but requires a separately versioned kernel/capability.
- **Drop**: deliberately unsupported with a migration diagnostic.

## Global Surface

| API | Decision | Phoenix boundary |
|---|---|---|
| `point3`, `segment3`, `dir3`, `line3`, `vec3`, `triangle3`, `ray3`, `plane3`, `transform3` | Preserve/adapt | invocation-local immutable value handles; exact predicates may upgrade internally |
| `build_mesh` | Preserve | transactional canonical halfedge builder |
| `normal` | Preserve | canonical/working geometry utility |
| `intersection` | Adapt | typed 3D result variants behind host |
| `boolean_union`, `boolean_difference`, `boolean_intersection` | Adapt | temporary CGAL worker, validated canonical output |
| `abs`, `round`, `pow`, `sqrt`, `ceil`, `floor`, `sin`, `cos`, `tan`, `sign`, `equals`, `PI` | Preserve | engine or deterministic host math contract |
| `debug_print` | Preserve with bound | bounded diagnostic console |
| `constants` orientation/side values, `transforms` identity/translation/rotation/scaling | Preserve/adapt | stable Phoenix enum/value objects |
| `insert` | Defer pending overload audit | split into explicit typed operations if retained |
| `overlay` | Drop from port | post-port overlay design only |
| all `point2`/curve/arrangement constructors and 2D side constants | Drop | no 2D geometry |

## 3D Primitive Surface

Preserve the following names and semantics, subject to production differential
fixtures:

- point: `x`, `y`, `z`, `transform`, `add`, `sub`
- segment: `source`, `target`, `min`, `max`, `vertex`, `squared_length`,
  `to_vector`, `direction`, `opposite`, `supporting_line`, `is_degenerate`,
  `has_on`, `transform`
- direction: `dx`, `dy`, `dz`, `vector`, `transform`
- line: `is_degenerate`, `has_on`, `projection`, `perpendicular_plane`,
  `opposite`, `to_vector`, `direction`, `transform`
- vector: `x`, `y`, `z`, `direction`, `transform`, `add`, `sub`, `mult`, `div`
- triangle: `is_degenerate`, `has_on`, `squared_area`, `supporting_plane`,
  `vertex`, `transform`
- ray: `source`, `direction`, `to_vector`, `supporting_line`, `opposite`,
  `is_degenerate`, `has_on`, `transform`
- plane: `a`, `b`, `c`, `d`, `opposite`, `point`, `orthogonal_vector`,
  `orthogonal_direction`, `base1`, `base2`, `is_degenerate`, `has_on`,
  `has_on_positive_side`, `has_on_negative_side`, `perpendicular_line`,
  `projection`, `transform`
- transform: `inverse`, `is_even`, `is_odd`, `transform`, `m`

The JavaScript-visible values do not promise CGAL exact-number objects. Numeric
results are doubles unless the compatibility corpus proves a script-observable
need for another representation. Predicate implementations may use exact
working kernels internally.

## Mesh And Topology Surface

### Preserve inspection

- mesh: `empty`, `size_of_vertices`, `size_of_halfedges`, `size_of_facets`,
  `vertices`, `halfedges`, `faces`, `is_closed`, `is_pure_bivalent`,
  `is_pure_trivalent`, `is_pure_triangle`, `is_pure_quad`, `is_triangle`,
  `is_tetrahedron`, `size_of_border_halfedges`, `size_of_border_edges`,
  `border_halfedges`
- vertex: `halfedge`, `incident_halfedges`, `vertex_degree`, `degree`,
  `is_bivalent`, `is_trivalent`, `point`, `x`, `y`, `z`, `index`
- halfedge: `opposite`, `next`, `prev`, `next_on_vertex`, `prev_on_vertex`,
  `is_border`, `is_border_edge`, incidence lists/degrees, `is_bivalent`,
  `is_trivalent`, `is_triangle`, `is_quad`, `vertex`, `facet`, `label`
- face: `halfedge`, `incident_halfedges`, `facet_degree`, `is_triangle`,
  `is_quad`, `label`

Returned vertex/halfedge/face references are generation-checked opaque handles.
Any topology mutation invalidates handles according to a documented generation
rule; stale access fails deterministically rather than dereferencing CGAL
iterators.

### Preserve/adapt mutation

- builder: `add_vertex`, `add_face`, `error`
- mesh: `split_facet`, `join_facet`, `split_vertex`, `join_vertex`,
  `split_edge`, `flip_edge`, `create_center_vertex`, `erase_center_vertex`,
  `make_hole`, `fill_hole`, `add_vertex_and_facet_to_border`,
  `add_facet_to_border`, `erase_facet`, `erase_connected_component`,
  `keep_largest_connected_components`, `clear`, `normalize_border`,
  `inside_out`
- writable `face.label` and `halfedge.label`

These run only against the invocation-local edit session. Each operation must
specify label propagation before implementation; production behavior is the
default oracle. The final mesh must pass canonical topology and stable-label
validation before publication.

### Drop or replace

- `debug_export_file`: drop; scripts have no filesystem access
- capacity and memory properties (`capacity_of_*`, `bytes`, `bytes_reserved`):
  drop because they expose an implementation detail
- `set_halfedge` on vertex/face: replace with safe topology operations; direct
  representative-pointer mutation can violate invariants
- raw native equality/pointer identity: drop; handles expose stable equality
  only within one invocation and generation

## Host Objects And Outputs

The production global is `td`:

| JavaScript surface | Production behavior | Phoenix decision |
|---|---|---|
| `td.vars.<name>` | read-only double function variables | Preserve name; expose typed immutable bindings |
| `td.labels.<name>` | read-only integer label index | Preserve name; expose stable Phoenix `LabelId`, never traversal/array position |
| `td.inputs` | returns the first constructed input object only | Fix defect: keyed object containing every named input; migration alias `td.input` may address the first input |
| input `.mesh` | copied 3D polyhedron wrapper or `undefined` | Preserve through read-only invocation handle |
| input `.faces3`, `.edges3` | selected element lists or `undefined` | Preserve as generation-checked handle lists |
| input `.arrangement`, `.faces2`, `.edges2` | 2D values | Drop with explicit no-2D migration diagnostic |
| `td.log(value)` | appends text to collector console | Preserve with byte/entry limits |

Production constructs one input object per socket and connects each internally
by socket name, but the `td.inputs` accessor returns `_script_inputs[0]`. This
is recorded as a production defect, not desired compatibility behavior.

The script body is wrapped as an immediately invoked `main` function. Its return
value is interpreted as a named-output object. Production accepts 3D vertex,
edge/halfedge, or face handles and homogeneous arrays of those handles. Empty
arrays, mixed arrays, scalar properties, and whole 3D polyhedron properties are
rejected. Separately, every geometry created by builders/booleans is appended
to a hidden global collector and published as geometry, even when not returned
as a named output.

Phoenix preserves named element/list outputs and explicit geometry outputs but
removes the hidden collector side effect: creation alone does not publish.
Scripts must explicitly return or emit each output geometry. Version one also
allows named scalar outputs because Phoenix's graph has typed scalar ports;
migrated production scripts remain compatible with their narrower output set.
All output ports are validated against the instruction descriptor before
commit.

Includes execute in stored list order before the main body, inside the same
sandbox/context. Phoenix represents each include as a versioned immutable
library asset `{id, version, content fingerprint, source}`. Resolution occurs
before execution; ordered identities and sources participate in cache identity.
Duplicate library IDs, missing assets, or dependency cycles fail before any
geometry session begins.

## S1G Work Order

1. Complete `td` input/output/collector and include API inventory. **Complete.**
2. Add opaque handle and invalidation semantics to `GeometryScriptHost`.
   **Foundation complete:** session/geometry/generation/index handles over an
   invocation-local CGAL `Surface_mesh`, canonical promotion/demotion,
   vertex/face/halfedge lookup, point and label edits, topology-generation
   invalidation, atomic canonical commit, and rollback. CGAL materialization is
   lazy on first inspection/mutation; untouched clones reuse their canonical
   source directly at commit.
3. Add primitive-value and mesh-inspection conformance fixtures. **Mesh slice
   complete:** lazy CGAL-backed collection/count queries, boundary inspection,
   face and vertex incidence traversal, source/target/facet lookup, degrees,
   bivalent/trivalent and triangle/quad predicates, and production directed
   halfedge count semantics are covered. The primitive-value foundation is now
   implemented independently of any JavaScript engine: lightweight 3D
   point/vector/direction/segment/line/ray/triangle/plane/transform values,
   affine composition and inversion, derived construction operations, and
   CGAL exact-predicate-backed containment/orientation checks. Remaining
   production primitive overloads remain. The production 3D intersection
   matrix is complete for line/line, line/plane, line/segment, plane/plane,
   plane/segment, and segment/segment in both argument orders. Results are a
   typed `none | point | line | segment | plane` value rather than the old
   loosely populated JavaScript object, and exact CGAL construction is scoped
   to each intersection call.
4. Port builder plus label mutation fixtures. **Core complete:** lazy CGAL mesh
   creation, stable vertex/face/halfedge/edge IDs, oriented polygon insertion,
   face labels, directed-edge labels, and CGAL rejection of duplicate,
   conflicting, or non-manifold faces. Additional production builder overloads
   remain corpus work.
5. Port topology mutations one operation at a time with production differential
   fixtures and explicit label propagation. **First slice complete:** safe face
   traversal plus CGAL-backed `split_edge`, `split_facet`, and `join_facet`,
   including generation invalidation, stable IDs, production-default unassigned
   labels for new elements, and retained-face label preservation. The second
   slice adds CGAL-backed `flip_edge`, `make_hole`, and `fill_hole` with checked
   Euler preconditions. Hole filling creates an unassigned face label as in
   production. Public mesh counts enumerate live descriptors rather than
   exposing `Surface_mesh` removed slots before garbage collection.
   The third slice adds `split_vertex`, `join_vertex`,
   `create_center_vertex`, and `erase_center_vertex` through their CGAL Euler
   equivalents. New topology receives run-scoped IDs, new labels remain
   unassigned, and the original face label survives center triangulation and
   collapse through production-compatible descriptor attachment.
   The final management slice adds the CGAL-backed border growth operations,
   facet and connected-component erasure, largest-component retention, clear,
   and orientation reversal. `normalize_border` is retained as a compatibility
   no-op: it only partitioned legacy `Polyhedron_3` physical iterator storage,
   while Phoenix exposes opaque descriptors and explicit border collections.
6. Add boolean/3D intersection workers behind exact-kernel adapters. **3D
   intersections complete; boolean compatibility worker deferred to post-port
   and does not gate corpus-port completion:** production
   uses an exact `Nef_polyhedron_3` pipeline followed by bespoke,
   tolerance-based label transfer. That transfer contains an apparent
   `else if (is1)` typo where `is2` was intended, so it cannot become Phoenix's
   stable-label contract without real-project differential evidence.
7. Add rollback, invalid topology, stale handle, cancellation, and budget tests.
8. QuickJS-NG 0.16.1 is selected and the concrete adapter passes the
   common scalar, isolation, interruption, cancellation, and memory-budget
   corpus. Ordered libraries, immutable variables and labels, keyed input mesh
   metadata, the first-input compatibility alias, named scalar return decoding,
   and atomic host finalization are implemented. Complete the scorecard with
   The first opaque geometry bridge now covers creation/cloning, vertex edits,
   polygon construction, stable face/directed-edge labels, face removal, typed
   element collections, explicit geometry/element returns, rollback, and stale
   token rejection. Mutable readback exposes counts, point snapshots, face
   loops, and stable face/directed-edge labels. Bounded `td.log` collection is
   complete at 1,024 entries/64 KiB per invocation. Finish the production
   All mutation methods already implemented by the CGAL edit session are now
   projected through opaque JavaScript handles, including Euler, hole, border,
   component, clear, and orientation operations. Finish the production
   incidence/predicate inspection projection is now complete through
   `td.inspectGeometry(mesh)` and `td.inspect(element)`: mesh counts, borders,
   closure/purity predicates, stable IDs and labels, points, incidence rings,
   degrees, and halfedge navigation all resolve through generation-checked
   host handles. The 3D primitive bindings and Linux/macOS debug/release CI
   matrix are complete.
   The first engine-facing 3D slice now binds point/vector/direction,
   segment/line/plane constructors and the complete existing exact intersection
   matrix as frozen lightweight JavaScript values. Ray, triangle, transform,
   transform factories, arithmetic/transformation methods, exact containment,
   opposites, area/length values, supporting/perpendicular constructions,
   projections, plane basis/side operations, plane transforms, transform
   composition/inverse/matrix access, math aliases, and constants are bound.
   The recorded 3D primitive inventory is complete; real-project differential
   fixtures remain the authority for undocumented overload quirks.

## Graph Instruction Checkpoint

The `script` instruction now executes through the normal `FunctionExecutor` for
scalar and whole-geometry inputs and outputs. It uses the execution seed and
run element-ID allocator, retains actor ownership, finalizes geometry atomically,
and propagates stable script diagnostics as instruction failure. Vertex,
halfedge, and face outputs now use the first-class runtime
`ElementSelectionValue`, which carries an owning canonical snapshot, explicit
element kind, and stable IDs. The backing geometry and selection commit
atomically, and cache identity includes kind and IDs. Selection-valued inputs
are projected back as typed opaque vertex, halfedge, or face arrays backed by
the owning canonical snapshot; CGAL promotion remains lazy until requested.

## Invocation Host Checkpoint

The engine-neutral `InvocationGeometryHost` now bridges the script contract to
the transactional CGAL edit session. It exposes every named input through the
keyed `td.inputs` model, validates declared output names and kinds, requires
explicit geometry publication, and never publishes geometry merely because a
script created it. Geometry clones remain lazy through finalization.

Element values crossing the engine boundary are host-issued opaque tokens, not
CGAL descriptors or vector positions. Finalization resolves current tokens to
stable vertex, halfedge, or face IDs before closing the session and rejects
foreign, duplicate, mixed-kind, or stale tokens. Geometry validation and all
named outputs commit atomically; failure rolls the invocation back.
