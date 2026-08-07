# Actor Design Questions

Use this file to answer the open design questions for actors, hierarchy, and
instancing. Fill in each `Answer:` section and then tell me to review it.

## 1. Actor As Runtime Value

Is an actor:

- a runtime value type
- a function output type
- both

Answer:

And actor, and its hierarchy, are the actual result of the process. As the vm is running the code, it will find that it needs to run a function marked as an actor. It does so creatting a node in the actor hierarchy and adding all the geometry (and children actors) produced by runng the function.

The result actor is added as a chil to te current actor presiding over the run of the inner actor function.


## 2. Actor-Generating Functions

When a function is marked to generate actors, does it:

- return one actor
- return many actors
- return a subtree rooted at one actor

Answer:

One actor, but multiplexed function will create N actors pero instruction call.
Each internally with a hierarchy.


## 3. Top-Level Root

Does the top function always produce:

- exactly one root actor
- one or more top-level actors

Answer:

One top level root.


## 4. Hierarchy Construction

What creates the actor hierarchy?

Possible models:

- parent function creates parent actor and child functions create child actors
- graph instructions explicitly assemble the hierarchy
- both

Answer:

parent function creates parent actor and child functions create child actors


## 5. Instancing Semantics

What does "instanced" mean in this model?

Possible interpretations:

- instances share geometry and materials, but have different transforms
- instances share geometry only
- instances allow per-instance overrides
- another rule

Answer:

instances share geometry and materials, but have different transforms
possibly: instances allow per-instance overrides


## 6. Pivot Ownership

Is the pivot point part of:

- the actor
- the geometry
- both

Answer:

the actor, although its contruction will be based on the input geometry.


## 7. Local Space

When geometry is relative to the pivot, do you mean:

- geometry is stored in actor-local space
- actor transform places local geometry in parent/world space
- another rule

Answer:

geometry is stored in actor-local space, possibly one file per actor.
plus scene.


## 8. Actor Payload

What can an actor directly own?

Please clarify whether an actor has:

- one transform
- zero or one geometry payload
- zero or one material set
- multiple geometry or material attachments

Answer:

one transform
zero or one geometry payload
we will not do materials for now.
zero or more child actors


## 9. Geometry Without Actor

Can non-actor-generating functions still return geometry that later gets wrapped
into an actor by another function?

Answer:

Yes, everytime some geometry is produced it gets added to the current actor geometry.


## 10. Hierarchy And Evaluation

Is actor hierarchy:

- purely structural
- or can child generation automatically depend on parent transform/material
  context

Answer:

not sure what you mean. But I will go with  purely structural.


## 11. Actor Identity

Are actor names or stable ids part of the requirements already?

Please clarify:

- does every actor need a stable id
- does every actor need a name
- are names unique

Answer:

actors may or may not have a name, always an id. Stable and predictable.


## 12. Determinism Of Actor Generation

Should actor generation be deterministic in:

- final content only
- hierarchy order too
- both

Answer:

both


## Follow-Up Questions

These questions came up during review and focus on the remaining actor-specific
ambiguities.

## 13. Instancing Overrides In Version One

You said instances share geometry and materials, but possibly may allow
per-instance overrides.

Please clarify the version one rule:

- instances share geometry and materials, with no overrides
- instances may override transform only
- instances may override a defined subset of fields
- another rule

Answer:
Let's go with no overrides for now.


## 14. Geometry Accumulation Rule

Please confirm whether this exact rule matches your intent:

- while an actor-generating function is active, geometry produced by regular
  instructions is accumulated into the current actor

  correct

- when an inner actor-generating function runs, it creates a child actor under
  the current actor

  correct 

- geometry produced inside that inner function belongs to that child actor

  correct 

If not, please rewrite it in your preferred wording.

Answer:

correct


## 15. Actor Identity Stability

You said actors always have a stable and predictable id.

Please clarify:

- should actor ids be deterministic across identical runs
- should actor ids depend on the random seed
- should actor ids depend only on graph structure and deterministic execution
  context

Answer:

When we run a program with the same seed an dinput twice the geometrical hierachy should be identical.
That's the only constraint.


## 16. Runtime Requirement Versus Export Format

For actor-local geometry, please separate runtime behavior from file/export
layout.

Please clarify:

- is "geometry stored in actor-local space" a runtime requirement

yes

- is "one file per actor plus scene" only an export/output possibility

yes, only one way to do it.

Answer:

above.


## Additional Instancing Requirement

Example requirement:

- if a large building contains many identical windows, the system should be able
  to run the window-generating function once and then place that resulting actor
  multiple times as instances, instead of rerunning the same function for every
  window

This suggests a distinction between generating an actor once and placing that
actor many times.

## 17. Prototype Versus Instance Identity

When an actor is generated once and instanced many times, what should happen to
identity?

Please clarify:

- does the generated actor become a reusable prototype/definition

yes

- does each placed instance get its own actor id

not sure, lets say yes.

- can multiple instances share the same underlying generated actor content

yes, that is the whole point.

- is instance placement considered separate from actor generation

yes.

Answer:

above.
