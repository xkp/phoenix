# Audited Production Partition Boundary

This directory records the immutable source boundary for the initial partition
compatibility port. The audited production root is:

`C:\Users\emili\source\repos\threedee.io\threedee.solver`

Audited on 2026-08-12:

| File | Lines | SHA-256 |
| --- | ---: | --- |
| `backend/partition/partition_solver.h` | 1305 | `f642705894f2ff886f3ce9a4e94f9747411aa90d0c33c0c60e7344ed29d948ab` |
| `backend/partition/partition_solver.cpp` | 1677 | `01a72a1a6a6d7a7c2d0f2397cf01cbf5a5e9ca1eebfa16320b3b6b83db6be13d` |
| `backend/partition/partition_solver_constraints.h` | 85 | `09c15d4cecd68e70bc8fd091a73199cb80267341ffbb18cceb618a630d46090d` |
| `backend/partition/partition_solver_constraints.cpp` | 901 | `ef8b1830997917196ec29c5d3be2f493c31410cd15a39f39e0641890a5d905fe` |
| `backend/partition/partition_solver_filters.h` | 100 | `b9ea13ec61d3a204e10950dc25f1c22e6d20e3a9a611338feb6aa5350315118f` |
| `backend/partition/partition_tesselator.h` | 245 | `704f68763130dc2bb97f92569d65bb21408230fbc02df7f91bfa7d9dff459cfc` |
| `backend/partition/partition_tesselator.cpp` | 1703 | `0ef7cdd235cf070ebc8a36ef6af8ccf6552dc3ed413dc33562f38605fb03662f` |
| `backend/partition/partition_errors.h` | 43 | `88ff56fcfba27753a38f0b86e42f50f69792bde78e433faf2fd185a3107c1e5b` |
| `backend/partition/partition_errors.cpp` | 75 | `65dbe35d49cb182636a6631a7726b99ff8ea25dafd90bd59b9d1ebd3177f6fb6` |

These files are evidence and the behavioral baseline. Adapted code belongs
under `src/partition`; each divergence must be classified as mechanical
compatibility, Phoenix-boundary adaptation, or an approved algorithm change.
The command, persistence loaders, VM integration, legacy 2D runtime handler,
and mutable model ownership are intentionally outside the trusted boundary.
