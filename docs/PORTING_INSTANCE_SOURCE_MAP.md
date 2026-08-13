# Instance Port Record

## Trusted Production Sources

| Source | SHA-256 | Role |
|---|---|---|
| `commands/instance_command.cpp` | `0FB780CFFC7242BBA5CB69B85D1CDFEEB137743025202FEBE937BE841F45BF506` | 3D face selection, orientation, placement transforms, and notification |
| `commands/instance_command.h` | behavioral evidence | Persisted options and mode names |
| `loaders/instance_loader.h` and binary equivalent | schema evidence | Defaults and legacy routing |

Production VM callbacks, pipeline annotations, materials/styles, and 2D
arrangements are not port targets.

## Actual Production Boundary

The instruction does not copy model geometry into the solver mesh. It computes
one transform per selected face and emits an external-instance notification
containing model/file identity, name, transforms, backend annotations, and
attributes. Phoenix should therefore create actor placements referencing an
immutable prototype/asset identity rather than clone mutable geometry.

Production options include:

- external model/file and name
- `axisAligned` or `byFace` method
- optional directed-edge orientation label
- `removeInput`
- bounding-box-center or centroid placement
- ranged/stepped X/Y/Z rotation, scale, and translation
- one shared random sample or one sample per face
- legacy pipeline/backend annotations and arbitrary attributes

Only canonical 3D faces are in scope.

## Production Defects And Accepted Handling

- The loader accepts `byFace`, but the active runtime asserts for every method
  except `axisAligned`. Phoenix rejects `byFace` with a migration/runtime
  diagnostic until a new behavior is explicitly designed.
- When an orientation label is configured but absent, production reports an
  error yet continues with a default edge direction.
- For 3D faces, production applies extra rotations only inside the
  `found_orientation_edge` branch. Thus extra rotations are silently skipped
  when no orientation label is requested or found. This is recorded as a defect
  and must be fixture-pinned before deciding whether migration preserves it.
- Production uses the first matching directed edge in face traversal order.
  Phoenix preserves that deterministic rule and stable integer label identity.

## Ownership

`removeInput=false` is non-consuming. `removeInput=true` consumes each placement
face only after every requested placement succeeds. Missing orientation labels,
invalid/degenerate faces, or missing prototype assets publish and consume
nothing for the failed item.

## Slices

1. I0: source record and canonical 3D placement-plan math
2. I1: immutable external prototype identity and actor placement delta
3. I2: handler, optional transactional input consumption, cache, partial rerun
4. I3: deterministic ranged/stepped options and one-seed-each behavior
5. I4: platform matrix
6. Migration participates in P12; no persistence format is selected here

## Completed Checkpoint

I0-I4 are implemented. Instance now emits cached actor children that reference
an immutable external prototype identity; it never clones the prototype mesh
into canonical geometry. The 3D placement planner preserves production's
first-directed-edge label rule and recorded rotation gate. Ranged transforms
are deterministic from execution seed context, and `oneSeedEach` selects
shared versus per-face samples. `removeInput` is an explicit transactional
empty replacement, while all planning failures leave input geometry intact.
Linux and Apple Silicon CI cover both placement and instruction suites.

Project-file migration remains P12 work. `byFace`, 2D geometry, materials,
styles, VM callbacks, and backend annotations remain outside this port.
