# Inset Production Source Map

Authoritative production root used for the audit:
`C:/Users/emili/source/repos/threedee.io/threedee.solver`.

## Accepted Oracle Sources

| Production source | Phoenix source | SHA-256 | Treatment |
| --- | --- | --- | --- |
| `backend/inset/inset.h` | `include/phoenix/inset/ported/production/inset.h` | `8A994A93680F9C55C8DC69142A08ECD9730A16D1BCA2D8A21D345A5FCB155C33` | Geometry and diagnostics includes localized; unused custom straight-skeleton include replaced by direct CGAL headers. |
| `backend/inset/inset.cpp` | `src/inset/ported/production/inset.cpp` | `5ABEF30B89EC7179D63427007DB040DDC3D6A630FE6889BBF412145B73C9940E` | Algorithm body retained; prelude, geometry operations, cleanup, and null diagnostics includes localized; timer include removed. |
| `backend/operations/cleanup_face.h` | `include/phoenix/inset/ported/cleanup_face.h` | `56CF4D40BA647055640A3BD09A876CA2404F2CDC9EAE582E9F933FFBEE4C0A9E` | Body retained; geometry support includes localized. |

The production command, VM, mesh conversion, merge operations, custom legacy
straight-skeleton implementation, SVG output, timer, and `debug_json` are not
part of the inset kernel oracle. The active inset implementation calls CGAL's
interior straight-skeleton API directly.

## Observable Production Contract

- Input is one bounded arrangement face and a strictly positive inset amount.
- Production cleanup uses `degen_epsilon = 0.0001`, label-aware collinear
  merging, and antenna removal.
- The deterministic compatibility path calls `inset::run(..., false)` and does
  not apply the legacy `rand()` perturbation.
- A successful convex rectangle produces one result/center face and one side
  face per source boundary edge.
- Result and side faces are tagged separately.
- Current and opposite halfedge labels have distinct roles. Side boundaries
  use bottom/left/right/top labels; center boundaries use `result_edge` or
  inherit the original directed boundary label.
- `result_face` and `side_face` override inherited face labels when assigned.
- The instruction consumes and replaces its source face only after success.

## Deferred Extrusion Reuse

The production algorithms share the same wavefront/profile concept. The audit
found that production stores the governing profile sign explicitly, while the
initial Phoenix profile boundary inferred it only from nonzero `delta_y`. That
Phoenix-only restriction rejected valid horizontal-only profiles. Explicit
profile sign construction is restored for inset and other horizontal profiles;
existing callers retain sign inference. Replacing production inset with the
extrusion kernel is explicitly deferred until after the production inset port
and broader corpus compatibility baseline are complete. The current P10 runtime
must use the localized production inset algorithm.

## Compatibility Corpus

- exact production rectangle: one center face, four side faces, explicit labels
- canonical tilted rectangle: projection/lift, directed labels, and replacement
- canonical tilted concave L-face: one center face and six side faces
- unset label parameters: source face and source directed-edge inheritance
- invalid zero amount: failure without source consumption
- executor publication: source face replaced on its call-path actor
- cache replay: replacement/effects replay without another kernel invocation
- partial rerun: cached replacement is stable and a changed failure restores the
  original source face
