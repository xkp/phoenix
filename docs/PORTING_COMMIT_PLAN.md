# Porting Commit Plan

The current worktree spans multiple completed phases and must not be reviewed or
landed as one commit. This plan is the persistent split contract. No commit is
created automatically; staging and commit creation remain an explicit user
action.

## C1 — Extrusion P8 hardening

Owns only the already-completed extrusion/platform work:

- `.github/workflows/extrusion-platforms.yml`
- `docs/PORTING_EXTRUSION_P8_HARDENING.md`
- extrusion changes in `include/phoenix/extrusion`, `src/extrusion`, and their
  tests
- canonical geometry changes required by extrusion
- the extrusion/platform hunks in `CMakeLists.txt`, `CMakePresets.json`, and
  `docs/PORTING_IMPLEMENTATION_PLAN.md`

Verify the full pre-partition test suite for this commit.

## C2 — Partition audit and immutable source evidence

Documentation/evidence only:

- `docs/PORTING_PARTITION_AUDIT.md`
- `docs/PORTING_PARTITION_SOURCE_MAP.md`
- `src/legacy/partition/README.md`
- the P9 policy/status hunks in `docs/PORTING_IMPLEMENTATION_PLAN.md`

This commit explains what is trusted, what is intentionally dropped, and the
3D/exact-kernel/label-ownership boundaries before implementation appears.

## C3 — Quarantined partition behavioral scaffolding

Owns the earlier experimental and fixture-oracle implementation:

- non-`ported` files under `include/phoenix/partition` and `src/partition`
- `tests/partition_plan_tests.cpp`
- `tests/partition_solver_view_tests.cpp`
- the scaffolding portions of `tests/partition_working_face_tests.cpp`
- the matching CMake target/source hunks

The commit message and source comments must say **quarantined behavioral
scaffolding**. It is not the accepted production port and must not be extended.

## C4 — Verbatim production partition source import

Owns the nine hash-pinned production partition files under:

- `include/phoenix/partition/ported/production`
- `src/partition/ported/production`

Also owns:

- `docs/PORTING_PARTITION_PRODUCTION_MANIFEST.md`
- the source-import row in `docs/PORTING_PARTITION_DIFF_LEDGER.md`

Review criterion: all production files were normalized-content-identical to the
manifest checkout before adaptation. This commit may include the minimal path
layout needed to make the copy reviewable, but no Phoenix runtime adapter.

## C5 — Partition compatibility boundary and compile proof

Owns the explicit adaptations around the imported code:

- `include/phoenix/partition/geometry.h`
- localized `ported` geometry, Bezier, repository, solver, randomizer, VM-value,
  null-diagnostics, CGAL, and prelude headers
- the two `debug_json` include substitutions
- exclusion of the legacy reader-only loader
- `docs/PORTING_PARTITION_DIFF_LEDGER.md`
- `phoenix_partition_production_probe` CMake hunks
- compile assertions added to `tests/partition_working_face_tests.cpp`

Required verification:

1. Clean rebuild of all four production partition translation units.
2. Existing partition fixture suite passes.
3. No production VM, `debug_json`, Boost.Asio, or application-wide `stdafx.h`
   enters the include graph.
4. `git diff --check` passes.

## C6 — End-to-end Phoenix partition adapter

This is the next feature commit and does not exist yet. It will contain:

- canonical 3D face projection into invocation-local exact geometry
- construction and execution of the production solver/tessellator
- stable label and provenance transfer
- consumed-face replacement semantics
- lift back to canonical 3D geometry
- end-to-end parity fixtures

Keeping C6 separate prevents runtime integration decisions from obscuring the
review of the production source import and compatibility boundary.

## Shared-File Rule

`CMakeLists.txt`, `CMakePresets.json`,
`docs/PORTING_IMPLEMENTATION_PLAN.md`, and
`tests/partition_working_face_tests.cpp` contain work from more than one commit.
They must be staged by hunk (`git add -p`) or split with temporary index-only
patches. Do not stage these files wholesale until their unrelated hunks have
already landed.

