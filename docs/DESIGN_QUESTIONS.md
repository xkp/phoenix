# Execution Engine Design Questions

Use this file to answer the open design questions for the command-line geometry
execution engine. Fill in each `Answer:` section and then tell me to review it.

## 1. Runtime Data Model

What exactly is an input or output value at runtime?

Examples:

- generic typed object
- file reference
- in-memory geometry object
- serialized blob
- something else

Answer: 

Excelent question, as originally designed, inputs are 3d faces and instructions could either processes them as meshes or run once per face. In this iteration we need to keep tha mechanism where there is an "special" input called "input" that multiplexes the work per input item (again, can be faces in a mesh, or not).

Other than that, we should accept at least literal elements that if the inst i sforced to run it can use default values set yb the user. Functions can also return literals. Poco objects will be added later.


## 2. Ports And Types

Are instruction input ports named and typed?

Example:

- an instruction declares inputs like `mesh_a`, `mesh_b`, `tolerance`
- each incoming edge targets a specific input port

Answer:

Yes, typed and named, with the particulars that geometry inputs can always have multiple inputs that are compunded in a virtual "mesh". For non-geometric, it must be typed and arrays shoul dbe permitted as types.  


## 3. Readiness Rule

What counts as "all inputs ready" for a normal instruction?


Please clarify:

- whether every input is required

Special inputs are (input in this case, cant run without primary input). The other inputs should be considered not required unless they specify so. For literal inputs a default value should be provided.

- whether optional inputs exist

Yes

- whether defaults exist

Yes, for literals, not for geometry.

Answer:

An instruction has n inputs and gets updated everytime the output of another instruction is connected to its inputs. When every input has notified results the instruction is ready. While not all the inputs are realized we must keep state.


## 4. Equilibrium Rule

When the graph reaches equilibrium, how do we choose the "most independent
pending instruction" to force-run?

Possible choices:

- the pending instruction with the fewest missing inputs
- the one with the most inputs already received
- the one farthest downstream
- the one with no pending predecessors
- another rule

Answer:

the one with no pending predecessors
if all have predecesors we need to consider if its even valid, but if it is then we need a predicatble way to decide. 


## 5. Missing Inputs At Forced Execution

When a pending instruction is force-run at equilibrium, how should missing
inputs be represented?

Possible choices:

- `null`
- empty value
- omitted entirely
- explicit presence mask
- instruction-specific default

Answer:

instruction-specific default, if exists. Other than that, empty value.


## 6. Partial Outputs

Can an instruction produce partial outputs when some inputs are missing?


Please clarify:

- yes or no
- if yes, how the instruction indicates which outputs are valid

Answer:

Yes, the function will end by the criterion given, at that point the special "output" instruction will use its current pending state as return value. 


## 7. Graph Shape

Are graphs always acyclic, or should cycles / feedback loops be supported?

Answer:

No feedback loop, there is also a special "loop" instruction to be defined later.



## 8. Function Node Semantics

What is a function node?

Possible interpretations:

- a reusable named subgraph
- a nested graph invocation
- a special instruction implemented in code
- something else

Answer:

A program is a function, in the same sense as in programming languages. A function has its own graph, inputs and outputs.


## 9. Parallel Execution Semantics

Can multiple ready instructions run in parallel with no ordering guarantees?


Please clarify:

- whether deterministic execution order matters
- whether deterministic output values matter

Answer:

Absolutely, the graph creates the ordering (a ready instruction can only be ready on one state and doesnt matter how we get there)

Deterministic execution order matters, much.

## 10. Error Handling

What should happen when an instruction fails?

Possible choices:

- stop the whole function immediately
- continue unrelated branches
- collect errors and return partial results
- configurable behavior

Answer:

Right now we habdle it via a "else" special output that transmit the input. For this version I also want an instruction to be able to throw which would stop its own function and throw up the callstack until an "else" is called. Otherwise the program ends in its current state.



## 11. Side Effects

Are instructions pure transformations, or can they have side effects?

Examples of side effects:

- writing files
- invoking external tools
- mutating shared state
- network access

Answer:

No, no side effects, uberly important.


## 12. Execution Boundary

Should the first version execute instructions:

- in-process only
- as external processes only
- both

Answer:

For now, in process, we should keep external processes in play as well since the arch is designed for long running tasks.


## 13. Result Collection

Results are taken from the connections into an output node. Please clarify:

- whether output node ports are named
- whether output order matters
- whether unfulfilled output ports are omitted or returned as empty values

Answer:

output node ports are named,  output order does not matter, unfullfilled should be returned as empties.


## 14. Pending Instruction Definition

You defined a pending instruction as one that has received input but still
awaits the full set. Please confirm whether this is exact, and clarify:

- does an instruction with zero received inputs count as pending
- can an instruction go from pending back to idle

Answer:

It is correct. an instruction with zero received values is not pending. Instructions are ran only once, theyo go from pending to executing to "will never run again on this function run"


## 15. Scheduling Priorities

If several instructions are ready at the same time, do you want a scheduling
priority rule?

Possible choices:

- FIFO
- topological order
- explicit priority number
- shortest estimated runtime
- no special rule

Answer:

No special rule until we knw better.


## 16. Minimal First Version

For the first implementation, what is the smallest useful scope?

Example scope options:

- execute an in-memory graph with mock instructions
- support only acyclic graphs
- no external process execution yet
- single data type first, then generalize

Answer:

We have many many examples of the language. The minimum value would be to run a simple program.


## Follow-Up Questions From Review

These questions came up while reviewing the answers above. Please answer them so
the execution model can be made precise enough to implement confidently.

## 17. Determinism Versus Parallelism

You said instructions should run in parallel when possible, but you also said
deterministic execution order matters a lot.

Please choose which of these is the real requirement:

- deterministic final results only
- deterministic scheduling decisions, even if execution happens in parallel
- deterministic execution trace and ordering
- another rule

Answer:

deterministic final results only


## 18. Exact Readiness Rule

Please define readiness in one exact sentence.

Possible interpretations:

- an instruction is ready when all inputs have values
- an instruction is ready when all required inputs have values
- an instruction is ready when the special `input` port has a value and all
  other required ports have values

Answer:

an instruction is ready when all inputs have values


## 19. Equilibrium Tie-Breaker

At equilibrium, several pending instructions may have no pending predecessors.
How should the runtime choose one deterministically?

Possible tie-breakers:

- smallest node id
- graph declaration order
- topological order
- explicit priority field
- another rule

Answer:

smallest node id


## 20. Normal Instruction Partial Outputs

When a normal instruction is force-run with missing inputs, how does it report
which outputs are valid?

Possible choices:

- each output carries a present / absent flag
- absent outputs are returned as empty values
- the instruction returns an explicit output mask
- instruction-specific behavior

Answer:

The output is no more than the input of the "output" instruction. Whatever is there, incuding missing as empty values or default for non geometry.


## 21. Error And `else` Semantics

Please define exactly how `else` works.

Questions to answer:

- is `else` an output port on every instruction

tbd

- is `else` only present on some instructions

most likely

- does throwing route control through an `else` edge

Yes, you route throwing throuhg 'else', "do this but if it fails, do this other op"
Keep in mind that else has the value of the "input".

- what happens if an instruction throws and no `else` path exists

If it is configured to throw, does so, otherwise the error ends there.

Answer:

Answered individually.


## 22. Runtime Value Representation

Please make the runtime value model more concrete.

Questions to answer:

- what is the base runtime value type

for now, the "mesh", that what goes through inputs and outputs.

- how is a geometry collection represented

tbd

- how is the special `input` multiplexing represented internally

if (instruction.mutiplexes)
{
    foreach(face f in input)
       instruction.run(f)    
}
else 
       instruction.run(input)    

- how are literals represented

however needed.

- how is an empty value represented

however needed.


Answer:
Answered individually.


## 23. First-Version Execution Scope

Please confirm the implementation scope for version one.

Suggested interpretation:

- in-process execution only
- no feedback loops
- external process execution postponed
- enough built-in instructions to run one simple real program

Answer:

Confirmed.


## Additional Follow-Up Questions

These are the last major gaps identified in the second review. Once these are
answered, we should be in good shape to write a concrete execution spec.

## 24. Exact `else` Port Model

We know that throws should route through `else` when available, but the graph
structure is still not fully defined.

Please answer:

- does every instruction type have an `else` output port

if needed, yes.

- or do only some instruction types expose `else`

There could be the case where we dont want to thow. For instance, an "if" instruction.

- if only some do, how is that declared in the instruction definition

As needed, can be a flag.

- can `else` be connected only once, or to multiple downstream inputs

List any other inst.


Answer:

Above.


## 25. Ready State With Optional Inputs

There is still a conflict between:

- "optional inputs exist"
- "an instruction is ready when all inputs have values"

Please choose one exact rule:

- ready when all declared inputs have values
- ready when all required inputs have values
- ready when all required inputs have values, and optional literal inputs are
  filled from defaults before execution
- another exact rule

Answer:

The rule is not about the values, the rule is about incoming values.
Our instructions are simply awaiting for all the inputs *they were promised*.
When all those promises are fullfilled, the instruction is ready.

"optional" inputs are those that can have no connection from another instruction.


## 26. Missing Versus Empty Versus Default

The runtime needs to distinguish these states precisely.

Please define the difference between:

- missing value
  if nothing is returned through an output nothing gets dispatched.  

- empty value
  no geometry came out of the "output", instructions would normally do not react top empty input.
  but they could. 

- default value
  for non-geomtry, say an int output, there could be default values stored in the inst.


And please answer:

- can a port receive a missing value as actual data

no, missing values do not get dispatched.

- or is missing only an internal engine state

not sure what you mean but I dont think so.

Answer:

Above.


## 27. Output Presence Model

For normal instructions, we still need a precise way to represent whether each
output port produced a value.

Please choose one:

- each output port has a present / absent flag
- every output port always has a value, and absence is represented by empty
- instruction returns a separate output mask
- another rule

Answer:

instructions return the values that got fulfiled on its "output" instruction. 
each output port has a an input value. If no fullfilmentL then missing value, if "output"'s input port gets nothing return empty value there (empty mesh)


## 28. Geometry Collection Representation

The runtime currently assumes mesh-like geometry, but the representation is
still `tbd`.

Please clarify:

- what concrete runtime type represents a geometry collection
- whether a single face and a mesh use the same wrapper type
- whether "virtual mesh" is a materialized merged mesh or a logical grouped view

Answer:

Those are implementation concerns to be reoslved at implementation time.


## 29. Literal Representation

Please define the minimal literal types required in version one.

Examples:

- integer
- floating point
- boolean
- string
- arrays of literals

Answer:

Those are fine.


## 30. Invalid Equilibrium Case

If the system reaches equilibrium and every pending instruction has a pending
predecessor, what should happen in version one?

Possible choices:

- this is a graph validation error and execution should stop
- this is a runtime deadlock error
- choose the smallest node id anyway
- another rule

Answer:

this is a graph validation error and execution should stop, we'll see there.


## Final Clarification Questions

These are the last two open points before the execution model can be written as
an implementation-ready spec.

## 31. V1 `else` Port Rule

Please choose one exact rule for version one:

- every instruction has an `else` output port
- only some instructions have an `else` output port

If only some instructions have it, please also answer:

- how the instruction definition declares that `else` exists
- whether graph validation should reject an `else` connection on instructions
  that do not expose it

Answer:

Fine, everyone has an else. 


## 32. Canonical Missing / Empty / Default Semantics

Please confirm whether this exact wording matches your intent:

- missing: no value was dispatched on that port
    on missing outputputs it means no value will be dispatched through that output unless its the "output"

- empty: a value was dispatched, but it represents empty geometry or an empty
  container

  correct 

- default: the engine injected a configured literal value because no upstream
  value was connected or available

  correct

If not, please rewrite these three definitions in your preferred wording.

Answer:

Should be enough.