# Select Port Record

## Contract

Select is non-mutating and non-consuming. It routes immutable canonical 3D face
copies while preserving face, vertex, halfedge, edge, and label IDs. It emits no
publication consumption effects. Legacy shared mutable geometry is excluded.

## Production Evidence

- `commands/select_command.h/.cpp`: traversal, first-match label routing,
  all-condition matching, limits, shuffle, percentage conversion, and `else`
- `loaders/select_loader.cpp` and binary equivalent: label, `hasEdge`,
  `hasEdgeByLength`, `hasEdgeByOpposite`, and `hasBorderEdge` predicates

Only production's 3D semantics are implemented. No arrangement geometry is
created or persisted.

## Implemented Face Selection

- first-match face-label routes to named ports
- conjunction of face label and edge-existence conditions
- edge label, opposite face label, opposite directed-edge label, border state,
  and inclusive edge-length bounds
- deterministic count, random range/step, and percentage limits
- unmatched and over-limit faces route to `else`
- one geometry or an accumulated geometry collection as input

Traversal starts in canonical contribution order and face-index order. A
positive limit applies the production seeded shuffle before matching.

## Directed-Edge Selection

Production can return directed edge lists. Phoenix has stable `HalfedgeId`
values but no runtime value/reference type for a directed halfedge. Edge
selection is intentionally not represented as an incident face or copied
geometry, because that would lose direction and change semantics. Add a stable
immutable directed-halfedge reference/value before enabling this mode. Vertex
selection was also unimplemented in production and is outside the port.

## Deferred With Scripting

Expression predicates remain blocked on the scripting contract. They will
compose with this routing/limit layer rather than mutate its ownership model.
