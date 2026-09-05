# LM0 v2 Generation Contract

Build with `make`; use `build/lm0`. Compilation is native assembly, without Python.
Generate `module NAME version 2`, or exactly the requested function/block.
Use `build/lm0 describe OP...` for instruction rules; consult [spec.md](spec.md)
and [instructions.md](instructions.md) for semantics.

1. Instructions end at newlines. Names are ASCII: `%register`, `@symbol`, `^block`.
   Type function parameters, returns, and block parameters explicitly.
2. Define registers once within each block. Other blocks may reuse their names.
   Function parameters are visible everywhere and cannot be shadowed. Pass all
   other cross-block values through block arguments, assigned simultaneously.
3. Omit destination types when determined locally: `%next = add %i, 1`.
   Literals take the required operand type. Use `1:i32` when ambiguous. No
   default integer/float type or implicit conversion exists. Comparisons return
   `bool`; that result does not determine operand types.
4. Annotate `cast`, `inttoptr`, `null`, and numeric `const` destinations.
   Cast source literals also need a type: `%x:f64 = cast 1:i32`.
   Use explicit casts between numeric types; Boolean casts are forbidden.
5. End each block with `jump`, `branch`, `return`, or `trap`. Entry has no block
   parameters and cannot be targeted. Calls use declared signatures; assign every
   non-void return. Executables need `fn @main() -> i32`.
6. Integers wrap. Zero division, signed minimum/-1, and invalid shifts trap.
   `offset` takes an i64 index; allocation and byte-copy counts are u64.
   `stack T, N` is entry-only. Initialize memory, respect allocation bounds and
   lifetimes, and free each heap base once. Use `move` for overlap, `copy` for
   disjoint ranges. `load`/`store` require exact pointer-element types.
7. C imports/exports use `extern c fn` / `export c fn`; scalar/pointer ABI only.
   No variadics, indirect calls, structured loops, nested expressions, or GC.
   Traps terminate the process. Only `-O0` is supported; sanitizers are unavailable.

```text
module count version 2
fn @count(%n:u64) -> u64 {
^entry:
    jump ^loop(0)
^loop(%i:u64):
    %more = lt %i, %n
    branch %more, ^step(%i), ^done(%i)
^step(%i:u64):
    %next = add %i, 1
    jump ^loop(%next)
^done(%i:u64):
    return %i
}
```

Check before building. For repairs, retrieve only the needed unit:

```sh
build/lm0 inspect input.lm0 --function count --block step --view compact
build/lm0 replace input.lm0 --function count --block step --replacement step.txt --expect-revision HASH -o fixed.lm0
```

Use the returned `revision` as HASH. Inspect again on `E_STALE`. Read
`validation` and `unresolved`; inspection success does not imply valid source.
Syntax errors require direct source editing or a corrected complete module.

Report compiler bugs, unclear guidance/diagnostics, and missing capabilities,
including workarounds, through [reporting.md](reporting.md). Search existing
issues, then submit a minimal reproducer with exact commands and output:
`gh issue create --repo Comb1e/LM --title '[LM0] Summary' --body-file REPORT.md`.
Record the issue URL; when unavailable, preserve the report and state
**Not submitted**, its path, and the reason.
