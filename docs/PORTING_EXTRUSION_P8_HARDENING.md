# Extrusion Port: Phase P8 Hardening Record

Status: repository work complete; external evidence pending.

## Platform Evidence

| Platform | Configuration | Status | Evidence |
|---|---|---|---|
| Windows x64 / MSVC 19.37 | Debug, CGAL 6.2 | Verified | Complete build and all Phoenix test executables pass locally. |
| Windows x64 / MSVC 19.37 | Release, CGAL 6.2 | Verified | Separate `CMAKE_BUILD_TYPE=Release` tree built; all 22 test executables passed. |
| Linux x64 / GCC | Debug and Release | Pending external runner | Presets exist but cannot be validated from the current Windows host. |
| macOS Apple Silicon / AppleClang | Debug and Release | Pending external runner | Explicit `arm64` presets added in P8. |
| Linux x64 / Clang + TSan | Best effort, pending | CI job builds and runs the concurrent kernel fixture under ThreadSanitizer. |
| Intel macOS / AppleClang | Best effort, pending | Explicit `x86_64` presets exist; not a release gate. |

Do not mark a pending row verified based only on successful configuration or
inspection. Record compiler and dependency versions, commands, and test output
from the target platform.

## Kernel Isolation Audit

The adapted kernel has no mutable namespace/global state. Remaining
function-local statics are immutable constants or member functions. The legacy
debug counter, debug printing, and hard-coded debug paths were not ported.
Profiles, input, builders, arrangements, event queues, and meshes are
invocation-local.

A focused stress fixture launches 16 independent `run_kernel` invocations
concurrently with separate ID allocators and identical immutable input. Every
result must demote successfully. This is not a substitute for ThreadSanitizer;
a Linux TSan run remains required before final thread-safety signoff.

## Source And Attribution Audit

The byte-identical internal production snapshot under `src/legacy/extrusion`
has recorded hashes and provenance in `PORTING_EXTRUSION_AUDIT.md`. No external
license header was present in the copied internal files. Permission to
redistribute that internal source has not yet been recorded; external
distribution remains blocked on that decision.

CGAL, Boost, GMP, MPFR, and transitive dependencies are supplied through
vcpkg, not copied into Phoenix. A release attribution bundle and license-text
review still need to be produced from the resolved release dependency graph.
The resolved Windows installation contains per-port license texts under
`build/windows-default/vcpkg_installed/x64-windows/share/*/copyright`; release
packaging must collect those files rather than maintaining a handwritten list.

## Performance Baseline

Extrusion now accepts an optional payload-free stage metrics sink and records
aggregate preparation, kernel, demotion, and repair microseconds plus attempted
and successful item counts. Normal execution pays only clock reads and does not
retain geometry in diagnostics. Publication remains covered by the executor's
total instruction timing.

On the current Windows host, the separate Release tree passed all 22 test
executables in 2910.5 ms wall time. Twenty fresh-process runs of the complete
kernel fixture suite measured 10.884 ms minimum, 11.591 ms median, 12.274 ms
p95, and 12.416 ms maximum. This process-level baseline includes executable
startup and every direct fixture; it is a regression marker, not a production
solver comparison.

Canonical geometry now reports a capacity-based storage estimate covering its
object and vertex/halfedge/face arrays. Cache entries retain shared immutable
geometry references, so this estimate must be deduplicated by geometry pointer
when calculating cache footprint. P8 still needs captured stage distributions
and deduplicated cache/peak-process measurements from representative projects.
Production-solver comparison remains unavailable until its executable can run.

## Automated External Evidence

`.github/workflows/extrusion-platforms.yml` defines required GCC Debug/Release
and Apple Silicon AppleClang Debug/Release jobs, plus a best-effort Linux Clang
ThreadSanitizer job for the concurrent kernel fixture. All platform presets use
separate single-configuration build directories. CI definitions make the work
repeatable but do not count as successful platform evidence until their runs
are recorded in this document.

## Remaining Exit Work

- run the added CI workflow and record GCC, AppleClang, and TSan results
- provide representative production projects and a runnable production solver
- record approval or restrictions for redistributing the internal source
- generate the release license bundle from resolved vcpkg copyright files
