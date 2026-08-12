# Preserved Production Extrusion Snapshot

This directory is an unmodified source snapshot of the audited production
profile-extrusion kernel. It is quarantined from the normal Phoenix build while
the narrow compatibility layer is constructed.

Source root:

`C:\Users\emili\source\repos\threedee.io\threedee.solver\backend\extrusion`

Captured for Phase P6 on 2026-08-11:

| File | SHA-256 |
| --- | --- |
| `extrude.h` | `2D32E064A115FA369539C86771914E31B35752B56D7F145A289AEA33B4575DC4` |
| `extrude_plan_builder.h` | `4768DA5975C0CB9F857EBC0AE476471627B32039968A3D0059E1246905FE7297` |
| `extrusion_errors.h` | `DA97D2DD9EB08425F553D926C6345D65C8DA6F212FE4413EB9202318C3118DCB` |
| `extrusion_errors.cpp` | `D2E96D1DB05A5EB65007516703724E92E9AF655B37C811663841B8ACB244AC1E` |

The snapshot is evidence and the behavioral baseline. Do not edit it during
the initial port. Adapted source belongs under `src/extrusion`; every change
from this snapshot must be classified as mechanical compatibility,
Phoenix-boundary adaptation, or an explicitly approved algorithmic deviation.

The snapshot is not compiled directly because its apparent includes hide
dependencies on production-wide macros and types from `geometry.h`, including
the exact arrangement, custom polyhedron data, geometry utilities, exception
plumbing, debug exporters, and legacy profile interface. Those dependencies
are being replaced by narrow Phoenix compatibility types rather than copied
wholesale.
