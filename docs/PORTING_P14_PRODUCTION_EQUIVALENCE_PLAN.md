# P14 Production Equivalence Plan

Status: planning baseline.

## Goal

P14 proves that migrated Phoenix packages execute faithfully compared with the
production project outputs they replace.

P13 answered whether migrated packages can be read, loaded, and executed. P14
answers whether the execution is correct: geometry, labels, materials, styles,
actor hierarchy, diagnostics, and known unsupported behavior must be compared
against production evidence.

## Status Convention

- not started: no implementation work has landed
- in progress: implementation exists but is incomplete or not fully verified
- blocked: work cannot continue without a decision, fixture, or dependency
- complete: implementation and focused verification have landed
- deferred: intentionally postponed outside the current P14 slice

## P14-P1: Equivalence Artifact Contract

Status: not started

Define the structured artifact format used to compare production output and
migrated Phoenix output.

Required fields:

- project identity, package identity, repair selection identity, and run seed
- actor tree shape and stable actor identifiers where available
- geometry summary: vertex, edge, face, shell/component counts
- geometry fingerprints, with tolerance-aware fallbacks where exact identity is
  not expected
- per-face, per-edge, and per-vertex labels
- label names, material references, and style references
- output ports and root scene outputs
- diagnostics, unsupported runtime behavior, and repair choices applied

Exit criteria:

- a checked-in schema or documented JSON shape exists
- the artifact distinguishes missing data from intentionally ignored data
- unsupported runtime behavior, such as `smooth(method=hardEdges)`, is explicit

## P14-P2: Production Baseline Reader

Status: not started

Read production output artifacts for real projects from `C:\prod\projects` and
normalize them into the P14 equivalence artifact contract.

Initial target:

- `COUNTRY_BARN` production output files beside the source project

Required behavior:

- discover the production output files for a project
- preserve label names, material references, and style references
- preserve actor/output hierarchy when production output records it
- report missing or unreadable baseline sections as diagnostics instead of
  guessing

## P14-P3: Phoenix Output Snapshot

Status: not started

Extend the migrated package runner, or add a sibling CLI, to emit the Phoenix
runtime output using the same P14 equivalence artifact contract.

Required behavior:

- run a migrated package with a selected repair policy
- capture all produced outputs, not only the current runtime smoke status
- serialize actor hierarchy, geometry summaries, labels, materials, styles, and
  diagnostics
- keep runtime unsupported nodes visible in the snapshot

## P14-P4: Equivalence Comparator

Status: not started

Compare production and Phoenix equivalence artifacts and emit a structured
report.

Required checks:

- actor/output tree differences
- geometry count and fingerprint differences
- label, material, and style differences
- missing outputs or unexpected extra outputs
- runtime diagnostics versus expected unsupported behavior
- tolerance-aware geometry comparison where exact fingerprints are too strict

Report requirements:

- machine-readable JSON report for automation
- concise human-readable summary for command-line use
- file paths and actor/output identifiers in every actionable mismatch

## P14-P5: COUNTRY_BARN Equivalence Attempt

Status: not started

Run the first end-to-end production equivalence attempt for the repaired
COUNTRY_BARN package.

Inputs:

- production source project under `C:\prod\projects`
- corresponding production output artifacts
- `build\windows-default\COUNTRY_BARN_retired_test.phxmig`
- repair plan and repair selection used to produce the package

Acceptance:

- production baseline artifact is generated
- Phoenix output artifact is generated
- comparator report is generated
- all mismatches are classified as one of:
  - migration/runtime bug
  - known unsupported behavior
  - expected representation difference
  - missing baseline data

## P14-P6: Regression Fixture Set

Status: not started

Promote selected real-project equivalence attempts into repeatable regression
fixtures.

Required behavior:

- fixture inputs are small enough to run in normal development
- reports are deterministic under fixed seeds
- failures point to project, function, instruction, actor, and output context
- fixture set includes at least one successful project and one project with a
  known unsupported runtime diagnostic

## Known Carry-Ins

- P13 currently reports `runtime_attempt: ok` for
  `COUNTRY_BARN_retired_test.phxmig`, with `outputs: 0`.
- `smooth(method=hardEdges)` is recognized but intentionally runtime
  unsupported until a hard-edge kernel is ported.
- overlay remains out of scope.
- reusable versioned function repositories remain a later direction after
  migration correctness is proven.
