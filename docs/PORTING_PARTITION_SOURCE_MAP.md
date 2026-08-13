# Partition Production-to-Phoenix Source Map

Status: binding implementation policy for P9. This map supersedes any earlier
implication that the compile-spike solver is an accepted partition port.

## Non-Negotiable Port Rule

Partition behavior comes from the audited production sources. Phoenix may
change ownership and boundaries, but it does not independently reimplement the
solver or tessellator. Every changed production function must be classified as
one of:

| Classification | Permitted treatment |
| --- | --- |
| Trusted algorithm | Port body and control flow intact |
| Mechanical compatibility | Namespace/type spelling, CGAL 6.2 API, standard-library modernization |
| Phoenix boundary | Replace VM, command, persistence, allocation, or publication access with a narrow adapter |
| Legacy orchestration | Do not port; record the Phoenix owner of the behavior |
| Intentional deviation | Requires written rationale, fixture, and approval before implementation |

No function moves from the first two categories into a rewrite merely because
the production dependency graph is inconvenient.

All accepted adaptations are tracked line-by-line at the function level in
`PORTING_PARTITION_DIFF_LEDGER.md`. The existing `trusted_*` files are temporary
transcriptions, not the final source form: they remain only as comparison
fixtures until production-shaped adapted copies pass, then are removed.

## Authoritative Geometry Types

Production uses `CGAL::Arrangement_2` with `Arr_extended_dcel`. Its vertex,
directed-halfedge, and face data are authoritative for planar partition work.
Phoenix mirrors that design in `working_arrangement.hpp` with stable source IDs
and `LabelId` payloads. Exact arrangement handles remain invocation-local.

`ExactWorkingFace` is only a canonical-3D projection/lifting envelope. It is
not a face implementation. The provisional `SegmentCandidate`, `SolverView`,
angle solver, and constraint spike are behavioral test scaffolding; they are
not trusted port code and must not become the production execution path.

## File Classification

### `backend/segment_repository.h`

Status: accepted direct adaptation compiled and tested against Phoenix's
extended DCEL in `arrangement_segment_repository.{hpp,cpp}`.

Ported intact for the partition-used surface:

- `repo_segment_id`
- `repo_edge2`
- same-current-label collinear range construction
- arrangement/face search
- known, matched, and unmatched candidate semantics
- candidate lookup and edge-range label writes
- repository-wide production randomization using one compatibility-stream draw
  and one reused `default_random_engine`

Mechanical changes are limited to Phoenix IDs/labels, namespaces, and CGAL 6.2
handle/API spelling. Do not substitute vector boundary geometry.

The production `at()` and combination iterators are used by overlay, not the
partition solver, and are excluded from P9 unless a later trusted partition
call site proves otherwise. Debug JSON is diagnostics only.

Phoenix boundary classification: a projected mesh boundary has one shared CGAL
exterior face, while different source edges can border differently labeled mesh
faces. Therefore each directed DCEL halfedge also carries its source opposite-
face label for base-condition matching. This does not alter arrangement
topology or solver control flow.

### `backend/partition/partition_solver.{h,cpp}`

Foundation status: behaviorally accepted temporary transcription in
`trusted_solver_foundation.{hpp,cpp}` for `repo_segment_id`, the cut-segment
enum/wrapper and ID arithmetic, `segment_info`, `angle_range`, and
`partition_view`. Tests exercise arrangement-handle identity, line restriction,
angle restriction, branch-copy isolation, reset, and first-error evidence.

Branching status: behaviorally accepted temporary transcription in `trusted_branching.{hpp,cpp}`
for `branch_simple`, both segment `branch` overloads, filter reversal/application,
base/derived candidate lookup, cut ancestry/root arithmetic, `is_candidate`,
`view_for_segment`, `view_for_segments`, `angle_solution`, and `view_for_cut`.
The immutable `TrustedCut` index relationships replace production pointers but
preserve parent/left/right traversal. CGAL 6.2 intersection variant extraction
is classified as mechanical compatibility.

Trusted algorithm:

- cut-segment layout and identity arithmetic
- `segment_info` reset, collapse, and line restriction
- `angle_range` wrapping, intersection, and opposite handling
- `partition_view` branch-local state and angle restriction
- `partition_plan::build` and `advance` ordering
- `partition_model::branch_simple` and all `branch` overloads
- `partition_model::angle_solution`
- cut candidate validity/interception checks
- `view_for_segment`, `view_for_segments`, and `view_for_cut`
- segment ancestry/root/caused-segment logic
- evaluation and candidate orientation behavior

Phoenix boundary:

- `partition_cut` pointer tree becomes immutable linked plan ownership while
  preserving fields and traversal semantics
- VM values are resolved before kernel entry
- `partition_notify` maps to structured instruction-local failure evidence
- production randomizer calls are served by an invocation-local compatibility
  stream derived from Phoenix's item seed
- debug JSON/printing is optional diagnostics, never solver state

Legacy orchestration to drop:

- mutable model reset shared across commands
- loader-owned caches and VM context ownership
- command-global error plumbing

### `partition_solver_constraints.{h,cpp}`

Port every worker/evaluator body intact:

- absolute and percentage segment-length restrictions
- relative angle restrictions
- absolute and percentage distance restrictions
- parent/child extra distance instructions
- constraint priority and error registration ordering

Only each `build` method's VM lookup is replaced: Phoenix links immutable scalar
values and label/segment references before invocation. Constraint math and
branch mutation are not rewritten.

### `partition_solver_filters.h`

Port predicates intact over production-shaped `repo_edge2` arrangement ranges.
Resolve any VM inputs before construction. Do not approximate filters using
detached segments.

### `partition_tesselator.{h,cpp}`

Port intact after the solver compiles against arrangement handles:

- `compass_labels` and every directed label write
- `partition_tesselator::run`
- count/length repeat distribution
- repeat-face cutting and margin/last-repeat cases
- vertex lookup/insertion
- Bezier insertion
- `insert_cut`, `add_repo`, and `do_cut`
- tessellator-node traversal and result-face collection

Phoenix boundaries are limited to exact-arrangement input, final 3D lift/
demotion, fresh published element IDs, and source provenance. Production's
shared split edge ID is provenance only; it cannot violate Phoenix ID
uniqueness.

### `partition_errors.{h,cpp}`

Preserve error categories and triggering conditions. Translate payload and
routing into Phoenix failure effects; do not port exception/command plumbing.

## Explicitly Excluded Sources

- `commands/partition_command.*`: orchestration evidence only
- JSON/binary partition loaders: persistence evidence and plan-link inputs only
- legacy 2D runtime handler: not part of Phoenix
- generic cleanup/simplify/merge helpers: add only when a fixture proves the
  trusted partition path requires them

## Recovery Sequence

1. Keep projection/lifting, immutable linked inputs, and extended-DCEL builder.
2. Quarantine compile-spike solver types from the runtime build.
3. Adapt production `segment_repository.h` directly to the Phoenix DCEL.
4. Compile `segment_info`, `angle_range`, and `partition_view` with mechanical
   changes and compare against existing spike fixtures.
5. Port branch/cut solving function-by-function, retaining source correspondence.
6. Port constraints without algebraic rewrites.
7. Port tessellator against the same arrangement.
8. Remove superseded spike implementations.
9. Only then integrate instruction consumption, failure routing, cache, and
   publication.

## Review Gate

A P9 change is incomplete if a reviewer cannot point from each solver or
tessellator function to its production source and identify every altered line
as mechanical compatibility, Phoenix boundary adaptation, or an approved
deviation.
