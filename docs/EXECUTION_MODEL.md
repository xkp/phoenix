# Execution Model

This document defines the version one execution model for the command-line
geometry program runtime.

It is based on the decisions captured in `DESIGN_QUESTIONS.md` and is intended
to be precise enough to guide implementation while still leaving some low-level
data structures open.

## 1. Scope

Version one includes:

- in-process execution only
- no feedback loops
- graph-based instruction scheduling
- parallel execution of independent ready instructions
- deterministic final results
- geometry-first runtime values
- literal support for basic non-geometry values
- function-local error propagation through `else`

Version one does not include:

- external process execution
- general POCO/object payloads
- loop semantics
- concrete mesh container standardization in this document

## 2. Core Concepts

### 2.1 Program And Function

A program is a function.

A function has:

- a graph
- named input ports
- named output ports
- instruction nodes
- edges between output ports and input ports

A function invocation executes exactly one graph instance for one set of inputs.

### 2.2 Instructions

An instruction is a node in the graph.

Each instruction:

- has a stable node id
- has named input ports
- has named output ports
- always exposes an `else` output port in version one
- runs at most once per function invocation

Instruction lifecycle:

- idle: no received input values
- pending: at least one promised input has received a value, but not all
  promised inputs have been fulfilled
- ready: all promised inputs have been fulfilled
- executing: currently running
- completed: ran and will never run again in this function invocation

### 2.3 Promised Inputs

Readiness is based on promised inputs, not all declared inputs.

A promised input is an input port that is expected to receive a value during the
current function invocation. In version one, this means:

- an input port connected from an upstream output is promised
- an unconnected input port is not promised
- unconnected literal inputs may still have configured defaults

An instruction becomes ready when all promised inputs have been fulfilled.

## 3. Runtime Values

### 3.1 Value Families

Version one supports two main value families:

- geometry values
- literal values

Geometry is the primary runtime family in version one. Literal support exists
for parameters and return values such as integers, floating-point values,
booleans, strings, and arrays of literals.

### 3.2 Missing, Empty, And Default

These states are distinct.

- missing: no value was dispatched on a port
- empty: a value was dispatched, but it represents empty geometry or an empty
  container
- default: the engine injected a configured literal value because no upstream
  value was connected or available

Missing is not actual data. A missing value is an execution-state condition in
which nothing is dispatched through a port.

Empty is actual data. An empty geometry value is still a real dispatched value.

Default is actual data created by the engine from instruction configuration.

### 3.3 Geometry Representation

This document does not standardize the concrete in-memory mesh container for
version one.

However, the execution model assumes:

- geometry can be passed as a single runtime value
- multiple geometry inputs may be logically compounded into a virtual mesh
- multiplexing may also process geometry item-by-item

The exact container type is an implementation concern.

## 4. Ports And Typing

All instruction ports are named and typed.

Rules:

- each incoming edge targets one specific input port
- non-geometry ports are strongly typed
- arrays are valid types
- geometry inputs may accept multiple upstream geometry contributions that are
  treated as one virtual geometry input at execution time

Output node ports are named. Output order does not matter.

## 5. Scheduler

### 5.1 General Rule

Any instruction whose promised inputs are all fulfilled is ready to run.

Ready instructions may run in parallel.

The runtime does not require deterministic execution order. It requires
deterministic final results.

Therefore:

- instruction behavior must not depend on thread timing
- random behavior must be seed-driven
- scheduling order must not affect final observable outputs

### 5.2 Pending Instructions

A pending instruction is one that:

- has received at least one promised input value
- has not yet received all promised input values

An instruction with zero received values is not pending.

### 5.3 Equilibrium

The system is in equilibrium when:

- no instruction is ready to run
- the function has not yet finished

At equilibrium, the runtime selects one pending instruction to force-run.

Selection rule:

1. Consider only pending instructions with no pending predecessors.
2. Select the instruction with the smallest node id.

If equilibrium is reached and every pending instruction has a pending
predecessor, this is a graph validation error in version one and execution must
stop.

### 5.4 Completion

A function invocation completes when both of the following are true:

- the system is stable, meaning no further instructions can run
- there are no pending instructions left to force-run

At that point, the function result is collected from the special output node.

## 6. Forced Execution

When an instruction is force-run at equilibrium:

- any missing promised literal input may be replaced by its configured default,
  if one exists
- otherwise missing inputs are treated as missing and may result in empty output
  behavior depending on instruction semantics

Version one assumes instructions can be written to tolerate this mode where
appropriate.

## 7. Output Semantics

### 7.1 Normal Instruction Outputs

Each output port may end up in one of these states:

- missing: no value was produced on that output port
- fulfilled with a concrete value
- fulfilled with an empty value

Conceptually, each output port has presence semantics, even if the final
implementation does not store it as an explicit boolean flag.

### 7.2 Function Output

Function results are taken from the connections into the special output node.

Rules:

- output node ports are named
- output order does not matter
- an unfulfilled output port returns an empty value
- the output node may expose partial state when the function ends

## 8. Error Handling And `else`

Every instruction exposes an `else` output port in version one.

If an instruction fails:

- it may throw
- failure is routed through `else`
- the `else` path receives the instruction input context as its value

This supports the pattern:

- do this operation
- if it fails, continue through `else`

If an instruction throws and no usable `else` path handles the failure, the
current function throws upward through the call stack.

If no higher-level function handles the throw, the program ends in its current
state.

## 9. Multiplexing

Some instructions may multiplex over the special `input` value.

Conceptually:

```cpp
if (instruction.multiplexes) {
    for (auto item : input) {
        instruction.run(item);
    }
} else {
    instruction.run(input);
}
```

The scheduler treats the instruction as one node, but the instruction may
internally apply its logic once per item.

Version one does not fully standardize what counts as an item beyond the general
idea of per-face or per-element geometry processing.

## 10. Reproducibility And Randomness

### 10.1 Global Reproducibility Rule

A function run is governed by a global seed.

Given:

- the same graph
- the same inputs
- the same instruction implementations
- the same configuration
- the same global seed

the final results must be identical.

### 10.2 Derived Instruction Seeds

Instruction randomness must be derived deterministically from the global seed.

The runtime should not allow thread timing or execution order to affect random
results.

Seeds are intended to initialize random number generators. This means the
runtime must guarantee that the same effective seed produces the same random
number sequence on each run, assuming the same generator algorithm and the same
instruction behavior.

A suitable model is:

- derive an instruction seed from the global seed and stable instruction identity
- use that derived seed whenever the instruction needs randomness

Conceptually:

```text
instruction_seed = derive(global_seed, function_call_path, instruction_id)
```

The exact derivation function is an implementation concern, but it must be
stable and deterministic.

The same requirement applies to RNG construction:

- the RNG algorithm used by an instruction must be fixed for reproducibility
- the RNG must be initialized from the derived seed in a deterministic way
- instruction code must consume random values deterministically for identical
  inputs and configuration

### 10.3 Multiplex Seed Modes

Multiplexing instructions support two seed modes:

- one seed for all
- one seed each

In `one seed for all` mode:

- every multiplexed item uses the same derived instruction seed

In `one seed each` mode:

- each multiplexed item gets a deterministic per-item derived seed

Conceptually:

```text
item_seed = derive(instruction_seed, item_key)
```

The `item_key` must be stable for reproducibility. Version one should prefer a
stable item index or another stable item identity derived from the input value.

This ensures that multiplexed execution can still recreate the same per-item
random streams on every run.

### 10.4 Local Seeds

An instruction may also have its own local seed configuration.

When present, that local seed must still participate in deterministic seed
derivation rather than introducing nondeterministic runtime randomness.

A suitable model is:

```text
effective_instruction_seed =
    derive(global_seed, function_call_path, instruction_id, local_seed)
```

## 11. Validation Rules

A version one graph must satisfy at least the following:

- no feedback loops
- all connected ports are type-compatible
- all node ids are stable and unique within the function
- all required execution paths are structurally valid
- equilibrium cycles where every pending node depends on another pending node are
  rejected as invalid

Additional validation may be added by implementation.

## 12. Version One Implementation Notes

The following are intentionally left open for implementation:

- concrete mesh and geometry collection types
- literal storage layout
- exact internal representation of missing versus empty versus present states
- exact seed derivation algorithm
- exact virtual mesh materialization strategy

These are not open behavioral questions. They are implementation choices that
must preserve the semantics defined in this document.
