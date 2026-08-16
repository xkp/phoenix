# P13 Package Reader And Runtime Validation Plan

Status: runtime load validation slice.

## Goal

P13 proves that P12 migrated packages can be read back, validated, and then
fed into Phoenix runtime execution paths.

P13 starts with package integrity and smoke validation. Full geometry/material
equivalence is intentionally a later P13 slice.

## Status Convention

- not started: no implementation work has landed
- in progress: implementation exists but is incomplete or not fully verified
- blocked: work cannot continue without a decision, fixture, or dependency
- complete: implementation and focused verification have landed
- deferred: intentionally postponed outside the current P13 slice

## P13-P1: Package Reader Integrity

Status: complete

Read a migrated package and validate its structural integrity before attempting
runtime execution.

Required checks:

- package header and schema are recognized
- root function is present in the function table
- function graph IDs match their function table keys
- called function IDs resolve inside the package
- instruction payload references resolve inside the owning function package
- graph descriptors are counted and preserved for the runtime load model

Landed:

- deterministic text package reader exists from P12
- text package IO round-trip tests exist in
  `phoenix_production_migrated_package_io_tests`
- package validator API/CLI
- real `COUNTRY_BARN` package reader smoke test

Deferred:

- strict Phoenix `GraphValidator` checks after the runtime load model resolves
  function boundary nodes and ports

## P13-P2: Runtime Load Model

Status: complete

Convert a read package into the runtime structures needed to execute a root
function.

Required data:

- root function ID
- canonical function table
- label registry metadata
- per-function graph descriptors
- instruction payload blobs/configuration revisions
- provenance for diagnostics

Landed:

- runtime package loader API
- load-time graph normalization for production function boundary edges
- synthetic output node materialization from production output boundary edges
- strict Phoenix `GraphValidator` checks over normalized loaded graphs
- focused runtime loader tests
- `phoenix_validate_package` now reports normalized edge count and graph
  validation status

Deferred:

- executable boundary input/output binding model
- materialized runtime instruction payload objects
- actual geometry execution

## P13-P3: Minimal Execution Smoke

Status: complete

Execute a tiny migrated fixture package through the runtime path.

Acceptance:

- package reads cleanly
- runtime loader constructs an executable function graph
- execution either produces expected geometry or a precise runtime gap
  diagnostic

Landed:

- tiny migrated package fixture is written, read, loaded, and executed through
  `FunctionExecutor`
- load-time output boundary materialization is covered by execution smoke

## P13-P4: COUNTRY_BARN Runtime Attempt

Status: in progress

Use the saved COUNTRY_BARN repair selection file to migrate, read, load, and
attempt runtime execution.

Landed:

- `phoenix_run_migrated_package` CLI reads, loads, and preflights a migrated
  package for execution
- missing migrated instruction handlers are reported by kind before execution
- function-call nodes are routed through `called_function_id` instead of being
  treated as missing handlers
- migrated instruction registry installs explicit unsupported-operation
  placeholders so runtime attempts report unsupported adapter kinds before
  execution
- package emission now preserves each production node `data` object as
  per-instruction inline loader data
- package emission now preserves finalized label UID to numeric `LabelId`
  records so migrated production loader data can be adapted into Phoenix
  instruction configs
- package emission now preserves non-expression numeric manifest variables per
  function so legacy bracket references like `[BARN_HEIGHT]` can be resolved
  during migrated adapter loading
- package emission now evaluates deterministic numeric manifest expressions
  over already-known manifest variables and preserves the resulting values
- package emission now folds unambiguous call-site numeric variable overrides
  into the callee's migrated variable table
- package emission now preserves profile sidecar JSON per function, allowing
  migrated adapters to rebuild immutable Phoenix extrusion profiles from
  production profile assets
- COUNTRY_BARN replay package currently contains 641 `instruction_data`
  records, 117 `label_id` records, 102 numeric `variable` records, and 37
  `profile` records,
  matching the old solver loader-scope config source plus the finalized label
  registry/variable/profile bridges
- migrated `lod` adapter is wired to the existing Phoenix LOD handler using
  production port names
- migrated `merge` adapter decodes old `merge_loader` fields
  (`joinVertexs`, `mergeFaces`, `mergeFacesLabels`, `joinColineal`,
  `mergeBorders`, and `method == edges`) into the existing Phoenix merge
  handler
- migrated `extrusion` adapter supports the numeric `amount` production path by
  translating it into a one-segment immutable Phoenix extrusion profile and
  wiring the current Phoenix extrusion handler into the migrated runner
- migrated `extrusion` adapter supports simple deterministic production amount
  expressions over function numeric variables, such as `[BARN_HEIGHT]`,
  `BARN_HEIGHT-2.4`, and `-(BARN_LENGHT+0.5)`
- migrated `extrusion` adapter supports explicit `method == profile`
  instructions by loading the referenced production profile sidecar into a
  Phoenix immutable profile
- migrated `extrusion` adapter supports legacy implicit profile objects shaped
  as `"profile": {"id": ...}` without an explicit `method` field
- retired production `extrusion.method == label` is now rejected during package
  emission as `package.retired_instruction_method`
- migrated `select` adapter supports by-label output routing and face
  condition forms backed by the existing Phoenix select handler:
  `isLabeled`, `hasEdge`, `hasEdgeByLength`, `hasEdgeByOpposite`, and
  `hasBorderEdge`
- migrated `rename` adapter supports manual label maps plus non-expression
  face/edge condition forms backed by the existing Phoenix rename handler;
  the COUNTRY_BARN expression/binding edge-adjacent shape is translated into a
  native adjacent-edge predicate
- migrated `partition` adapter loads preserved production partition payloads
  (`baseCurves`, `cutHelpers`, common constraints, labels, and repeat metadata)
  into the existing ported partition solver/runtime handler
- focused CLI test covers the missing-handler diagnostic contract

Current expected result:

- COUNTRY_BARN loads, but runtime attempt is blocked until the remaining
  migrated instruction payload adapters are implemented

COUNTRY_BARN current missing handler kinds:

- case
- choice
- extrusion
- if
- inset
- loop

`rename` remains listed because COUNTRY_BARN includes production
`renameEdgeByExpression` nodes with `edgeAdjacentBinding`; those need the
binding/expression compatibility layer before they can execute faithfully.

Current partial adapter coverage reported by `phoenix_run_migrated_package`:

- extrusion: 73 / 74 graph instructions adapted before retired-method package
  rejection
- partition: 47 / 47 graph instructions adapted
- rename: 81 / 81 graph instructions adapted

`extrusion` remains listed only for the retired production label-driven profile
method. Current policy is to fail migration/package emission for that case
instead of emulating it in Phoenix.
