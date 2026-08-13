# Profile Asset Port Record

## Boundary

Profiles are immutable, versioned assets. Their stable string ID identifies one
definition for the lifetime of a registry. Registering a different definition
under the same ID is an error; resolving or evaluating a profile never mutates
the asset. Styles and materials are not dependencies.

The asset definition is ordinary scalar data. Evaluation creates the existing
immutable `extrusion::Profile`, which remains the only kernel-facing form. No
exact geometry or persistent 2D arrangement is stored.

## Production Evidence

- `vm/assets.h`: descriptor fields, segment labels, interpolation and repeat
  metadata
- `loaders/profile_loader.cpp` and binary equivalent: persisted defaults,
  label resolution, horizontal-segment behavior, and sign validation
- `vm/profile_builder.h/.cpp`: evaluation order and algorithms

Production evaluation order is preserved:

1. interpolate source and target segments
2. expand repeat intervals
3. resolve named variables
4. tessellate Bezier segments
5. discard empty segments, normalize near-horizontal deltas, and validate sign

The VM/context and mutable descriptor cache are not ported. Expression parsing
is deferred to the scripting contract; this layer receives already-evaluated
named scalar bindings and an explicit deterministic seed.

## Implemented Contract

`phoenix/profiles/asset.hpp` provides:

- stable `AssetId`, positive version, immutable `Definition`, and deterministic
  content fingerprint
- registry ownership that rejects ID/definition drift
- source/target interpolation
- deterministic repeat expansion with explicit seed
- named height, width, length, angle, and global-height bindings
- cubic Bezier tessellation by explicit subdivisions or tolerance resolution
- all six stable segment `LabelId` channels
- explicit sign for horizontal-only profiles and overhang rejection
- evaluation fingerprint covering asset content, interpolation, reference
  height, variables, and seed

The evaluation fingerprint must be included in a consuming instruction's cache
identity. Persisted JSON/binary parsing remains a P12 migration adapter and must
resolve label UIDs through the stable Phoenix label registry.

## Fixtures

- immutable identity and version fingerprint changes
- conflicting registry ownership rejection
- evaluation cache-key changes with deterministic seed
- interpolation, repeat expansion, and Bezier tessellation
- named height/width variables
- horizontal-only explicit sign

## Deferred With Scripting

- evaluation of source expressions and production host APIs
- instruction-specific per-face/per-vertex expression bindings
- sandbox budgets and compiled expression cache

These features consume this API after scripting is defined; they do not change
profile identity or the kernel representation.
