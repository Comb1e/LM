# LM0 V3 generation contract

Build with `make`; compile/check/run with `build/lm0`. Generate V3 for new programs.
V1/V2 remain supported; their contracts are archived in `llm-v1.md`/`llm-v2.md`.

```text
module sum version 3
export c fn solve(data:slice<i64>, key:i64) -> i64:
    total = 0
    for x in data:
        total += x
    return total
```

- Type function parameters and returns. Indent bodies consistently with spaces.
  Use ordinary identifiers, expressions and direct calls. Locals infer a fixed
  type on first assignment and may be reassigned. Assign before every possible
  read. Assign every non-void call result. Executables need `fn main() -> i32`.
- Use `if`/`elif`/`else`, `while`, `for x in sequence`, and
  `for i in range(stop)` or `range(start, stop, positive_step)`. Ranges exclude
  stop. `break`/`continue` target the nearest loop. Conditions are strictly bool;
  `and`/`or` short-circuit. `return` exits a function; `pass` is an empty body.
- Locals and literals take established operand/interface types. Standalone
  integers default to i64, floats to f64. Write `1:u64` or `x:i32 = 1` to select
  another type. There are no implicit conversions; use `cast<T>(value)`.
  Integers wrap; division truncates toward zero, including `//`. Invalid integer
  division and shifts trap. Boolean values do not cast to/from numbers.
- `slice<T>` borrows scalar storage. `view(pointer, u64_length)` constructs one;
  `len(view)` returns i64. Indices are i64; negative/out-of-range indices trap.
  Assignment aliases the same storage. Keep its backing allocation live.
  C slice parameters flatten to pointer then u64 length; slice returns are absent.
- Core memory operations are calls: `stack<T>(N)`, `alloc<T>(count)`, `load(p)`,
  `store(p, value)`, `offset(p, i64_index)`, `field(p, field_name)`,
  `copy(dst, src, u64_bytes)`, `move(dst, src, u64_bytes)`, and `free(p)`.
  Stack allocations are hoisted to entry. Initialize memory and free each heap
  base exactly once. `copy` requires disjoint ranges; `move` permits overlap.
  Use `data name = "text"` and `address(name)` for byte data, without implicit NUL.
- Before implementing utilities, query `build/lm0 library list`, then
  `library describe std_MODULE FUNCTION... --syntax v3`. Import with `use std_MODULE`.
  Use the returned exact signatures and call templates.
  Check fallible statuses before reading outputs: 0 succeeds. Initialize
  owned output-handle slots to null and destroy/close them once. Borrowed views
  expire on mutation/destruction. Handle fields are private. See `libraries.md`.

Check before building. For repair, retrieve only the needed function:

```sh
build/lm0 inspect input.lm0 --function solve --view compact
build/lm0 replace input.lm0 --function solve --replacement solve.txt --expect-revision HASH -o fixed.lm0
```

Use the returned source `revision` as HASH; inspect again after `E_STALE`.
Read `validation` and `unresolved`; successful inspection does not imply valid
source. Emit the complete selected function, including its signature. V3 has
function replacement; generated blocks are internal. Syntax defects require
source editing or a corrected module. Catalogue identity is separate from source
revision. On linking `E_LIBRARY`, rebuild with `make stdlib` or use a matching
`--stdlib-dir`; do not guess foreign declarations.

Use `describe OP...`, `v3.md`, `spec.md`, and `instructions.md` for details.
Report compiler bugs, ambiguous diagnostics and missing capabilities following
`reporting.md`, including known workarounds. Record a submitted issue URL, or
preserve a local report marked **Not submitted** with its reason.
