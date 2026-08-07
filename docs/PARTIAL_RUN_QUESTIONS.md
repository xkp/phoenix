# Partial Run And Incremental Update Questions

Use this file to answer the open questions about partial runs, invalidation, and
scene updates. Fill in each `Answer:` section and then tell me to review it.

## 1. Change Types

Which changes must support partial rerun in version one?

Possible examples:

- parameter changes
- input geometry changes
- function body changes
- graph wiring changes
- seed changes

Answer:

All of the above, although it is understood some changes may require a full run, like changing the global seed or the input geometry for everything


## 2. Rerun Unit

What is the smallest unit the runtime should rerun?

Possible choices:

- one instruction
- one actor-generating function
- one actor subtree
- another unit

Answer:
  one actor subtree, after a single instruction is changed there is a cascade of "dirty" functions and instructions that will have to be recalculated.



## 3. Invalidation Boundary

When something changes inside an inner actor, what should be invalidated?

Possible interpretations:

- only that actor-generating function
- that actor and all descendants
- that actor, descendants, and dependent parents
- another rule

Answer:
  that actor and all descendants, but depending what instructions get dirty by the change. Some chages will not touch descendants. 
  if the actor has outputs other than the actor itself, yo must keep propagating to parent.



## 4. Scene Update Semantics

After a partial rerun, how should the scene be updated?

Possible choices:

- replace the affected actor subtree in place
- merge newly produced geometry into the old subtree
- rebuild the whole parent actor
- another rule

Answer:
  whatever is needed to reflect the changes. But for now we will do it in place.



## 5. Actor Id Stability Under Partial Rerun

When a subtree is partially rerun, what must remain stable?

Please clarify:

- must unaffected sibling actor ids remain stable
  yes

- must unaffected ancestor actor ids remain stable
  yes

- can rerun actors receive new ids
  yes, but it is not preferred.
  

Answer:
  above.



## 6. Child Count Changes

If a rerun changes the number of generated child actors, how should that be
handled?

Please clarify:

- should removed children disappear
  yes

- should added children get new ids
  yes

- should retained children keep their previous ids when they still represent the
  same logical actor
  as much as we can, it would be nice.

Answer:
above.


## 7. Geometry-Only Changes

If only an actor's geometry changes but its hierarchy does not, should the
runtime:

- replace only the geometry payload
- replace the whole actor node
- replace the whole subtree

Answer:
  replace only the geometry payload. Generally the minimum changes to reflect the changes.


## 8. Parameter Propagation

If a parameter on an inner actor changes, should the rerun affect:

- only that actor
- that actor and descendants
- any dependent downstream graph work outside the actor subtree
- another rule

Answer:
  that actor, dependents and if there are outputs on the actor the ancestors affected by those changes. 


## 9. Caching Requirement

Does version one require caching of intermediate results to support partial
runs, or is selective reevaluation enough?

Possible choices:

- caching is required
- selective reevaluation is enough for version one
- caching is optional but should be architecturally possible

Answer:
almost sure we will need caching.



## 10. Seed Changes

If a seed changes, what is the required invalidation scope?

Possible interpretations:

- rerun only the instruction that owns that seed
- rerun the actor/function subtree governed by that seed
- rerun the whole program
- another rule

Answer:

again, you re-run the instruction and that creates a cascade of changes on other instructions.


## 11. Function Body Changes

If the implementation or graph body of a function changes, what should happen?

Possible choices:

- rerun only direct call sites
- rerun all actor subtrees produced from that function
- require a full rerun
- another rule

Answer:

same, re-run the whole function and whatever it dirties.


## 12. Input Geometry Changes

If source geometry changes at the top or at an inner stage, how should
invalidation flow?

Please clarify:

- can the runtime identify the affected subtree only
- or should geometry changes force broader reruns

Answer:

input changes invalidate the instructions, from there on, cascade.


## 13. Partial Run Determinism

What determinism requirement applies to partial reruns?

Possible interpretations:

- partial rerun must produce the same final scene as a full rerun from scratch
- partial rerun may differ internally but final visible geometry must match
- another rule

Answer:

partial rerun must produce the same final scene as a full rerun from scratch, but not sure.



## 14. Failure During Partial Rerun

If a partial rerun fails, what should happen to the existing scene?

Possible choices:

- keep the previous subtree unchanged
- replace it with the failed partial state
- mark the subtree as invalid
- another rule

Answer:

not sure, my intuition is the result would be the result after the failure.


## 15. User Intent

When a user edits something, should the system aim to:

- automatically compute the minimal rerun scope
- allow user-selected rerun scope
- support both

Answer:

both, initially user-induced.


## Follow-Up Questions

These questions came up during review and focus on the last important
uncertainties in the partial-run model.

## 16. Canonical Invalidation Rule

Please confirm whether this exact wording matches your intent:

- a change invalidates the directly affected instruction or actor subtree
- invalidation then cascades through all downstream dependent instructions
- if an actor exposes outputs that feed ancestors or other parent-side work,
  invalidation continues through those dependent paths too

If not, please rewrite the invalidation rule in your preferred wording.

Answer:

that i sthe correct wording.

## 17. Partial Run Determinism Requirement

Please choose one exact rule for version one:

- a partial rerun must produce the same final scene as a full rerun from
  scratch
- a partial rerun may differ internally, but final visible geometry must match
- another rule

Answer:
  a partial rerun must produce the same final scene as a full rerun from
  scratch


## 18. Failure During Partial Rerun

Please choose one exact rule for version one:

- keep the previous subtree unchanged unless the rerun completes successfully
- replace the old subtree with whatever state exists at failure time
- mark the affected subtree as invalid while preserving the old subtree
- another rule

Answer:
  replace the old subtree with whatever state exists at failure time, for now



## 19. Caching Requirement In Version One

Please choose one exact rule:

- caching is required in version one
- caching is not required yet, but the architecture must support it later
- caching is optional in version one

Answer:
  caching is required for partial runs. Whenever we have one, we need to have the other.

