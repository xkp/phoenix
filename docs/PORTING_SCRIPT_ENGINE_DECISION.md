# Script Engine Decision

## Decision

Phoenix selects **QuickJS-NG 0.16.1** as the version-one embedded JavaScript
engine. The version is pinned by the repository's vcpkg baseline. V8 remains a
production compatibility oracle, not a runtime dependency.

## Why QuickJS-NG

- It exposes runtime memory limits, maximum stack size, and a regular interrupt
  callback, which map directly to Phoenix invocation budgets and cancellation.
- Its native C API supports explicit host functions and classes without
  exposing CGAL pointers or requiring generated bindings.
- The core runtime does not provide filesystem, process, network, or clock APIs;
  Phoenix will not link or expose `quickjs-libc`.
- It uses CMake and supports the required Windows, Linux, macOS, x64, and ARM64
  targets. This is materially simpler than adding V8's GN toolchain and large
  isolate/snapshot distribution.
- It is actively maintained and implements contemporary ECMAScript while
  retaining a small invocation-local runtime model.

## Rejected Alternatives

- **V8:** strongest production lineage and highest peak performance, but a
  substantially larger binary/toolchain/patch surface. Its GN build, snapshot,
  ICU, platform, and isolate lifecycle would dominate this port. Existing V8
  bindings remain useful as behavioral fixtures, but their native pointer
  ownership model is deliberately not recovered.
- **Duktape:** compact and easy to embed, but its published stable line and
  ECMAScript surface are older, and deterministic interruption requires more
  build-time/internal configuration than QuickJS-NG's public callback.
- **JavaScriptCore/SpiderMonkey:** capable engines, but neither offers a better
  combination of portable packaging, hard resource controls, and embedding
  simplicity for this application.

## Integration Rules

1. Create one `JSRuntime` and one `JSContext` per invocation; never share script
   objects or mutable globals between runs.
2. Apply memory and stack limits before context creation. Install the interrupt
   handler before evaluating includes or user source.
3. Expose only the Phoenix `td` surface and approved deterministic math/3D
   values. Do not link `quickjs-libc` or expose module loading, filesystem,
   process, environment, network, dynamic library, or wall-clock APIs.
4. Compile and execute resolved immutable includes in stored order in the same
   context, followed by the wrapped main body.
5. Store source and engine identity in cache keys. Never persist QuickJS
   bytecode across engine versions or architectures.
6. Convert all engine exceptions, out-of-memory failures, interrupts, invalid
   outputs, and host errors into stable Phoenix diagnostics and roll back the
   invocation transaction.

## Acceptance Gate

The common expression corpus, host isolation, instruction interruption,
pre-cancellation, deterministic scalar bindings, memory exhaustion, ordered
library execution, immutable `td` metadata, and named scalar host finalization
now pass on Windows. Geometry creation, cloning, core mutation, stable-label
publication, rollback, and stale-handle rejection also pass through the real
transactional host. Mutable mesh readback and bounded result-only logging also
pass. The complete CGAL mutation surface currently implemented in the edit
session is also exposed through opaque JavaScript handles. Final acceptance
still requires the remaining production mesh-inspection/3D API and
Windows/Linux/macOS CI.
The first 3D engine bridge additionally passes exact line/plane/segment
intersection fixtures while keeping exact CGAL values inside the worker call.
The complete recorded 3D primitive and transform API is now projected as
frozen lightweight values; exact predicates and constructions remain scoped to
their C++ worker calls.
