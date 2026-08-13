# Rename Port Record

## Contract

Rename creates a new immutable canonical 3D geometry value. It preserves all
topology and every `VertexId`, `HalfedgeId`, `EdgeId`, and `FaceId`; only face
and directed-halfedge `LabelId` values may change. The source and label registry
definitions are never mutated. Rename is non-consuming.

## Production Evidence

- `commands/rename_command.h/.cpp`: manual maps, seeded multi-destination
  selection, face/edge condition passes, global fallbacks, and
  `@toItsFaceLabel`
- `loaders/rename_loader.cpp` and binary equivalent: persisted condition modes
  and bindings

Production's mutation of a subdivided working copy is expressed directly as an
immutable canonical copy. Only 3D behavior is retained.

## Implemented Non-Scripted Behavior

- manual label maps for faces and directed edges
- multiple destination labels selected by deterministic seed
- `allFaces` and `allEdges` fallback labels
- face and directed-edge condition targets
- source label, owning face label, owning edge label, opposite face label,
  opposite directed-edge label, and border predicates
- inclusive length ranges and per-face `any`, `largest`, and `smallest` modes
- maximum face edge-count condition
- destination label inherited from the owning face (`@toItsFaceLabel`)
- one canonical geometry or an accumulated collection

Directed halfedges are renamed independently. Changing one direction never
implicitly changes its opposite label.

## Deferred With Scripting

Expression conditions and production binding host APIs remain blocked on the
scripting contract. More specialized production relation modes that depend on
those global binding passes (`best fit`, expression-driven own/all selection)
will be enabled against this immutable core after that contract exists.

Persisted label UIDs and legacy option spelling are P12 migration concerns.
