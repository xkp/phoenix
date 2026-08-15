# P12 Production Project Migration Plan

Status: planning baseline for migrating production projects into Phoenix before
the authoring tool is ported to a Phoenix-native development format.

## Goal

P12 migrates existing production projects into a Phoenix-runnable representation
with deterministic validation, explicit repair points, and preserved provenance.

The migrated format is not the final Phoenix authoring format. Until the tool
itself is ported, P12 should prioritize correct project migration over editable
project ergonomics.

## Format Direction

Use a Phoenix migrated-project package as the execution artifact. A binary
package is acceptable and likely preferable for the runtime-facing artifact, but
it must be paired with human-readable diagnostics and repair inputs.

Required artifacts:

- migrated package: canonical Phoenix-runnable project data
- migration report: diagnostics, warnings, provenance, and summary statistics
- migration overrides: user-authored or tool-authored fixes applied before
  package emission

The migrated package may be self-contained initially. Later phases can replace
embedded reusable functions with references to a versioned function repository.

## Status Convention

Use these status values when updating tasks:

- not started: no implementation work has landed
- in progress: implementation exists but is incomplete or not fully verified
- blocked: work cannot continue without a decision, fixture, or dependency
- complete: implementation and focused verification have landed
- deferred: intentionally postponed outside the current P12 slice

## Non-Goals

- Do not design the final Phoenix authoring/development format in P12.
- Do not require the production tool to write Phoenix-shaped projects yet.
- Do not silently repair ambiguous legacy state.
- Do not duplicate reusable imported/tool functions in the long-term model.
- Do not make runtime execution responsible for migration conflict resolution.

## Production Inputs

Production projects are currently directory/object-store shaped. A root project
contains a manifest file named like `NAME@GUID`, a `.nodes` scene graph, local
instruction payload blobs, and sometimes local function manifests plus their
`.nodes` files. Imported functions may be referenced without being physically
present in the same project directory.

The migration pipeline must preserve these source facts:

- project directory path
- project ID and function IDs
- root/local/imported function distinction
- source manifest path and `.nodes` path
- instruction payload blob path
- label, material, profile, style, attribute, and variable origins
- call-site variable/config overrides

## Migration Pipeline

### P12-P1: Project Discovery

Status: complete

Scan configured production project roots and build an index of available
projects and functions.

Outputs:

- project ID to project directory
- function ID to candidate manifest and `.nodes` files
- imported function references that cannot yet be resolved
- duplicate candidate definitions for the same function ID as link-time facts

Landed:

- dependency-free production project discoverer API
- recursive manifest and `.nodes` pairing
- automatic inclusion of a sibling `functions` root when scanning `projects`
- root project candidate detection
- function candidate index keyed by function ID
- shallow function-reference extraction from `.nodes`
- unresolved imported-function reference reporting
- missing `.nodes` diagnostics
- duplicate candidates preserved for function-link fingerprinting, not treated
  as discovery errors because production projects are published into functions
- focused tests in `phoenix_production_project_discovery_tests`

### P12-P2: Raw Load

Status: complete

Load production-shaped data without converting it to Phoenix semantics.

The raw model should represent:

- project/function manifests
- `.nodes` node and link graphs
- instruction payload blob references
- labels, materials, profiles, styles, attributes, variables
- imported function call references and call-site overrides

This phase should be tolerant of unknown fields and preserve them for
diagnostics or future migration work.

Landed:

- raw production project loader API
- manifest text preservation
- `.nodes` text preservation
- instruction payload blob loading by GUID `file` references
- missing payload blob diagnostics
- function references are kept as function references, not payload blobs
- focused tests in `phoenix_production_project_raw_load_tests`

### P12-P3: Canonical Registry Build

Status: complete

Build canonical registries for run-visible project data before execution.

Labels are the first required registry:

- same label UID and identical full definition: one canonical label
- same label UID and conflicting definition: hard migration error
- empty label UID: hard migration error
- missing referenced label UID: migration error unless repaired
- different UID with same name/color/material: allowed, optional warning
- function-local label crossing a function boundary: explicit diagnostic and
  repair candidate

Profiles are validated before migration even though they do not yet have a
Phoenix runtime registry:

- same profile ID and identical manifest definition: one canonical profile
- same profile ID and conflicting manifest definition: hard migration error
- unreachable sibling-library profiles do not participate in the migrated
  project registry

Materials, styles, and attributes should follow the same pattern once their
Phoenix runtime contracts are defined.

Landed:

- production label registry builder API
- production manifest label extraction from raw loaded functions
- production `visible` mapped to Phoenix `hidden`
- canonical Phoenix `LabelRegistry` build through existing `LabelLinker`
- identical reachable duplicate labels deduplicate deterministically
- conflicting reachable duplicate label UIDs produce diagnostics
- per-function production label declarations are preserved
- production profile registry/audit builder API
- identical reachable duplicate profiles deduplicate deterministically
- conflicting reachable duplicate profile IDs produce diagnostics with
  profile ID/name and manifest path
- focused tests in `phoenix_production_profile_registry_tests`
- focused tests in `phoenix_production_label_registry_tests`

### P12-P4: Function Linking

Status: complete

Resolve every function reference to a canonical definition.

Rules:

- same function ID and identical definition: one canonical function
- same function ID and conflicting definition: hard migration error
- repeated call nodes with different variable/config overrides are call sites,
  not duplicated function definitions
- unresolved imported function ID: migration error unless repaired
- root/local functions may be embedded in the package
- imported reusable functions may be embedded initially and externalized later

The linker should produce a complete reachable function set for the migrated
package. Lazy repository loading is desirable later but is not required for the
initial migration path.

Landed:

- production function linker API
- reachable function traversal from root project candidates
- sibling function-library definitions participate in linking
- canonical linked function table keyed by function ID
- content fingerprinting for duplicate function candidates
- identical duplicate function candidates deduplicate with preserved origins
- duplicate project/function publication copies ignore export-only
  `selfContained` metadata and JSON formatting differences during semantic
  comparison
- conflicting same-ID function definitions produce diagnostics
- unresolved function references produce diagnostics
- focused tests in `phoenix_production_function_linker_tests`

### P12-P5: Phoenix Graph Adaptation

Status: complete

Convert production `.nodes` data into Phoenix `FunctionDescriptor` data.

Each production node becomes an instruction descriptor with:

- stable node ID
- instruction kind
- input and output port descriptors
- referenced label UIDs
- called function ID, when the node invokes a function
- configuration revision covering behavior-affecting payload/config data
- payload reference into the migrated package for instruction-specific data

Production function-call nodes must keep per-call variable/config overrides
separate from the canonical called function body.

Landed:

- production graph adapter API
- raw `.nodes` node extraction into Phoenix `FunctionDescriptor`
- production node type mapped to instruction kind
- disabled production nodes are excluded from migrated graphs
- branches that only flow through disabled nodes are pruned from migrated graphs
- production input/output socket arrays mapped to Phoenix ports
- function-call `file` references mapped to `called_function_id`
- GUID payload `file` references mapped to configuration revisions
- function input and output nodes mapped to function ports
- production links mapped to Phoenix edges
- production links resolved by node array index, matching `threedee.solver`
  loaders
- link node/socket diagnostics
- focused tests in `phoenix_production_graph_adapter_tests`

### P12-P6: Diagnostics And Repair

Status: complete

Migration diagnostics must be stable, precise, and actionable.

Every diagnostic should include where possible:

- severity
- diagnostic code
- message
- project ID and path
- function ID and function name
- node ID and node type
- referenced UID or function ID
- source file/blob path
- conflicting definitions or candidates
- suggested repair actions when deterministic

Suggested severity model:

- error: migrated package cannot be emitted without an override
- warning: package can be emitted, but behavior may differ or legacy data is
  suspicious
- info: duplicate-but-identical or provenance-only observation

Repair must be explicit and reproducible. The migration tool should not choose
between conflicting production definitions silently.

Missing functions have only two valid repair paths:

- ignore the unresolved function calls for this migration, equivalent to
  marking the offending production call nodes disabled and then pruning any
  now-disabled-only subtrees
- install/publish the missing function in the original production source
  (`C:\prod\functions` or the equivalent deployment location) and rerun
  migration

Label and profile conflicts must present all discovered choices to the user.
Each choice should include:

- source function ID and manifest path
- stable UID/profile ID
- recognizable name
- full comparable values, including fields that caused the conflict
- count/origins where the same choice appears multiple times

Repair overrides should support at least:

- ignore unresolved function calls by referenced function ID
- select one canonical label definition by label UID and source choice
- select one canonical profile definition by profile ID and source choice
- remap one label UID to another only as an explicit advanced repair
- mark a diagnostic as accepted only when the runtime behavior remains defined

Landed:

- unified production migration report API
- stable diagnostic severity enum
- stable diagnostic code prefixes per pipeline phase
- discovery, raw-load, function-link, label, and graph diagnostics merged into
  one report
- report carries discovery/raw/link/label/graph intermediate outputs
- report-level success and error/warning counts
- focused tests in `phoenix_production_migration_report_tests`
- in-memory override hook exists for early repair experiments
- generated repair-choice JSON reports for unresolved functions, labels, and
  profiles
- repair choices include source function ID, path, occurrence count, and
  comparable values
- label choices include production-facing `visible` for recognition, but
  `visible` is not a semantic differentiator
- label material/style references are enriched from sidecar label asset files
- unresolved function repair supports `ignore_calls`, applied in memory by
  disabling matching production function-call nodes before relinking
- interactive CLI repair mode writes cumulative selections and rebuilds until
  clean or no further selections are made
- saved repair selection replay via `--repair-selection`
- repaired reports can proceed to package emission from the CLI

### P12-P7: Package Emission

Status: complete

Emit a Phoenix migrated package only after errors are resolved or explicitly
overridden.

The package should contain:

- migration schema version
- source project identity
- canonical function table
- canonical label registry
- material/profile/style registries as supported
- per-function graph descriptors
- instruction payload blobs
- variable defaults and call-site overrides
- deterministic seeds/randomization settings
- provenance and fingerprints

Landed:

- migrated project package API
- package builder from unified migration report
- package emission is refused while report errors exist
- schema version marker for the initial P12 package boundary
- root function ID preservation
- label registry semantic fingerprint preservation
- canonical adapted function graphs included
- instruction payload blob text and source paths included
- manifest, `.nodes`, origin paths, and function fingerprints included
- focused tests in `phoenix_production_migrated_package_tests`
- deterministic text package reader/writer for fixture testing
- `phoenix_migrate_project` CLI writes deterministic text packages from clean
  production migration reports
- CLI smoke coverage in `phoenix_migrate_project_cli_tests`

Deferred:

- final binary file writer/reader
- deterministic seed/randomization serialization beyond raw preserved data

## Versioned Function Repository

Status: planned, deferred until self-contained migrated packages are working

A versioned reusable function repository is a later P12/P13 direction, not the
first migration artifact.

Repository rules:

- functions are referenced by exact version or content hash, not by latest name
- project-local functions override repository lookup only by explicit identity
- repository functions expose their required labels/profiles/assets
- old migrated projects must remain stable when repository functions evolve
- self-contained migrated packages remain possible by vendoring exact function
  versions

Initial migration should still collect the data needed to populate the
repository later: canonical function fingerprints, origins, import paths,
dependency lists, and call-site override shapes.

## Initial Implementation Slices

### P12-S1: Read-Only Production Auditor

Status: complete

Build a scanner over `C:\prod\projects` and selected fixtures.

Report:

- project and function manifests found
- `.nodes` files found
- local vs imported function references
- unresolved imported function IDs
- duplicate label UID counts
- conflicting label definitions
- unresolved label references in `.nodes`
- instruction payload blobs referenced by nodes
- repeated function-call nodes and their call-site overrides

No Phoenix package emission in this slice.

Landed:

- project/function discovery core via `P12-P1`
- sibling `functions` root inclusion for production project scans
- audit API for duplicate label occurrences and conflicting definitions
- audit summary for repeated function calls
- audit summary for instruction payload blob references
- initial unresolved label-like GUID reference diagnostics in `.nodes`
- explicit audit summary counters for manifests, `.nodes`, local/imported
  function references, unresolved imports, duplicate/conflicting labels,
  unresolved label references, payload blobs, and repeated call sites

### P12-S2: Label Canonicalization

Status: complete

Implement production label extraction and canonical registry diagnostics.

Acceptance:

- identical duplicate labels deduplicate
- conflicting duplicate UIDs produce migration errors
- production `visible` maps consistently to Phoenix `hidden`
- diagnostics include project/function/source provenance
- tests cover local duplicates, imported duplicates, and conflicts

Landed:

- production label extraction from raw function manifests
- canonical label registry build through `LabelLinker`
- production `visible` mapped consistently to Phoenix `hidden`
- identical reachable duplicate labels deduplicate
- conflicting reachable duplicate label UIDs diagnose
- node graph label GUID references feed canonical label validation
- function and payload `file` GUIDs are excluded from label-reference checks
- unknown node label references diagnose as unresolved references
- focused coverage in `phoenix_production_label_registry_tests`

### P12-S3: Function Reference Index

Status: complete

Resolve reachable function IDs across production project roots.

Acceptance:

- local function manifests are discovered
- imported function references are reported when missing
- same-ID candidate definitions are fingerprinted
- repeated call sites do not produce duplicate canonical function bodies

Landed:

- function linker exposes caller-to-callee call counts
- function linker exposes flattened function reference records
- repeated production function call nodes remain one canonical linked function
  body
- sibling function-library definitions remain resolvable from project scans
- duplicate same-ID candidate fingerprinting remains the canonical conflict
  detector
- focused coverage in `phoenix_production_function_linker_tests`

### P12-S4: Repair Override Prototype

Status: complete

Add repair override input and apply it before package emission.

Initial override cases:

- unresolved function call ignore
- label definition choice
- profile definition choice
- advanced label UID remap

Landed:

- in-memory production migration override model
- label UID remap overrides applied before label registry/report/package stages
- function reference rewrite overrides applied before function linking
- unresolved function ignore overrides applied by disabling matching call nodes
- label definition choice overrides applied before label registry/report/package
  stages
- profile definition choice override model
- applied override records preserved in migration reports
- repaired reports can proceed to migrated package emission
- generated repair-choice JSON writer
- interactive CLI repair loop
- saved repair selection replay with `--repair-selection`
- focused coverage in `phoenix_production_migration_overrides_tests`
- CLI coverage in `phoenix_migrate_project_cli_tests`

### P12-S5: Minimal Migrated Package

Status: complete

Emit a first Phoenix migrated package for a small fixture with:

- root function graph
- canonical labels
- instruction payload references
- provenance
- no repository dependency

Landed:

- end-to-end minimal production-shaped migration fixture
- project root plus sibling function-library function
- canonical duplicate label deduplication across project and child function
- function reference indexing into one canonical child function body
- production graph adaptation for root and child functions
- instruction payload blob preservation inside the migrated package
- self-contained package emission with no external repository dependency
- focused coverage in `phoenix_production_minimal_migration_tests`

### P12-S6: Real Project Package Attempt

Status: complete

Create a migrated package from a real production project before implementing a
package loader.

Current target:

- `C:\prod\projects\COUNTRY_BARN@4F3E6259-C81C-438F-B614-C5D6B6599DB7`

Landed:

- single-project discovery now includes the matching sibling function set under
  `C:\prod\functions\<project-id>` when present
- clean smoke fixture writes a deterministic `.phxmig` package through
  `phoenix_migrate_project`
- real `COUNTRY_BARN` package attempt reaches actionable diagnostics instead of
  scanning the entire shared function library
- real `COUNTRY_BARN` repair-choice file is generated
- interactive repair can ignore the unresolved
  `AUX_FACE_INVERTER_2.0@429C1E50-4830-4B0F-A15F-230F4189BE0D` call and choose
  canonical label definitions
- saved `COUNTRY_BARN` repair selections replay non-interactively with
  `--repair-selection`
- repaired `COUNTRY_BARN` package emission succeeds with zero diagnostics

Deferred:

- package reader
- package execution/runtime validation
- geometry/material output comparison

## Fixture Candidates

Start with production projects under `C:\prod\projects`.

Known useful examples:

- `COUNTRY_BARN@4F3E6259-C81C-438F-B614-C5D6B6599DB7`: local functions,
  imported functions, repeated tool calls, repeated labels, partition payloads
- `BG_BLANK_00@9A7C286F-D2A1-4A92-AA14-F84ECA7812F7`: active output JSON tabs
  indicate it is useful for migrated output comparison

Add more fixtures as the auditor classifies small, medium, pathological, and
performance-sensitive projects.

## Acceptance Principles

- Migration conflicts are found before runtime execution.
- Identical legacy duplication is deduplicated deterministically.
- Ambiguous or conflicting legacy state is never silently first-wins or
  last-wins.
- User repairs are explicit and repeatable.
- Runtime-visible registries are immutable after link/package load.
- Migrated project behavior is compared by geometry and materials.
- Function repository extraction does not destabilize already migrated projects.
