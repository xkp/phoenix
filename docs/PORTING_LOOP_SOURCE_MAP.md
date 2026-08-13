# Loop Port Record

## Trusted Production Source

| Source | SHA-256 | Role |
|---|---|---|
| `commands/loop_command.h` | `BD983AC4DEFF46A7696088D92FEDB3B31DD5BA99B9AE191FEEF08819DECA0FF5` | Iteration, feedback, accumulation, index, variables, and completion behavior |
| `loaders/loop_loader.h` | behavioral evidence | Persisted count/range/step and loop-variable fields |
| `loaders/binary_loader/binary_loop_loader.h` | migration evidence | Binary descriptor routing |
| `vm/randomizer.h` | behavioral evidence | Inclusive range and step selection |

The callback-based VM implementation is evidence, not a port target.

## Production Behavior

- `count` is the default maximum iteration count.
- When `range >= 0`, production chooses an inclusive value between `count` and
  `range`; a positive `step` restricts the selectable values.
- A missing input or a non-positive chosen count produces no output.
- The first `loop` link identifies the loop body entry.
- Each body run may emit `loop` for the next iteration and `all` or `output` for
  accumulation. `all` takes precedence over `output`.
- Before the next iteration, the feedback value is removed from accumulated
  output.
- Missing feedback terminates early.
- `$index` is zero for the first iteration and increments before subsequent
  iterations. Production also supplies `_index` to loop-variable expressions.
- Production passes iteration seeds `1, 2, ...` to body execution.
- Config values are propagated from the original input.

## Phoenix Boundary

Loop is a special bounded nested-execution construct. It does not introduce
arbitrary feedback edges into `FunctionDescriptor` graphs. The initial core is
engine-independent and invokes an explicit body callback once per iteration.

Required safety and determinism:

- configured and global hard iteration budgets are checked before work starts
- the selected count and every iteration seed derive deterministically from the
  run seed, call path, loop node, and iteration index
- accumulation order is iteration order
- an item failure returns no successful loop result
- geometry publication remains invocation-local until the complete loop commits
- expression-updated variables are deferred to the scripting contract

Exact legacy RNG stream compatibility is not claimed. Persisted count/range/
step semantics are preserved using Phoenix deterministic seeds.

## Slices

1. L0: source record and bounded callback runtime
2. L1: nested Phoenix function-body adapter and typed `$index`
3. L2: transactional geometry publication and consumption across iterations
4. L3: cache identity, partial reruns, tracing, diagnostics, and budgets
5. L4: participate in the general P12 production-descriptor migration after
   Phoenix persistence boundaries are defined; do not choose a loop-specific
   JSON, binary, or native storage format here
6. L5: expression-updated variables after scripting

## Current Checkpoint

L0-L1 are implemented. `phoenix::loop::run` owns bounded iteration and
`make_function_body` adapts each iteration to a complete acyclic Phoenix
function invocation. The adapter supplies the feedback value on `input`, a
typed 64-bit integer `$index`, a deterministic iteration seed, and a unique
`loop:<node>:<index>` call-path segment. It reads feedback from `loop` and
accumulates `all` before falling back to `output`; Phoenix empty output-port
sentinels are treated as absent values. Nested execution failure aborts the
whole loop and discards accumulated results.

L1 defaults to no nested publication ledger. Only the L2 transaction adapter
may supply a loop-owned private ledger; callers must never pass the outer/global
ledger directly to an iteration body.

L2 is now implemented as `run_geometry_transaction`. Iterations publish only
to a loop-owned private ledger. After full success, accumulated geometry is
collapsed in iteration order into one outer effect that consumes the original
source faces. Intermediate generated face IDs are never registered as global
actor sources. Any iteration failure discards the private ledger and returns an
unsuccessful effect with no geometry and no consumption. A successful loop with
no accumulated geometry emits no replacement and consumes nothing, matching
production's empty-output behavior.

L3 is implemented at the outer loop instruction boundary. Phoenix caches the
collapsed output/effect as one instruction result, so cache hits do not invoke
or trace the body again. Loop bounds, budgets, and body function identity have
a stable `configuration_revision` included in the generic graph revision;
option changes therefore invalidate instruction, function, subtree, and
partial-rerun identities. A changed-scope failure replaces the prior successful
scope and restores the original unconsumed geometry. Loop trace events cover
count selection, completed iterations, early termination, failure, total work,
and completion. Bodies report work units, enforced in addition to the hard
iteration limit.

The L3 fixture exercises the actual partial-rerun planner, scope resolver, and
applier: cached replay performs no body work, while a changed failing loop
restores the original actor source and clears prior consumption. Runtime
failures carry the zero-based failing iteration as their item key, including
body failure and work-budget exhaustion. Linux GCC and Apple Silicon Clang
debug/release jobs run all four loop suites in `loop-platforms.yml`.

L4 is deferred to the general P12 persistence/migration design. The production
JSON and binary loaders remain behavioral/schema evidence only; this loop port
does not prescribe Phoenix's eventual persistence format. L5 expression-updated
variables remains blocked on scripting.
