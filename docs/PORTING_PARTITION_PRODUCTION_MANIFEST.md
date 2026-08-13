# Production Partition Source Manifest

Authoritative root used for the compatibility spike:
`C:/Users/emili/source/repos/threedee.io/threedee.solver`.

The production checkout is a read-only comparison oracle. The compile probe
now consumes only localized files beneath `partition/ported`; it has no build
or include dependency on the external checkout.

The complete set is now copied beneath `partition/ported/production` and
`src/partition/ported/production`. Normalized content identity was verified for
all nine files before the first adaptation, and the compile probe now targets
the local copies. The external checkout remains the immutable comparison oracle.

| Source | Lines | SHA-256 |
| --- | ---: | --- |
| `backend/segment_repository.h` | 488 | `AC2A64BE0F47B6662DC5320A1635F2C1F7916BA003730613D653EA79B62D2C16` |
| `backend/partition/partition_solver.h` | 1305 | `F642705894F2FF886F3CE9A4E94F9747411AA90D0C33C0C60E7344ED29D948AB` |
| `backend/partition/partition_solver.cpp` | 1677 | `01A72A1A6A6D7A7C2D0F2397CF01CBF5A5E9CA1EEBFA16320B3B6B83DB6BE13D` |
| `backend/partition/partition_solver_constraints.h` | 85 | `09C15D4CECD68E70BC8FD091A73199CB80267341FFBB18CCEB618A630D46090D` |
| `backend/partition/partition_solver_constraints.cpp` | 901 | `EF8B1830997917196EC29C5D3BE2F493C31410CD15A39F39E0641890A5D905FE` |
| `backend/partition/partition_solver_filters.h` | 100 | `B9EA13EC61D3A204E10950DC25F1C22E6D20E3A9A611338FEB6AA5350315118F` |
| `backend/partition/partition_tesselator.h` | 245 | `704F68763130DC2BB97F92569D65BB21408230FBC02DF7F91BFA7D9DFF459CFC` |
| `backend/partition/partition_tesselator.cpp` | 1703 | `0EF7CDD235CF070EBC8A36EF6AF8CCF6552DC3ED413DC33562F38605FB03662F` |
| `backend/partition/partition_errors.h` | 43 | `88FF56FCFBA27753A38F0B86E42F50F69792BDE78E433FAF2FD185A3107C1E5B` |
| `backend/partition/partition_errors.cpp` | 75 | `65DBE35D49CB182636A6631A7726B99FF8EA25DAFD90BD59B9D1EBD3177F6FB6` |

Partition also reaches the following production support headers. Their source
hashes were captured before localization:

| Support source | SHA-256 | Phoenix treatment |
| --- | --- | --- |
| `backend/geometry_constants.h` | `F6C6089ACA1C7370B0E4EADF69D706F1E2DD219595CB6534A3E74DD69917F591` | Copied locally unchanged apart from line endings. |
| `backend/geometry_types.h` | `F2F63BCDBD01C13D286FE9E61AFDE2674C13F82C0F043484488291FCF79CC943` | Copied locally unchanged apart from line endings. |
| `backend/geometry_utils.h` | `C0EFF079BD92BCAD887DD6401600FEB2CAA26C5E4B392410B44F266D282F2D68` | Geometry bodies retained; SVG/file diagnostics removed and includes localized. |
| `backend/error.h` | `AB6E156C2184A866406DEED4F291CCA975ABF980CEA09B695791C93F76C86E04` | Copied locally unchanged apart from line endings. |
| `solver_errors.h` | `117E10A3E94390ED79BA1E1E911439E3E32F80688F8160CA2AE13B356162C804` | Copied locally with its error include localized. |

`backend/timer.h` and `backend/simple_svg_1.0.0.hpp` are not reachable algorithm
dependencies. All uses were diagnostic or commented, so they were dropped at
the same diagnostics boundary as `debug_json`.

## Compile-Probe Policy

`phoenix_partition_production_probe` deliberately compiles the complete
production `.cpp` set together. It is excluded from normal builds and exists to
turn the legacy dependency surface into concrete compiler evidence. Failures in
VM, diagnostics, error plumbing, geometry helpers, Bezier utilities, allocation,
or CGAL APIs are adapter tasks—not permission to rewrite algorithm bodies.

## Compiler-Discovered Adapter Backlog

1. Production `stdafx.h` required `boost/asio.hpp`, initially exposing a missing
   dependency. The localized partition-only prelude does not use Asio, so the
   temporary `boost-asio` manifest dependency has been removed.
2. The production source depends on permissive MSVC parsing and mutable string
   literals in unrelated legacy geometry/VM headers. The probe mirrors those
   compiler settings; modernizing that code is outside the partition port.
3. CGAL 6.2 no longer exposes `CGAL::Arr_observer` through the production
   transitive include graph. A forced probe-only compatibility header includes
   `CGAL/Arr_observer.h`; no production algorithm body changes for this.
4. CGAL 6.2 point-location results use `std::variant`. The production
   tessellator has three source-local `boost::get` calls at lines 1213, 1222,
   and 1227, while shared `bezier_utils.h` has three more. The probe shim adds
   the pointer-form `boost::get` spelling for `std::variant`, allowing the
   trusted bodies to remain byte-for-byte intact. The final adapter can retain
   this bridge or mechanically change those six calls to `std::get_if`.
5. Production `debug_json.h` contains an overload set accepted by its legacy
   MSVC toolchain but ambiguous under conforming two-phase lookup. The probe
   tests `/Zc:twoPhase-`, but current MSVC still rejects line 336. This is the
   first confirmed Phoenix boundary replacement: retain the production solver
   calls and satisfy their narrow debug interface with a Phoenix diagnostics
   adapter rather than importing or modifying VM/debug infrastructure.

## Current Probe Result

All four copied
production translation units compile successfully against the localized
repository, solver loop, geometry/Bezier helpers, production randomizer,
partition-only `stdafx.h`, immutable scalar context, and null diagnostics.

## Diagnostics Decision

Legacy `debug_json` is explicitly out of scope. The port uses
`ported/null_diagnostics.hpp`, a source-compatible null object preserving the
trusted function signatures and fluent call expressions without serialization,
VM dependencies, allocations, or runtime work. When production files move into
Phoenix, their `../../debug_json.h` include is replaced by this one boundary
header; individual algorithm bodies are not cleaned up or rewritten.
