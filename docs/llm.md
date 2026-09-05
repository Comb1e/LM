# LM0 Generation Contract

Generate complete LM0 version 1 source, or exactly the function/block requested
by a replacement operation. Read `docs/instructions.md` for operand signatures.

1. Begin a complete file with `module NAME version 1`. Instructions end at
   newlines. Use ASCII identifiers, `%registers`, `@functions`, and `^blocks`.
2. Declare all parameter, return, block-parameter, and destination types.
   Registers are scalar or pointer values. Arrays and structs live in memory.
3. Define each register once per function. Reuse function parameters freely;
   pass every other cross-block value through block parameters. A block cannot
   directly reference another block's registers even if it dominates that block.
4. End each block with `jump ^target(...)`, `branch %condition, ^yes(...),
   ^no(...)`, `return %value`, `return`, or `trap`. The entry block has no block
   parameters and cannot be a branch target.
5. Use registers for instruction operands. Materialize literals using `const`.
   Arithmetic operands have identical types. Comparisons produce `bool`.
   Use `cast` explicitly; Booleans do not cast to numbers.
6. Integer arithmetic wraps. Division by zero, minimum/-1, and shifts outside
   the type width trap. Signed right shift preserves the sign bit.
7. `offset` uses an `i64` element index. `alloc` and `copy` counts are `u64`.
   `stack T, N` uses a positive literal count and appears only in entry.
8. Initialize memory before reading. Keep addresses inside live allocations.
   Free each heap base once and never free stack/static storage. `load`/`store`
   require exact pointer-element types. `field` addresses a named struct field.
   Use `move` for possibly overlapping byte ranges and `copy` for disjoint ones;
   both take a `u64` byte count and allow null pointers only when it is zero.
9. Use `call @name(%args)` with a destination for every non-void return.
   An executable needs `fn @main() -> i32`. Imported/exported C functions start
   with `extern c fn` / `export c fn`. There are no variadic or indirect calls.
   `lm0 build --kind shared` builds exported functions for a native host without
   requiring `main`. Match the host's ABI types and release LM0 allocations via
   an exported destructor. A native trap terminates the host process.
10. Build the native compiler with `make`, then run `build/lm0 check` before
    building. The compiler is GNU x86-64 assembly and emits assembly; Python is
    not part of compilation. For repairs, use diagnostic code, source span, and
    function/block/register identifiers. Use `build/lm0 inspect` for required
    context and `build/lm0 replace` for atomic, validated changes to parseable
    source. Use only `-O0`; higher levels and `--sanitize` are currently rejected.
11. Report suspected LM0 bugs, unclear diagnostics or documentation, and missing
    capabilities through the [issue-reporting workflow](reporting.md), including
    problems for which you found a workaround. Submit a minimal reproducer and
    observed output to the project's GitHub Issues tracker. Include the issue
    URL in your task summary; if submission is unavailable, preserve a local
    report and explicitly mark it as not submitted.

Canonical loop:

```text
module loop version 1
fn @count(%n:u64) -> u64 {
^entry:
    %zero:u64 = const 0
    jump ^loop(%zero)
^loop(%i:u64):
    %more:bool = lt %i, %n
    branch %more, ^step(%i), ^done(%i)
^step(%j:u64):
    %one:u64 = const 1
    %next:u64 = add %j, %one
    jump ^loop(%next)
^done(%result:u64):
    return %result
}
```

Do not invent language instructions or assume familiar C constructs exist.
Structured loops, infix expressions, implicit conversions, automatic memory
management, string objects, and operator overloading are absent. Descriptive
register names are permitted and avoid unnecessary renaming during repairs.
