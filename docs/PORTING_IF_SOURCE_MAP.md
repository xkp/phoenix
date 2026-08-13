# If Port Record

## Trusted Production Source

| Source | SHA-256 | Role |
|---|---|---|
| `commands/if_command.h` | `9FC87FFB70EC6EF5C3999A35C9DEFEA7B36B648F7A9A80195AAEE146DD290140` | Truthiness and `then`/`else` routing |
| `loaders/if_loader.h` | schema evidence | `variable` and `expression` modes and defaults |

## Implemented Boundary

Phoenix's non-scripted `if` routes one input value unchanged to exactly one of
`then` or `else`. Its condition is a typed runtime input: booleans route by
their value and integer/floating-point values route by nonzero truthiness.
Missing, empty, defaulted, string, geometry, and other unsupported conditions
fail without publishing either branch.

This replaces production's lookup of a named mutable VM variable with an
explicit graph input. P12 migration will connect the migrated variable source
to that input. The production expression mode remains gated on the versioned
scripting contract; this implementation does not embed V8 or interpret source
text. Ordered `case` also remains gated on that work.

The instruction is non-consuming and preserves the complete `RuntimeValue`,
including geometry ownership, when routing it.
