# Production Solver Porting Questions

Use this document to resolve the architectural and compatibility questions that
must be answered before production solver functionality is moved into Phoenix.

This is a decision gate, not the porting plan itself. Fill in each `Answer:`
section and change its status when the answer is ready for review. After an
answer is reviewed, record the resulting decision in the requirements or
execution-model document named by the question.

Status values:

- `open`: unanswered or still under investigation
- `proposed`: answered but not yet reviewed
- `accepted`: reviewed and sufficient to guide implementation
- `deferred`: intentionally not required for the first porting slice

## Exit Criteria

Porting may begin when:

- every question marked **Blocking** is `accepted`
- the first production-kernel slice and its exact source boundary are identified
- label ownership and label conflict behavior are specified
- runtime geometry and exact-kernel conversion contracts are specified
- geometry lifetime across conversion boundaries is specified
- production compatibility expectations are explicit
- representative golden fixtures can be run through the production solver
- build, dependency, and source-ownership constraints are known
- accepted decisions are reflected in `KNOWN_REQUIREMENTS.md`,
  `EXECUTION_MODEL.md`, or the eventual persistent porting plan

## Decisions Already Established

These decisions came from the initial production-code review. They remain here
so later answers do not accidentally contradict them.

1. Mature geometry kernels, especially extrusion, partition, and inset, should
   remain substantially intact. Their legacy command wrappers do not need to be
   preserved.
2. Labels are fundamental geometry semantics, not optional display metadata.
3. Geometry stores compact integer label identifiers during a run.
4. A label identifier must remain stable for the duration of its run.
5. One label identifier must not resolve to mutable or function-dependent
   variants of a label definition.
6. Phoenix should move geometry through the runtime in a less-precise, compact
   representation and promote it to an exact representation only at kernel
   boundaries that require exact computation.
7. Faces and directed edges may carry labels; conversion must not reduce labels
   to one value per undirected edge or one value per geometry payload.
8. Exact CGAL handles must not escape an exact working-geometry lifetime unless
   their backing storage is explicitly retained.
9. Instructions that replace input faces with newly generated geometry consume
   those faces for final-scene assembly: the original faces are removed so they
   do not overlap their replacements.
10. Face consumption is instruction-specific. Non-mutating/view instructions
    such as `select` return references to original faces without consuming them.

## 1. Porting Goal And Compatibility

### 1.1 Compatibility Target — **Blocking**

Which production behaviors must Phoenix reproduce?

Consider separately:

- geometric shape and topology

I need more information on this. But generally speaking we will use normal topology for faces, edges, etc.
If you mean actual CGAL types, it is not necessary to keep the same types.

- face and directed-edge labels

yes, very important.

- stable element identifiers

Which eleemts do you refer to?

- random results for an existing seed

A single seed must produce the same results, randomness must be only seed based.

- actor/scene hierarchy

THe current implementation is very poor on this respect, you do not have to respect it.

- persisted project loading

persistence must be its own separate topic but yes, we must be compatible with existing projects as we have a large library of code and projects that we want to preserve/reuse.

- cache-file compatibility

no need for this compatibility, lets keep it on phoenix.

- exported output

generally speaking yes, our output must still be standard exchange files (fbx, glb, etc)

- error behavior

no need to keep.

Status: open

Answer:

above, per item.


### 1.2 Existing Project Compatibility — **Blocking**

Must existing production projects load directly in Phoenix, or will they pass
through a migration/conversion step?

If direct loading is required, which JSON and binary versions must be supported?

Status: open

Answer:

Technically, we could migrate as long as the original outputs are achieved.


### 1.3 Observable Differences — **Blocking**

What differences from production are acceptable in the first port?

Examples include floating-point coordinate drift, element ordering, triangulation
choices, generated IDs, diagnostic text, and output ordering.

Status: open

Answer:

Again, as long as te outputs are consistent.


### 1.4 Production As Oracle — **Blocking**

May the current production solver be treated as the behavioral oracle even when
its behavior appears accidental or defective? Who decides when Phoenix should
preserve a quirk versus intentionally correct it?

Status: open

Answer:

Case by case. Particvularly if you need to touch the kernel I would have to approve.
VM changes are fair game.


### 1.5 First Vertical Slice — **Blocking**

Which end-to-end slice should be ported first?

Candidate: load or construct labeled polygon geometry, run extrusion, convert
the result back to runtime geometry, and compare it with production output.

Status: open

Answer:

We can start with small examples and go progressively from there, so for the fist slice I'd be happy with a simple example (maybe even a single important instruction) and we take it all the way to output.


## 2. Kernel Preservation Boundaries

### 2.1 Trusted Kernel Source Boundary — **Blocking**

For extrusion, partition, and inset, which files and support types constitute
the trusted implementation that should remain intact?

Include dependent profiles, constraints, predicates, collision structures,
topology cleanup, geometry builders, and utility operations.

Status: open

Answer:

I'd go case by case but agin, simple equvalent type renaming for instance shouold be fine.
As long as they dont alter the algorithm.

### 2.2 Permitted Kernel Changes — **Blocking**

Which changes are allowed inside a trusted kernel?

Possible rules:

- namespace and include-path changes only

yes

- build and compiler compatibility changes

yes

- ownership modernization with identical behavior

yes

- targeted bug fixes backed by production/Phoenix fixtures

If we are to fix kernel issues we should wait until the end to preserve current behavior until we're sure we can still do all we did before. Only then we fix.

- algorithmic changes only with explicit approval

yes, that is very delicate.


Status: open

Answer:

above.


### 2.3 Kernel API Shape — **Blocking**

Should production kernel APIs be retained behind adapters, or may their public
parameter and result structures be reshaped while their internal algorithms
remain intact?

Status: open

Answer:

Internal algorithms must remain intact, but I'll give you leeway. 


### 2.4 Shared Support Code

When multiple kernels depend on the same production helpers, should those
helpers be ported once as a common internal library or kept private to each
kernel initially?

Status: open

Answer:

Do you have examples? I rather move this to shared internal libraries but I need to see cases.



### 2.5 Legacy Bugs And Undefined Behavior — **Blocking**

What is the policy when preserved code contains undefined behavior, unsafe
ownership, data races, or compiler-dependent behavior?

Must the defect be fixed during the port, or may the first slice reproduce the
behavior behind a compatibility test?

Status: open

Answer:

Bug fixing at the end, now reproduce behavior, that we know works suffiently well.
I would document issues for a post-port iteration.


## 3. Label Identity And Ownership

### 3.1 Canonical Label Owner — **Blocking**

Does one top-level run own exactly one canonical label registry shared by every
function invocation, actor, worker, partial rerun, and geometry value?

If ownership belongs elsewhere, specify its lifetime and sharing boundary.

Status: open

Answer:

Ideally we would share a single registry, however I;d prefer to lazy load in phoenix.
That brings its own set of issue to be discussed individually.


### 3.2 Persistent Identity Versus Run ID — **Blocking**

Confirm the relationship between:

- the persisted string label UID
- the compact integer used on geometry during a run
- any project-local or function-local label name/alias

Can two persisted UIDs intentionally resolve to the same run-local integer?

Status: open

Answer:

No, one UID -> one int. We have to eliminate bad cases in the migration process.


### 3.3 Immutability — **Blocking**

After a label is registered for a run, which fields, if any, may change?

Consider name, color, arbitrary data, profile association, material/style
association, and provenance.

Status: open

Answer:

Labels are immutable through a run, this is canon.


### 3.4 Duplicate UID Conflict — **Blocking**

When two functions or projects provide the same persistent label UID:

- which fields are compared for semantic equality?

All.

- are identical definitions deduplicated?

identical label definition are the same label.

- is a differing definition a hard load error?

its a hard migration error, yes. We shouold migrate to a format that ideally does nt allow drifting labels.

- is any explicit override or import-remapping mechanism permitted?

yes, case by case.

Silent first-wins and last-wins behavior should be addressed explicitly.

yes, we must migrate to a format that does not allow label drifting.

Status: open

Answer:

above.


### 3.5 Function-Local Labels — **Blocking**

Can a function define a label that is private to that function, or are all
labels run-global once loaded?

If private labels exist, what happens when their geometry crosses the function
boundary?

Status: open

Answer:

Yes, internal function labels are allowed and extremely common.
If they cross boundaries those labels will no longer be usable. However we do have renaming instructions that may fixed that on a wholesale basis: "rename unknow labels to this known one"


### 3.6 Imported Project Namespace — **Blocking**

Can separately authored/imported projects legitimately reuse the same label UID
with different definitions?

If so, define the namespace or deterministic remapping rule used when they are
combined in one run.

Status: open

Answer:

No, there is no legitimate use for same uid, different labels same UID is an error.
So we should try to make it a migration error.


### 3.7 Label Allocation Order — **Blocking**

Must integer label IDs be stable only inside one run, or reproducible across
independent runs of the same project?

They should be stable on one run. No need to persist over runs, although the caching of label -> id is a possiblity for a production final model.

Should all statically known labels be registered before execution? How are
runtime-created labels allocated without making IDs depend on worker scheduling?

Obviously knowing all labels a priori solves most issues, but it does invalidate lazy loading.
Must investigate more.

Status: open

Answer:

Above.


### 3.8 Label Editing And Partial Reruns — **Blocking**

When a user changes a label definition during a live session:

- does it create a new immutable registry generation?
- does it require a new persistent UID?
- which instructions, geometry, actors, and caches are invalidated?
- can existing integer IDs be retained?

Status: open

Answer:

Define a live session, is it a run? Like I said, a run makes all artifacts immutable.
However if a label changes it does invalidate the cache. Not sure if the whole cache, tbd.


### 3.9 Reserved Label Values — **Blocking**

List the required negative/sentinel label values and their exact meanings,
including unassigned, unbounded, layout, and any other production conventions.

Are all non-negative values reserved for registry allocation?

Status: open

Answer:

We have several canonical labels (the negatives ones are mainly gates) but we have:

- compass labels: north, south west and east.
- invisible (the faces with this labels will nt be exported in the output)


### 3.10 Label Provenance

What provenance must be retained for diagnostics?

Possible fields include project ID, function ID, persisted source, import path,
and every origin that supplied an identical duplicate definition.

Status: open

Answer:

Please rephrase.


### 3.11 Label Serialization And Cache Remapping — **Blocking**

May cached or serialized geometry store raw run-local label integers?

If geometry can be loaded under a different registry, specify whether the cache
stores label UIDs, a registry fingerprint, or a remapping table.

Status: open

Answer:

Yes, you may store label ids in the cache. If the label changes the cache ought to invalidate either partially or fully.


## 4. Runtime Geometry Representation

### 4.1 Canonical Runtime Format — **Blocking**

What concrete representation carries geometry between Phoenix instructions?

Decide whether 2D and 3D geometry use separate payloads and how vertices,
directed edges/halfedges, faces, holes, shells, and disconnected components are
represented.

Status: open

Answer:

First, there is no longer 2d geometry. That complicated everything. 2d faces will be represented as 3d faces lying the y = 0 plane. The concrete representation that moves geometry is up for debate/improvement. 


### 4.2 Runtime Coordinate Type — **Blocking**

Should runtime coordinates use `double`, `float`, configurable precision, or a
different compact number type?

Must persisted and cached geometry use the same coordinate type?

Status: open

Answer:

This is a good question, most game engines are fine with float precision and that would be ideal if its upgrade to better presicion still works.


### 4.3 Topology Requirements — **Blocking**

Which topology must survive between instructions?

In particular, must runtime geometry preserve:

- directed halfedge relationships

yes

- distinct labels on opposite halfedges

yes

- face holes and unbounded faces

we do not support holes, would love to, but post port.

- stable vertex, edge, and face IDs

again, good question, unique stable geometric are very useful.

- orientation and component boundaries

yes

- non-manifold structures

yes

Status: open

Answer:

Above.

### 4.4 Element References — **Blocking**

How do instructions represent a selected face, edge, or vertex after raw CGAL
handles are prohibited from escaping exact working geometry?

Specify element-ID stability across copying, conversion, accumulation, caching,
and partial reruns.

Status: open

Answer:

We can have our own representation as long as its efficient to convert from/to the needed cgal structures.
Ids must survice that conversion.

### 4.5 Geometry Ownership — **Blocking**

Define the difference between:

- memory/backing-store ownership
- actor accumulation ownership
- prototype/shared-instance ownership
- subgeometry or selection references

Can one runtime payload safely contain views into multiple backing stores?

Status: open

Answer:

Why would we have multiple backing stores, please be a little more precise.


### 4.6 Mutation Policy — **Blocking**

Are runtime geometry payloads immutable values, copy-on-write objects, or
mutable objects with exclusive ownership?

How is accidental mutation prevented when actors, caches, and graph values share
the same geometry?

Status: open

Answer:

Once a geometry is created by an intruction it becomes immutable, some faces could be removed from the final geometry as they are reused but that change does not happen in-site. At least now.


### 4.7 Canonical Geometry Identity — **Blocking**

What data participates in a geometry fingerprint?

At minimum consider coordinates, topology, orientation, element order, element
IDs, face labels, directed-edge labels, and actor ownership. Define any
canonicalization or floating-point normalization required before hashing.

Status: open

Answer:

All of the above, the floating point normalization would be up to you.


### 4.8 Stable Face Identity For Consumption — **Blocking**

What runtime identity names an original face when an instruction reports that
the face was consumed and must be removed from final geometry?

The identity must remain meaningful after selection, function calls, caching,
exact-kernel promotion/demotion, accumulation, and partial reruns. State whether
production face IDs are sufficient or a Phoenix geometry-payload/element key is
required.

Status: open

Answer:

The prod impl assigns an unique id to all geoemtry artifacts and makes sure they are not duplicated.
That face ID should be enough although this is not cannon. 


## 5. Exact-Kernel Conversion

### 5.1 Exact Kernel Types — **Blocking**

Which exact CGAL kernels and geometry containers must be retained for extrusion,
partition, inset, overlay, and related helpers?

Identify any kernel-specific differences rather than assuming one exact type can
serve every algorithm.

Status: open

Answer:

The CGAL kernel should be instruction specific, for instnce I spent a significant amount of time converting extrude to a doble based kernel with only small exact instructions. Part of the post-port work would be to reduce presicion where allowed. For now, unless spacified kernel instruction will remain exact. Except of cource for inset.


### 5.2 Promotion Contract — **Blocking**

When runtime floating-point coordinates are promoted, is the exact geometry an
exact representation of the stored binary floating-point values, or is an
intentional rationalization/snapping policy applied first?

How are invalid or degenerate runtime inputs reported?

Status: open

Answer:

Right now there is no snapping, degenerate detection is something we do sometimes but not system-wide.
It is an open topic.

### 5.3 Demotion Contract — **Blocking**

Define the policy for converting exact kernel output back to runtime geometry:

- rounding/conversion method
- overflow and non-finite handling
- coincident vertices after rounding
- zero-length edges and zero-area faces
- orientation changes
- topology reconstruction
- deterministic element ordering

Status: open

Answer:

Again, this is very ad-hoc at the moment but I would seriously consider system-wide geometry cleaning 


### 5.4 Label Preservation Through Conversion — **Blocking**

Specify how face, halfedge, opposite-halfedge, and sentinel labels map through
promotion, kernel execution, topology creation, and demotion.

What constitutes a label-preservation failure?

Status: open

Answer:

We can not lose labels as the system requires it for most everything.


### 5.5 Element-ID Preservation — **Blocking**

Which production vertex, edge, and face IDs must survive promotion and demotion?

When a kernel splits, merges, creates, or removes topology, who assigns new IDs
and what provenance is retained?

Status: open

Answer:

depends on the mechanism used to assign ids, the current incrementing int model makes it single-threaded.
that is still valid as long as we assign ids on the main thread for created elements.


### 5.6 Cleanup Responsibility — **Blocking**

Does cleanup occur inside each preserved production kernel, in the demotion
adapter, or as a separate explicit instruction?

Avoid introducing generic cleanup that changes tuned production results unless
that behavior is accepted and tested.

Status: open

Answer:

Yes, cleanup should not be kernel instruction responsibility. Even when now we do some ad-hoc cleanup.


### 5.7 Exact Working-Set Reuse

For the first port, should every exact instruction independently promote and
demote geometry, or may adjacent compatible kernels share an exact working set?

If deferred, state the measurements that would justify exact execution islands
or a lazy dual representation later.

Status: open

Answer:

I say we should institutionalize promotion/demotion. The kernels today expect a kernel and they should be provided the working set already promoted, then after the work is done we should do demotion/cleanup on our terms.


## 6. Instruction And Function Semantics

### 6.1 Legacy Command Responsibility — **Blocking**

For each first-slice kernel, which behavior belongs to the legacy command rather
than the internal kernel?

Include multiplexing, defaults, randomization, label resolution, selection,
error translation, output naming, configuration propagation, and scene
notification.

Status: open

Answer:

The new commands we create (or similar concept) should carry the responsibilit of handing off the working set as the kernel instructions expect it. Most kernel instructions now are separate algorithms that expect a certain shape.

### 6.2 Parameter Compatibility — **Blocking**

Must Phoenix expose every production option for the selected kernel immediately?

If not, identify the minimum option set and how unsupported persisted options
are diagnosed without silently changing behavior.

Status: open

Answer:

What options do you refer to?


### 6.3 Multiplex Unit — **Blocking**

For each major kernel, does production execute once per geometry payload, face,
component, selected item, or whole special `input` collection?

Which of these semantics must Phoenix preserve?

Status: open

Answer:

For multiplexing, each kernel algorithm is ran on a single face.

### 6.4 Failure Granularity — **Blocking**

When one multiplexed geometry item fails inside a major kernel, should Phoenix
route only that item through `else`, fail the whole instruction, or follow
kernel-specific behavior?

Status: open

Answer:

Failures should go through else.


### 6.5 Random Compatibility

Do any selected kernels or their parameter adapters consume randomness? If so,
must they reproduce production RNG sequences and consumption order?

Status: open

Answer:

Yes, it is completely possible that some kernels use randonmess, we should provide them with random interfaces that phoenix builds and controls.


### 6.6 Consuming Versus Non-Consuming Instructions — **Blocking**

How is an instruction classified as consuming/replacing input faces or as a
non-consuming view/transformation?

Clarify whether this is:

- static instruction metadata
- an invocation result decided from actual outputs
- kernel-specific behavior
- configurable per instruction

At minimum, identify expected behavior for extrusion, partition, inset,
selection, rename, merge, and function calls.

Status: open

Answer:

instruction static metadata, there are no cases where the same instriciton types behaves differently.


### 6.7 Consumption Result Contract — **Blocking**

How does an instruction publish which exact input faces were replaced?

Candidate: an instruction result contains normal output values plus an explicit
set of consumed runtime face identities. This should be distinct from mutating
or destroying the input value, because other graph branches may still read it.

Status: open

Answer:

Consuming instructions replace all of its input. Unless failure occurred.


### 6.8 Consumption Timing — **Blocking**

At what point do consumed faces disappear from actor/final geometry?

Consider instruction completion, canonical result publication, function
completion, actor assembly, and scene patch application. Define how the rule
avoids worker-completion-order effects.

Status: open

Answer:

Right now, we have a global accumulation mechanism, its not canon. I am open to flags, etc.


### 6.9 Fan-Out And Multiple Consumers — **Blocking**

If one original face flows into multiple branches, define the final-scene
behavior when:

- one branch selects it and another replaces it
- two branches both replace it with different outputs
- one branch passes it through unchanged
- a consuming branch fails or emits no replacement

Does consumption remove the original once while retaining every successfully
generated replacement?

Status: open

Answer:

Yes, it is possible multiple instructions try and replace the same face, thats ok since there is no un-replace mechanism
This is also common and we call it face duplication where the same output goes to different instructions.


### 6.10 Partial And Conditional Consumption — **Blocking**

Can one multiplexed invocation consume only the successfully processed subset
of its input faces? What happens to failed, filtered, skipped, or `else`-routed
faces?

Status: open

Answer:

else routed faces are not replaced. Only the ones consumed.


### 6.11 Function-Boundary Consumption — **Blocking**

When a nested function consumes geometry supplied by its caller, how is that
consumption propagated back to the owning actor and final-scene assembly?

Can a function explicitly return original faces without consuming them, and can
an actor-generating child function consume geometry owned by its parent?

Status: open

Answer:

Yes, you can return uncosumed faces and consume parent geoemtry.


### 6.12 Consumption Versus Actor Ownership — **Blocking**

Are consumed faces always removed from the geometry payload of their existing
actor owner, even when the replacing instruction executes in a different
function or actor context? Define whether cross-actor consumption is allowed.

Status: open

Answer:

Yes, replacement can happen on different function and te replacement will always occur on the actor ownig the gepoemetry.

## 7. Caching And Partial Reruns

### 7.1 Cache Compatibility

Must Phoenix read existing production cache files, or may it define a new cache
format and rebuild old entries?

Status: open

Answer:

You can, and probably should, define a new cache format. We do not need any compatibility at cache level.


### 7.2 Label Registry In Cache Identity — **Blocking**

Which label-registry identity, generation, or semantic fingerprint participates
in cache keys? Can a label presentation-only change avoid geometry invalidation?

Status: open

Answer:

Probably case by case. But generally even presentation changes invalidate the cache as now the output is different.
Cache is mostly useful on productions run where the graph is effectively immutable. If it simplifies things I'm good with cache invalidation on graph changes.


### 7.3 Precision Boundary In Cache Identity — **Blocking**

Are cached instruction outputs always canonical runtime geometry, or can exact
working geometry be cached? How are conversion-policy versions represented in
cache identity?

Status: open

Answer:

Whats the difference between working geometry and runtime geometry?

### 7.4 Kernel Version Identity — **Blocking**

How will a change to a preserved kernel or adapter invalidate cached results?

Specify whether identity derives from source/build version, an explicit kernel
schema version, or another deterministic mechanism.

Status: open

Answer:

Again, I'm fine with total cache invalidation on graph changes for now, the real use is at production runs where the graph is immutable. It would be nice to keep the cache as a development tool but not a must.


### 7.5 Registry Lifetime Across Partial Reruns — **Blocking**

Confirm that a partial rerun uses the live scene's label registry rather than
constructing a fresh registry. Define behavior when edited code introduces or
removes labels.

Status: open

Answer:

If the graph didnt mutate, you can absolutely reuse a cached registry.


### 7.6 Cached Consumption Records — **Blocking**

Must cached instruction results include the set of consumed face identities in
addition to generated output geometry? Define how those identities are
validated or remapped when cached geometry is reused.

Status: open

Answer:

Not sure, depends how those ids are generated, if you are incrementing ints for ids and you keep the last generated id then previous run ids should be sufficient.


### 7.7 Consumption Changes During Partial Reruns — **Blocking**

If a partial rerun consumes a different set of faces than the previous run,
how are previously removed originals restored and newly consumed originals
removed without rebuilding unaffected actor geometry?

Status: open

Answer:

I probably need more info here, but what the cache does is to simulate a run, as part of that simulation replacement happens, Once it gets dirtied, any created face can be replaced under the regular mechanism.


## 8. Loading, Linking, And Validation

### 8.1 Label Resolution Phase — **Blocking**

At what phase are persisted label UIDs converted to run-local `LabelId` values?

Candidate: register and validate labels while loading, then resolve all function
references during a deterministic link/compile phase before execution.

Status: open

Answer:

Right now we build the registry per run, if you were to cache the registry then there would not be a need to re-create it unless the graph changes. 


### 8.2 Whole-Program Knowledge — **Blocking**

Can every function and label be known before execution begins, or can functions,
projects, scripts, and labels be loaded lazily during a run?

If lazy loading is required, define its deterministic registration and conflict
rules.

Status: open

Answer:

Lazy run is not required but very much desired. I'll be ok with this work moving to after port.


### 8.3 Validation Severity — **Blocking**

Which conditions prevent a run from starting?

Include conflicting label definitions, unresolved labels, unsupported kernel
options, invalid topology, missing profiles, incompatible geometry dimensions,
and unavailable external assets.

Status: open

Answer:

All of those are valid. But we have decided so far to be very tolerant and just notify error and run as much as we can.

### 8.4 Function Import Semantics — **Blocking**

When a function is local, public, binary, or imported from another project,
which label table, profiles, styles, variables, and support assets should it
see?

Status: open

Answer:

If we buld the label table at the beginning of the run each function sees the same table.


## 9. Concurrency And Determinism

### 9.1 Registry Thread Safety — **Blocking**

May labels be registered after worker execution begins? If yes, define how
allocation and conflict publication remain deterministic. If no, specify the
pre-execution freeze point.

Status: open

Answer:

No, the label set is immutable, there is n olabel creation mechanisms.


### 9.2 Kernel Thread Safety — **Blocking**

Are extrusion, partition, and inset safe to execute concurrently on isolated
inputs? Identify global state, static caches, ID generators, debug facilities,
or CGAL services that require isolation or serialization.

Status: open

Answer:

Kernels ought to be safe to run in isolation always. The execution context must be immutable. 


### 9.3 ID Generation — **Blocking**

Production kernels assign geometry element IDs in several places. Must those
sequences be reproduced, and can allocation occur concurrently without making
results schedule-dependent?

Status: open

Answer:

Again, depending on the mechanism, but the safest would be to generate/assign ids on the main thread.
Under this, it'd be the inst responsibility to return invalid ids where id generation is needed.


## 10. Golden Fixtures And Acceptance Tests

### 10.1 Fixture Sources — **Blocking**

Which real production projects may be used as regression fixtures? Identify
small, medium, pathological, and performance-sensitive examples for extrusion,
partition, and inset.

Status: open

Answer:

We have LOTS, as we approach runnability I expect we will start adding examples.
When we approach completion there will be hundereds of production programs/functions.


### 10.2 Comparison Contract — **Blocking**

How will production and Phoenix results be compared?

Define exact versus tolerant comparison for:

- coordinates
- topology and orientation
- face and directed-edge labels
- element IDs
- component and output ordering
- errors and partial results

Status: open

Answer:

I'd compare geometry and materials, which are the only two resulting artifacts we produce.


### 10.3 Label-Specific Regression Cases — **Blocking**

Provide fixtures for:

- identical label UID and identical definition in multiple functions

this is te same label.

- identical UID with conflicting definitions

This is an migration error condition.

- labeled geometry crossing nested functions

What is the issue here?

- distinct opposite-halfedge labels

This the normal way of things. 

- label changes during a partial rerun

I'd go full cache invalidation unless there is a very simple solution that I do not see.

- cached geometry loaded under a compatible or incompatible registry

Cacched invalidation.

- imported projects with overlapping label sets

Migration error, runtime error if it gets there.

Status: open

Answer:

Above.

### 10.4 Conversion Regression Cases — **Blocking**

Provide fixtures that expose precision and topology risks:

- nearly coincident vertices and edges

Should me merged within a threshold. This is important because it causes all sorts of geometry issues.

- very small and very large coordinates

Most like errors.

- holes and disconnected components

Right now, an error.

- degeneracies created by demotion

Must be cleaned.

- repeated exact-kernel chains

I'm amenable to chain detection, not sure if I'd implement it pre-port.

- labels on topology created or removed by a kernel

I do not see the issue here.


Status: open

Answer:

Above.


### 10.5 Performance Baseline — **Blocking**

Which production workloads and measurements define success for moving runtime
geometry away from exact storage?

Record expected memory use, conversion cost, kernel time, total run time, cache
size, and partial-rerun latency where available.

Status: open

Answer:

I'd use the current production code as baseline.


### 10.6 Face-Replacement Regression Cases — **Blocking**

Provide fixtures covering:

- extrusion/partition/inset replacing original faces without overlap
- `select` returning original faces without consuming them
- one selected face feeding both consuming and non-consuming branches
- two consuming branches sharing one original face
- partial multiplex success and `else` routing
- nested-function consumption
- cached reuse of a consuming result
- a partial rerun changing the consumed-face set

Status: open

Answer:

More info needed, what is a "fixture" in this case?


## 11. Build, Dependencies, And Source Stewardship

### 11.1 Source Ownership And Licensing — **Blocking**

Confirm that the production solver code and all bundled third-party components
may be moved into this repository. Identify files with separate licenses,
attribution requirements, or unclear provenance.

Status: open

Answer:

I confirm. Although it would not hurt for you to ask.

### 11.2 Supported Toolchains — **Blocking**

Which operating systems, compilers, architectures, CGAL versions, Boost
versions, and build configurations must the port support?

Status: open

Answer:

We must compile in windows and lines (including Apple OSes)


### 11.3 Exact Dependency Versions — **Blocking**

Do tuned kernels depend on the behavior of the production CGAL/Boost versions?
May Phoenix upgrade them during the port, or should it first reproduce the
production dependency set?

Status: open

Answer:

Yes, please upgrade and we shall see. Case by case.


### 11.4 Source History

Should kernel files be moved with their Git history, copied as a clearly marked
legacy subtree, or introduced as newly adapted Phoenix sources? Who will own
future kernel maintenance?

Status: open

Answer:

Just introduce them as new code.


## Review 1 Clarifications And Proposed Resolutions

This section records the first review of the answers above. It intentionally
keeps unresolved questions and suggested defaults together so later decisions
do not depend on conversation history.

Unless marked otherwise, every resolution below has status `proposed`. A
proposal becomes `accepted` only after the remaining question is answered or
the suggested default is explicitly approved.

### R1. Output Compatibility Contract — **Blocking**

Proposed resolution:

- production and Phoenix results have the same connected components and
  topology
- orientation is preserved
- face and directed-halfedge labels are identical
- material assignments are identical when materials are in scope
- coordinates compare within an explicitly versioned tolerance
- element and output ordering need not match unless ordering is itself observed
  by later execution
- Phoenix element IDs need not equal production IDs, but Phoenix IDs are stable
  and deterministic according to the accepted Phoenix identity rules
- diagnostic text and legacy error wording need not match

Clarification required:

Must a production project using seed `N` generate the same geometry in Phoenix,
or is it sufficient that Phoenix produces a deterministic result for seed `N`
that may differ from production?

Status: proposed

Answer:

That's tricky, for that you would have to produce exactly the same random numberas as prod, instead, we can make sure we test with non-random operationms.

Accepted clarification:

- Phoenix randomness must be deterministic from its Phoenix seed.
- The initial port does not need to reproduce the production solver's exact
  random sequence for the same numeric seed.
- Initial production-comparison fixtures will avoid randomized operations.
- Legacy RNG compatibility may be investigated after the initial port.


### R2. Material Scope — **Blocking**

`KNOWN_REQUIREMENTS.md` currently defers materials, while the answers above name
geometry and materials as the two output artifacts to compare.

Suggested default:

The first kernel slice compares geometry, topology, and labels. It preserves
any label/material references needed for later compatibility, but full material
assembly and export remain deferred until after the first geometry-kernel port.

Clarification required:

Must the initial port preserve material identity and assignments, or may the
first slice validate geometry and labels only?

Status: proposed

Answer:

We can defer materials and concentrate on geometry.


### R3. First Vertical Slice — **Blocking**

Suggested default:

Use extrusion as the first end-to-end slice because it exercises input-face
consumption, generated topology, face and halfedge labels, profiles,
promotion/demotion, element-ID assignment, and final geometry publication.

Clarification required:

Confirm extrusion as the first kernel, or identify a smaller instruction that
should precede it.

Status: proposed

Answer:

Extrusion would work.


### R4. Function-Local Label Visibility — **Blocking**

Proposed resolution:

- every valid label definition is immutable and run-global once registered
- geometry always carries a `LabelId` that remains valid across function
  boundaries
- a function owns a local symbol/visibility table mapping its persisted label
  references to canonical run-global `LabelId` values
- crossing a function boundary does not invalidate a label definition or its
  integer ID
- a label may be unknown to the receiving function because it is absent from
  that function's symbol table
- `rename unknown labels` operates on labels not visible in the receiving
  function rather than repairing invalid integer IDs

Clarification required:

Confirm that "no longer usable" means symbolically unknown to the receiving
function, not that the carried `LabelId` loses its registry definition.

Status: proposed

Answer:

Confirmed.


### R5. Eager Label Linking For The First Port — **Blocking**

Proposed resolution:

- load every function and label reachable by the run
- build one deterministic run-owned label registry
- validate duplicate UIDs and resolve function-local label symbols
- freeze the registry before instruction workers begin
- do not create labels during execution
- defer lazy function/label linking until after the initial port

This resolves the tension between desired lazy loading, immutable label IDs,
and deterministic parallel execution.

Status: proposed

Answer:

Agreed.


### R6. Reserved And Canonical Labels — **Blocking**

Proposed resolution:

- preserve the semantics of the production negative sentinels initially
- represent them as named `LabelId` constants because preserved kernels expect
  integer labels
- reserve all negative values and allocate ordinary registry labels only from
  the non-negative range
- treat compass and invisible labels as canonical persisted labels rather than
  negative sentinels unless production inspection proves otherwise

Known production values requiring confirmation:

- `-1`: absent or unassigned label
- `-1000`: unbounded
- `-1001`: layout

Clarification required:

Provide or approve investigation of the complete sentinel list and confirm
whether the exact numeric values are compatibility requirements.

Status: proposed

Answer:

Lets investigate.


### R7. Initial Runtime Geometry Precision And Axes — **Blocking**

Proposed resolution:

- Phoenix runtime geometry is always represented as 3D geometry
- former 2D topology is represented in one canonical plane
- begin with `double` runtime coordinates
- evaluate `float` only after the port is behaviorally correct and measured
- continue using instruction-specific exact or inexact CGAL working geometry

Using `double` initially separates the kernel port from the more aggressive
precision reduction and makes compatibility failures easier to diagnose.

Clarifications required:

- Confirm that the canonical former-2D plane is `y = 0` and coordinates map to
  `(x, 0, z)`, rather than mapping to `(x, y, 0)` on `z = 0`.
- Confirm `double` as the initial runtime coordinate type.

Status: proposed

Answer:

Confirmed, (x, 0, z) is the correct value for 2d coordinates.

Accepted clarification:

- `double` is the initial Phoenix runtime coordinate type.
- Reducing runtime storage to `float` is deferred until after behavioral
  compatibility is established and performance is measured.


### R8. Runtime Geometry Versus Working Geometry — **Blocking**

Terminology:

- **runtime geometry** is the canonical immutable, non-exact representation
  passed across graph edges, stored by actors, serialized, and written to
  ordinary caches
- **working geometry** is a temporary CGAL structure prepared for one kernel
  invocation; its exactness and container type are kernel-specific

Proposed resolution:

- cache canonical runtime geometry only
- do not cache temporary exact working geometry in the first port
- include runtime-geometry schema and conversion-policy versions in cache
  identity
- promote runtime geometry before a kernel and demote the result afterward
- consider adjacent exact-kernel chaining only after the initial port

Status: proposed

Answer:

Agree, working geometry only exists inside of workers and should not cross that boundary.


### R9. System-Wide Conversion Validation And Repair — **Blocking**

Proposed resolution:

Separate two stages:

1. Mandatory conversion validation rejects non-finite coordinates, broken
   element references, invalid orientation representation, and topology that
   cannot be represented safely.
2. Versioned geometric repair may merge nearby vertices, remove zero-length
   edges, remove zero-area faces, and address degeneracies introduced by
   demotion.

Repair must not be hidden inside trusted kernels. Its policy and tolerances are
part of adapter/runtime compatibility and must be included in cache identity.

Clarifications required:

- Does near-vertex merging always occur, or only after exact-to-runtime
  demotion?

Should occur after geometry producing instructions.

- Is its threshold absolute, relative to geometry scale, or a combination?

I'd go absolute until proven wrong.

- If repair removes labeled topology, is the invocation an error or a
  successful repair with diagnostics?

Degenerate edges and faces can have labels and can be removed without error.

Status: proposed

Answer:

Above.


### R10. Geometry Element Identity — **Blocking**

Proposed resolution:

- unchanged input elements retain their IDs through conversions when the
  kernel preserves them
- workers return invalid/unassigned IDs for newly created elements
- new IDs are assigned during deterministic canonical publication on the main
  thread
- removed elements disappear
- split and merged elements receive new IDs and may record source-ID provenance
- persistent runtime references use a compound geometry-payload identity plus
  element ID unless global run-wide uniqueness is explicitly guaranteed

Clarification required:

Is a bare face/edge/vertex ID guaranteed to be unique across every geometry
payload in one run, or may separate payloads contain the same element ID?

Status: proposed

Answer:

Id's should be unique over a run.


### R11. Face Consumption And Duplication — **Blocking**

Proposed resolution:

- consuming behavior is static instruction-type metadata
- multiplexed consumption is decided per input item
- a successfully processed face is consumed
- a failed, skipped, or `else`-routed face is not consumed
- multiple branches may consume the same original face
- the original face is removed once from its owning actor
- every successful branch's generated replacement geometry remains
- input values remain immutable; consumption is published as instruction-result
  metadata
- consumption propagates across function and actor execution boundaries to the
  actor owning the original geometry

Clarification required:

If a consuming instruction succeeds for an item but intentionally emits no
replacement geometry, is the original face still consumed? The current answer
"replace all input unless failure occurred" implies yes.

Status: proposed

Answer:

A consuming instruction should always produce replacement. But worse case that no replacement is produced the replacement still stands.


### R12. Cache And Partial-Rerun Publication Ledger — **Blocking**

Proposed resolution:

- cached instruction results store generated runtime geometry and consumed
  source-face identities
- cache identity includes input geometry identity, label-registry fingerprint,
  seed, graph identity, kernel/adapter version, conversion-policy version, and
  parameters
- graph, label, adapter, kernel, or conversion-policy changes may invalidate the
  entire cache initially
- parameter, input, and seed changes invalidate dependent entries through their
  normal cache identities
- actor assembly retains a publication ledger describing geometry contributions
  and face-consumption effects by rerun scope
- a partial rerun first removes the prior scope's contributions and consumption
  effects, then applies the new result
- this ledger allows an original face to reappear when a new rerun no longer
  consumes it

Clarification required:

Is broad cache invalidation on graph and label changes acceptable while
parameter-driven partial reruns remain supported?

Status: proposed

Answer:

It is acceptable for the port, then becomes a to-do if feasible.


### R13. Validation Severity — **Blocking**

Proposed resolution:

- **fatal before execution:** conflicting definitions for one label UID,
  unreadable required graph data, unsupported persisted format, or a
  structurally invalid graph that cannot be executed coherently
- **function/instruction failure:** missing profile, invalid geometry topology,
  incompatible geometry, unavailable optional asset, or kernel failure
- **warning/diagnostic:** recoverable cleanup, ignored optional data, and other
  conditions with explicitly defined continuation behavior

The runtime should execute independent valid work after instruction/function
failures, consistent with Phoenix failure and `else` semantics. A label UID
conflict is proposed as fatal because continuing would violate immutable label
identity.

Clarification required:

Confirm that conflicting definitions for one label UID prevent the run from
starting.

Status: proposed

Answer:

Let's treat it as such for correctness but we should guarantee it beforehand.


### R14. Golden Fixture Definition And Initial Set — **Blocking**

A fixture is a small persisted input plus expected observable results generated
by the production solver. Phoenix runs the same or migrated project and compares
its result using the accepted compatibility contract.

Example contents:

```text
fixture/
  project.json
  input.glb
  expected_geometry.glb
  expected_topology.json
  expected_labels.json
```

An initial face-replacement fixture can route one labeled quad to both `select`
and extrusion, then verify that selection observes the original during
execution, the original is absent from final actor geometry, the extrusion is
present, and all generated labels are correct.

Suggested initial fixture set:

- three extrusion fixtures
- one conflicting-label migration fixture
- two face-consumption/fan-out fixtures
- two precision/degeneracy fixtures

Hundreds of production programs can be added progressively as broader features
become runnable.

Status: proposed

Answer:

I'd hope you would produce those fixtures. Else they can be added bu hand.


### R15. Supported Platforms And Toolchains — **Blocking**

Assumption requiring confirmation:

- Windows
- Linux
- macOS
- initially 64-bit desktop targets

Clarifications required:

- Does macOS require both Apple Silicon and Intel support?
- Must Linux support GCC, Clang, or both?
- Which Windows compiler versions are required?

Status: proposed

Answer:

I'm not sure, I recommend we support a resonable set.

Accepted clarification:

- Windows x64 with a currently supported MSVC toolchain is required.
- Linux x64 with GCC is required; Clang is initially best effort.
- macOS on Apple Silicon with AppleClang is required.
- Intel macOS is initially best effort and not a release blocker.
- The initial port retains C++17 and vcpkg-managed CGAL dependencies.


## Review 2 Accepted Clarifications

The project owner accepted the remaining Review 1 recommendations on
2026-08-11. This acceptance supersedes the individual `proposed` status lines
in R1 through R15; those proposals are accepted as clarified by this section.

### Runtime Precision And Axes

- Canonical former-2D coordinates map to `(x, 0, z)` on the `y = 0` plane.
- Canonical runtime geometry initially stores coordinates as `double`.
- A later measured optimization may consider `float` runtime storage.

### Random Compatibility

- Phoenix guarantees deterministic seed-driven randomness.
- Reproducing the legacy production random sequence for the same numeric seed
  is not required for the initial port.
- Initial production-comparison fixtures avoid randomized operations.

### Non-Manifold Geometry

- The Phoenix runtime representation supports non-manifold topology.
- Each kernel declares the topology it accepts.
- A kernel that requires manifold input reports an item-scoped failure through
  normal `else` handling.
- Trusted kernels are not modified merely to add non-manifold support.

### Geometry Repair Tolerance

- Near-element merging uses an absolute tolerance initially.
- The tolerance is versioned and configurable.
- Its initial numeric default will be derived from production fixtures during
  the extrusion slice rather than selected arbitrarily in advance.
- Geometry-producing instructions run the accepted validation/repair pipeline
  after producing runtime geometry.

### Supported Platforms

- Windows x64 with a currently supported MSVC toolchain is required.
- Linux x64 with GCC is required; Clang is initially best effort.
- macOS on Apple Silicon with AppleClang is required.
- Intel macOS is initially best effort and not a release blocker.
- C++17 and vcpkg-managed CGAL form the initial build baseline.

### Label Sentinel Investigation

The first source audit found these global production conventions:

- `-1`: unassigned, absent, or default label
- `-1000`: unbounded geometry
- `-1001`: layout geometry

The `hard_edges` implementation also uses `-2` through `-7` as internal
positional role markers for left, bottom, right, top, skirt, and cap. They are
not treated as global label-registry sentinels unless later kernel tracing shows
that they cross the algorithm boundary.

Ordinary run-registry labels use non-negative IDs. Negative values remain
reserved and are represented through named constants at kernel boundaries.


## Review Record

Use this section to record review passes rather than relying on conversation
history.

### Review 1

Date: 2026-08-11

Participants: project owner and Codex

Accepted questions: R1 through R15, as clarified by Review 2

Questions requiring follow-up: none at the product-decision level; kernel
source-boundary and fixture details remain implementation investigations

Requirements or design documents updated: `PORTING_QUESTIONS.md`; the accepted
decisions still need to be promoted into the requirements, execution model, and
persistent porting plan
