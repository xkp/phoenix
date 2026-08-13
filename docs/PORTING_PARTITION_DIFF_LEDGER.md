# Partition Source-Diff Ledger

This ledger is the review record for mechanically adapting the production
partition implementation. A ported function is not accepted until its source
file, production symbol, Phoenix symbol, and every non-whitespace change are
accounted for here.

## Compatibility Environment

| Production dependency | Phoenix compatibility binding | Classification | Rationale |
| --- | --- | --- | --- |
| `GeometryKernel` | `partition::compat::Kernel` | Mechanical compatibility | Both are `CGAL::Exact_predicates_exact_constructions_kernel`. |
| `DEFAULT_CGAL_TYPES()` | aliases in `compat/geometry_types.hpp` | Mechanical compatibility | Preserves the production point, segment, vector, ray, direction, line, and circle types without global macros. |
| `DEFAULT_ARRANGEMENT_TYPES()` | aliases in `compat/geometry_types.hpp` | Mechanical compatibility | Binds production handle vocabulary directly to Phoenix's invocation-local `ExactArrangement`. |
| `vertex2_data` | `ArrangementVertexData` | Phoenix boundary | Keeps production working `id/index/tag`; adds optional stable source provenance. |
| `edge2_data` | `ArrangementHalfedgeData` | Phoenix boundary | Keeps production working `id/label/tag`; adds directed source IDs and opposite-face label provenance. |
| `face2_data` | `ArrangementFaceData` | Phoenix boundary | Keeps production working `id/label/tag`; adds optional stable source provenance. |
| `LABEL_UNBOUNDED_IDX`, `LABEL_LAYOUT_IDX` | `compat::label_unbounded`, `compat::label_layout` | Mechanical compatibility | Preserves named negative kernel sentinels; they remain outside ordinary published label IDs. |
| VM context/value lookup | pre-linked immutable Phoenix inputs | Phoenix boundary | Values are resolved before entering the kernel; solver math is unchanged. |
| `boost::optional<CGAL::Object>` intersection extraction | CGAL 6.2 optional variant result | Mechanical compatibility | Required by the current CGAL result API; branch decisions and constructions remain unchanged. |

The compatibility aliases are the only approved vocabulary for copied solver,
constraint, and tessellator sources. Adapted algorithm files must not include
the canonical 3D runtime mesh directly.

## Function Ledger

| Production source/symbol | Phoenix location | Status | Classified changes |
| --- | --- | --- | --- |
| Complete partition source set (five headers, four implementations) | `partition/ported/production`, `src/partition/ported/production` | Copied; normalized identity verified before adaptation | None. All 6,131 normalized source lines match the hash-pinned production checkout. The compile probe now targets these local files. |
| `partition_solver.h/.cpp`: `../../debug_json.h` includes | same copied files | Mechanically adapted | Both include lines bind to `ported/null_diagnostics.hpp`. All production diagnostic expressions and algorithm signatures remain unchanged; serialization and VM/debug side effects are intentionally dropped. |
| `backend/segment_repository.h` | `partition/ported/segment_repository.h` | Copied, then one include adapted | Production repository body is retained. Its `../debug_json.h` include binds to the same null diagnostics boundary; matching, grouping, randomization, and label writes are unchanged. |
| `backend/solver.h` | `partition/ported/solver.h` | Copied, then one include adapted | Generic candidate ordering, plan advancement, branch insertion, and acceptance loop are retained. Only `debug_json.h` binds to null diagnostics. |
| `vm/vm.h` partition-used surface | `partition/ported/vm_compat.hpp` | Phoenix boundary | Replaces the full VM with immutable scalar context lookup and production-shaped `variable_value` construction/value access. Partition algorithm call sites remain unchanged; command execution, assets, and expression runtime are excluded. |
| `backend/bezier_utils.h` | `partition/ported/bezier_utils.h` | Copied, then one include adapted | Bezier construction/intersection code is retained; its VM variable include binds to `vm_compat.hpp`. |
| VM function-context ID and split helpers | `vm_compat.hpp::icontext` | Phoenix boundary | Supplies invocation-local vertex/edge IDs and the production split/copy-payload operation required by tessellation. No global VM ownership is imported. |
| `backend/match.h` | `partition/ported/match.h` | Copied, then includes narrowed | Matching predicates remain intact; the full VM include binds to `vm_compat.hpp`, and redundant `geometry.h` is removed because solver geometry types are established before repository matching. |
| `backend/geometry_ops.h` | `partition/ported/geometry_ops.h` | Copied; redundant umbrella include removed | Geometry operation bodies retain their production ordering. `geometry.h` was explicitly marked “shouldn't” in production and duplicated geometry types/Bezier/VM dependencies already supplied by narrow headers. |
| `partition_tesselator.h`: `../../geometry.h` | same copied header | Redundant include removed | `geometry_types.h` supplies the used arrangement vocabulary; this prevents external `geometry.h` from re-importing external Bezier and VM headers. |
| root `geometry.h` partition type/macro surface | `include/phoenix/partition/geometry.h` | Copied compatibility contract | Production `geometry::*`, payload, label sentinel, and `DEFAULT_*` macro definitions are retained. Only its Bezier include points at the localized helper, preventing VM re-entry. |
| `vm/randomizer.h` | `partition/ported/randomizer.h` | Copied implementation; includes made explicit | Production seed, clone, integer/double stepping, shuffle, and generator behavior are retained without VM ownership. |
| `partition_bezier::load(reader_ref)` | copied `partition_solver.h` | Excluded at compile time, body retained under `#if 0` | Binary/command model ingestion is outside the partition kernel. Property-tree loading remains available; Phoenix supplies linked immutable values. |
| `partition_model_view` | `null_diagnostics.hpp` | Debug-only spelling bridge | Pair alias exists only so untouched diagnostic call expressions type-check; the null sink never inspects it. |
| production root `stdafx.h` | `partition/ported/production/stdafx.h` | Phoenix boundary | Original include spelling is retained in all four `.cpp` files, but resolves to a partition-only prelude: standard library, used Boost facilities, exact geometry contract, CGAL compatibility, scalar context, and null diagnostics. Asio, filesystem, Nef/polyhedron, and application-wide headers are excluded. |
| `backend/segment_repository.h` partition-used members | `arrangement_segment_repository.{hpp,cpp}` | Temporary direct adaptation; source-copy comparison pending | Namespace/type spelling, Phoenix label predicates, RNG adapter. |
| `partition_solver.h`: `repo_segment_id`, `cut_segment_id`, `segment_info`, `angle_range`, `partition_view` | `trusted_solver_foundation.{hpp,cpp}` | Temporary transcription; supersede with adapted source | Namespace/type spelling and ownership only. |
| `partition_solver.cpp`: branching and view construction methods | `trusted_branching.{hpp,cpp}` | Temporary transcription; supersede with adapted source | Namespace/type spelling, immutable cut links, CGAL 6.2 intersection extraction, RNG adapter. |
| `partition_solver.cpp`: `partition_model::branch(view, cut, result)` | `trusted_branching.cpp` | Ported, awaiting source-shaped file consolidation | Immutable `TrustedCut`; `next()` RNG spelling; optional oriented lines replace invalid sentinel line; CGAL 6.2 optional variant point extraction; debug JSON removed. Loop/sampling/rejection/emission flow retained. |
| `partition_solver_constraints.cpp`: `restrict_cut_segment_length_base`, absolute and percentage workers | `adapted_constraints.{hpp,cpp}` | Ported | VM values supplied as immutable doubles; debug JSON and plan registration removed; endpoint/orientation/cumulative restriction body retained. |
| `partition_solver_constraints.cpp`: `restrict_cut_angle` | `adapted_constraints.{hpp,cpp}` | Ported | VM degrees supplied as immutable doubles; debug JSON and plan registration removed; reference branching and angle intersection flow retained. |
| `partition_solver_constraints.cpp`: `restrict_cut_distance_base`, absolute and percentage workers | `adapted_constraints.{hpp,cpp}` | Ported | VM values supplied as immutable scalars; debug JSON and plan registration removed; perpendicular direction, inter-cut reversal, minimum-then-maximum clipping, and failure notification retained. CGAL 6.2 variant extraction is mechanical. |
| `partition_solver_constraints.cpp`: `restrict_distance_extra` | `adapted_constraints.{hpp,cpp}` | Ported | Immutable cut lookup replaces pointers; diagnostics removed; collapsed-angle gate, child fallback selection, extreme-point choice, and parent range restriction retained. |
| `partition_solver_filters.h`: `base_length_pct_filter`, `base_angle_filter` | `ported/partition_solver_filters.h` | Mechanically adapted and compiled | Production names, member layout, build timing, comparison flow, and comments retained. Changes: namespace/type spelling, linked values replace VM lookup, `segment` replaces production `seg`, `angle_between` compatibility helper, model-registration methods remain outside kernel source. Source hash is embedded. |
| Earlier `adapted_filters.{hpp,cpp}` rewrite | quarantined, no longer compiled | Superseded | Retained temporarily only for worktree review; remove after the ported tree is established. |
| `segment_repository.h`: `repo_segment_id`; `partition_solver.h`: cut segment enum/wrappers, `segment_info`, `angle_range` | `ported/partition_solver_foundation.h`, `ported_partition_solver_foundation.cpp` | Mechanically adapted and compiled | Production names, layouts, bodies, tolerances, and comments retained. Type spelling is namespaced; repository `segment` replaces `seg`; const references are boundary-safe; intersection extraction is the documented CGAL 6.2 optional-variant change. |
| `partition_solver.h`: `partition_view` | `ported/partition_solver_foundation.h` | Mechanically adapted and compiled | Production field order and copy/reset/lookup/replacement/angle bodies retained. VM context removed; repository and invocation RNG are non-owning references; notifier becomes first-error structured evidence; empty `line2` sentinel becomes `optional<line2>`. |
| `partition_solver.h/.cpp`: instruction priorities and ordering | `plan.hpp` scheduled step descriptors | Boundary representation complete; executor pending | Exact numeric priorities retained; stable insertion order is explicit immutable data instead of mutable type-erased command ownership. |
| `partition_solver.cpp`: `partition_plan::advance` | `plan_executor.{hpp,cpp}` | Ported | Typed immutable payload dispatch replaces Boost variant callbacks; instruction increment, branch proposal failure, evaluator filtering, worker continue/fail, and terminal success semantics retained. Linked constraint/evaluator functions are invocation-local boundary objects. |
| `partition_tesselator.cpp`: straight `do_cut`/`insert_cut` slice | `straight_cut_tessellator.{hpp,cpp}` | Ported for one non-collapsed straight cut | CGAL arrangement split/insert API is direct; legacy ID allocation deferred to publication provenance; four directed boundary label pairs, cut labels, face-data copy, and left/right face labels retained. Recursion, collapsed sides, repeats, and Bezier cuts pending. |
| `partition_tesselator.cpp`: left/right `do_cut` recursion and result-face collection | `StraightCutTessellator::tessellate_tree` | Ported for straight non-collapsed cuts | Immutable cut IDs replace child pointers; each endpoint is resolved on its current parent-face boundary; left/right traversal and leaf-only face collection retained. |
| `partition_tesselator.cpp`: degenerate pieces and fully collapsed side | `StraightCutTessellator::tessellate` | Ported for straight cuts | Individual degenerate boundary pieces are accepted; double-side collapse fails; a single fully collapsed side reuses the coincident boundary, suppresses the absent face, and applies the surviving side's labels without overlapping insertion. |
| `partition_tesselator.cpp`: `distribute_by_count`, fixed-value `distribute_by_length` | `repeat_distribution.{hpp,cpp}` | Ported | VM/range randomization is resolved before entry; production margin defaults, count equations, adjustment modes, maximum-cut clamp, side-dependent primary lengths, and not-enough-space failures retained. |
| Rewritten solver/constraint/executor/tessellator/repeat files | existing `trusted_*`, `adapted_*`, `plan_executor`, `straight_cut_tessellator`, `repeat_distribution` | Quarantined behavioral scaffolding | These are fixture oracles only and are not accepted port progress. No further algorithm work is permitted in them. |
| `partition_tesselator.cpp`: interpolated `compass_labels::apply` semantics | `apply_repeat_region_labels` | Ported for non-collapsed interpolation strips | Each emitted face boundary is classified as lower/upper/source/target; current side and twin/opposite labels are independently applied, including inherited defaults supplied by linked inputs. Collapsed compass overloads remain covered by collapsed straight cuts, not repeats. |
| `partition_tesselator.{h,cpp}` | adapted production source | Pending | Publication, ID allocation, and 3D lift are boundary changes only. |

## Rules for Future Entries

- Copy the audited production function before editing it.
- Keep its statement ordering and control flow unless an intentional deviation is
  separately approved and fixture-backed.
- Record namespace, type, ownership, VM, randomizer, diagnostics, and CGAL API
  edits explicitly; “cleanup” is not a classification.
- Retain temporary transcriptions until their source-shaped replacements compile
  and pass the same fixtures, then remove the superseded path.
