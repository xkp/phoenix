# Phoenix Scripting Contract

## Production Evidence

| Source | SHA-256 | Evidence |
|---|---|---|
| `vm/variable.cpp` | `DD6431DB68DEB32674B80CBDBBC83E7163BA4395047B7FED713CE3A254D557C4` | expression preprocessing, bindings, result conversion, errors |
| `commands/case_command.h` | `2A8900E2CDD50DE27BBB16B2F6DC1C87D1E379AC65D3F1CA32F77AA7DAD88A84` | ordered first-match conditions and `else` |
| `commands/script_command.h` | `E0E48F99DF7952007E63A45F93690FA7E8CF39C82CBE90154E4E31588844731C` | general script surface and its host coupling |
| `commands/if_command.h` | `9FC87FFB70EC6EF5C3999A35C9DEFEA7B36B648F7A9A80195AAEE146DD290140` | boolean/numeric truthiness |

Production V8 behavior is compatibility evidence, not the Phoenix runtime
boundary. Persisted engine bytecode and V8 host objects are not port targets.

## Version-One Expression Boundary

An expression program has source text, language identifier, and language
version. Inputs are immutable named bindings containing signed 64-bit integer,
double, boolean, or UTF-8 string values. Instruction-local bindings shadow
function/global bindings explicitly. Binding identifiers use
`[A-Za-z_][A-Za-z0-9_]*`; migration must map production bracketed names that
fall outside this set rather than injecting them into source unsafely.

Expression results use the same scalar set. Arrays, objects, functions, host
handles, geometry objects, `undefined`, `null`, big integers, symbols, and
promises are unsupported as expression results in version one.

## Geometry Script Boundary

Production also has a materially different `script` instruction. It receives
geometry-bearing inputs, variables, and labels; scripts can create or modify 3D
geometry and publish named scalar or geometry outputs. That capability is a
required port target, not an expression-mode extension.

Phoenix exposes it through the separate `ScriptEngine::execute_script` and an
invocation-local `GeometryScriptHost`. Scripts may inspect canonical 3D input
vertices, faces, face labels, and directed edge labels. They may create empty
output geometry or clone an input into an output edit session, then add/move
vertices, add/remove faces, and change face or directed-edge labels. They never
mutate a shared `CanonicalGeometry` object directly.

The host records edits privately. Only a completely successful script may
validate, assign stable run IDs, lift its outputs into canonical geometry, and
publish them. An exception, cancellation, invalid topology, invalid label, or
budget failure discards the entire edit session and leaves source geometry
untouched. A future consuming option must use the normal publication ledger and
may consume only after all outputs validate.

Production supported 2D and 3D values; Phoenix ports only canonical 3D. Script
includes become versioned immutable library assets resolved before execution,
never filesystem includes. Console output is bounded diagnostic text.

`td.vars` and `td.labels` remain the compatibility namespaces. `td.inputs` is a
keyed collection of all named input ports; production's accidental first-input
behavior is not preserved. Named vertex/halfedge/face list outputs remain
supported, while output geometry must be explicit rather than leaking from a
hidden creation collector. Details are pinned in
`PORTING_SCRIPT_BINDINGS_INVENTORY.md`.

## Determinism, Isolation, And Budgets

- no filesystem, network, process, environment, dynamic module, wall-clock, or
  locale-dependent host access
- no ambient randomness; future random functions consume only the explicit
  Phoenix deterministic seed
- immutable bindings and no state retained between evaluations
- instruction, memory, and recursion budgets are mandatory engine inputs
- cancellation is observable through the engine adapter
- failures return stable Phoenix diagnostic codes with optional source location
- compiled artifacts are ephemeral caches keyed by language/version, source,
  engine/version, effective bindings, seed, and limits
- persisted projects store source and Phoenix language version, never compiled
  bytecode or engine-specific state

## Engine Adapter

`phoenix::scripting::Engine` and its geometry-capable `ScriptEngine` extension
are the only engine-facing interfaces. An engine
candidate must pass the same conformance corpus on Windows, Linux, and Apple
Silicon and demonstrate hard interruption and memory enforcement. Engine
selection follows the corpus inventory; this contract does not select one.

## Delivery Slices

1. S0 complete: typed contract, validation, diagnostics, cache fingerprint,
   budgets, cancellation interface, and production evidence.
2. S1 expression core complete: engine-neutral conformance corpus and migration rewrites for
   production bracket variables. No persisted expression corpus was present in
   the inspected source tree, so these cases pin observed production language
   entry points and must be augmented from real project files during P12.
3. S1G: inventory the production 3D script host API and add geometry creation,
   mutation, labels, invalid-topology rollback, cancellation, and budget cases
   to the conformance corpus. The first binding classification is recorded in
   `PORTING_SCRIPT_BINDINGS_INVENTORY.md`.
4. S2: spike shortlisted JavaScript engines behind both adapters and record
   compatibility, sandbox, interruption, footprint, licensing, and platform
   results.
5. S3: select and integrate one engine; add compiled-program caching and runtime
   telemetry.
6. S4: enable expression `if`, ordered `case`, and loop variable expressions.
7. S5: port transactional 3D `script`, then enable expression modes in select,
   rename, profiles, and instance.
