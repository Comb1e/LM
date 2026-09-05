# LM0 Language Specification, Version 1

## Representation

Source is UTF-8. Identifiers match `[A-Za-z_][A-Za-z_0-9]*`; names are
case-sensitive. `%name` identifies a register, `@name` a function or data symbol,
and `^name` a block. Horizontal whitespace and `#` comments are insignificant.
Newlines terminate declarations and instructions. Blank lines are allowed;
indentation has no meaning. Files start with `module NAME version 1`.

Functions and data share a module-wide namespace. Struct types have a separate
namespace and cannot redefine built-in types. Block names are unique within a
function. Function parameters, block parameters, and instruction destinations
share a function-wide register namespace; shadowing and redefinition are errors.

There are no expressions inside operands, overload declarations, macros,
implicit casts, inferred register types, or alternative operator spellings.
`%sum:i32 = add %left, %right` uses the operand/result types to select the
instruction's numeric behavior. Literal operands require separate `const`
instructions. Every non-void result must be assigned.

## Types and Layout

| Type | Meaning |
| --- | --- |
| `bool` | Boolean, one byte in memory; valid representations are 0 and 1 |
| `i8/i16/i32/i64` | Two's-complement signed integers |
| `u8/u16/u32/u64` | Unsigned integers |
| `f32/f64` | IEEE binary32/binary64 |
| `ptr<T>` | Native pointer; pointers themselves occupy eight bytes |
| `[T;N]` | Fixed array of positive integer length N |
| `Name` | Named struct |
| `void` | No return value; permitted behind a pointer, never in storage |

Only scalars and pointers may inhabit registers or cross a function interface.
Arrays and structs are memory objects, accessed by pointer. `ptr<void>` is useful
for FFI and casts; it cannot be offset, loaded, or allocated without casting to
a storage type. Pointer comparisons support only equality and inequality.

```text
struct Node {
    value:i64
    next:ptr<Node>
}
```

Struct fields are ordered, with the x86-64 System V C ABI's padding and alignment.
An array has its element alignment and occupies N element sizes. Structs must
be nonempty. Recursive by-value layouts are invalid; pointer recursion is valid.
Types can reference later struct definitions. `sizeof T` and `alignof T`
produce `u64` values. `field %pointer, name` returns a pointer to the field's
declared type. Padding bytes have no defined initial value.

The current target has eight-bit bytes, little-endian memory, natural scalar
alignments, and 64-bit pointers. The compiler verifies the GCC target triple and
emits ABI assertions. Changing the configured triple does not port the language
to another architecture.

## Functions and Control Flow

```text
fn @count(%limit:u64) -> u64 {
^entry:
    %zero:u64 = const 0
    jump ^loop(%zero)
^loop(%index:u64):
    %more:bool = lt %index, %limit
    branch %more, ^step(%index), ^done(%index)
^step(%previous:u64):
    %one:u64 = const 1
    %next:u64 = add %previous, %one
    jump ^loop(%next)
^done(%result:u64):
    return %result
}
```

The first block is the entry block regardless of its name. It has no block
parameters and cannot be a branch target. Every block ends in exactly one
terminator: `jump`, `branch`, `return`, or `trap`. No instruction follows it.
Unreachable blocks are allowed but still checked.

Instructions can read function parameters, their own block parameters, and
registers defined earlier in the same block. Values from another block must
be passed explicitly. Block argument arity and types must match exactly.
An edge evaluates all arguments before assigning any destination block
parameters, so swaps and loop backedges have simultaneous-assignment semantics.
Only the selected branch executes. Effects occur in instruction order.

Calls may refer to later functions, including mutual recursion. There are no
indirect calls, closures, first-class functions, or tail-call guarantees.
`return` takes one register for a scalar/pointer return or none for `void`.
An executable requires an internal `fn @main() -> i32`; an object file does not.
Normal process exit exposes the platform's low eight bits of main's return.

## Numeric Semantics

Integer literals are decimal or `0x` hexadecimal with an optional minus sign.
They must lie within the declared type's range. Boolean literals are `true`
and `false`. Float literals use decimal/exponent notation or `inf`, `-inf`,
and `nan`. A finite literal that overflows its declared type is an error.
`null` is a separate instruction for pointers.

For same-type integers, addition, subtraction, multiplication, and negation
wrap modulo 2^width. Signed results reinterpret the resulting bit pattern as
two's complement. Division truncates toward zero and remainder has the dividend's
sign. Division/remainder by zero and signed minimum divided by -1 trap.

Bitwise operations require integers; `not` complements all bits, or negates a
Boolean. Both operands of a shift have the same integer type. Shift counts must
be in [0, width); negative or excessive counts trap. Left shift discards bits
beyond the width. Right shift fills with the sign bit for signed types and zeros
for unsigned types. Comparisons produce Boolean results.

Float arithmetic follows the target's IEEE behavior, including NaN, infinity,
and signed zero, assuming the default rounding environment. Operations round to
the declared type; contraction and fast-math are disabled. NaN payloads are not
specified. Equality with NaN is false and inequality is true; ordered comparisons
with NaN are false. Float division by zero uses IEEE results, not an integer trap.

`cast` supports numeric conversions and pointer reinterpretation. Integer casts
preserve the source's mathematical value modulo the target width, then interpret
signedness. Integer-to-float conversion rounds to the destination precision.
Float-to-integer conversion first truncates toward zero, then checks the exact
destination range; NaN, infinity, and out-of-range values trap. Thus -0.5 converts
to unsigned zero. Float narrowing uses round-to-nearest with ties to even in the
default environment, with overflow to signed infinity. Booleans do not convert
implicitly or through `cast`; use control flow to select numeric values.

`ptrtoint` converts a pointer to `u64`; `inttoptr` converts `u64` to a chosen
pointer type. A live pointer can round-trip. Manufacturing addresses does not
establish a valid allocation or lifetime.

## Memory

- `stack T, N` allocates N uninitialized elements for the current function call.
  It is legal only in the entry block. Storage lives until that call returns.
- `alloc T, %count` takes a `u64` count and returns `ptr<T>`. It traps on size
  overflow or allocator failure. Count zero produces a non-null, freeable pointer
  with no accessible elements. There is no automatic destruction.
- `free %pointer` accepts null or the original base of a live heap allocation,
  including an equivalent pointer cast. Interior, stack, static, or already-freed
  pointers are invalid arguments.
- `offset %pointer, %index` takes `i64` element units, including negative offsets.
  The byte displacement must fit `PTRDIFF_MAX`, otherwise it traps. Valid pointer
  arithmetic stays within the allocation or one past it. Offset zero preserves
  the original pointer, including null.
- `load` and `store` operate on a scalar of the pointer's exact element type.
  They accept unaligned addresses and lower through `memcpy`. Memory must be
  live, large enough, and initialized with a valid representation when read.
- `copy %destination, %source, %bytes` copies a `u64` number of bytes. Positive
  counts require valid disjoint ranges. Zero counts perform no access and permit
  null pointers. Overlapping copies are undefined.
- `move %destination, %source, %bytes` has the same pointer and count types as
  `copy`, but permits overlapping ranges. It behaves as if the source bytes were
  read into temporary storage before any destination bytes were written. Zero
  counts perform no access and permit null pointers. It lowers to C `memmove`.

There is no provenance tracker or bounds/lifetime verifier. Invalid memory
operations, reads of uninitialized values, and invalid Boolean/pointer
representations have undefined behavior. Type checking does not establish memory
safety. The backend intentionally gives numeric errors stronger guarantees than
invalid memory access.

```text
data @hello = "Hello\u0000"
```

Static data uses JSON string escaping and UTF-8 encoding. No NUL terminator is
appended. `address @hello` returns `ptr<u8>`. Static data is mutable and lives for
the entire program. Empty data has one backing zero byte. Unicode indexing is
explicit byte indexing, not code-point indexing.

## Native Interfaces

```text
extern c fn @puts(%text:ptr<u8>) -> i32
export c fn @twice(%value:i64) -> i64 {
^entry:
    %two:i64 = const 2
    %result:i64 = mul %value, %two
    return %result
}
```

C exports retain their declared symbol names; internal functions have local
linkage and mangled names. The names `main` and `lm0_*` are reserved at the C
interface. Imports resolve at link time. ABI signatures are the programmer's
responsibility. All object pointers use the target's `void *` representation.
Signedness/width, return types, and actual foreign function conventions must match.

There is no language-level module linker or cross-object type checking. Multiple
objects can communicate through C exports/imports. Variadic functions, function
pointers, callbacks, and aggregate-by-value signatures are not supported.

`lm0 build --kind shared` produces a position-independent shared library without
a `main` wrapper. Exported functions can be loaded by native hosts such as Python
`ctypes`; internal functions and data remain private. Imports must resolve at
link time through `--link` or `--library`. A sanitized library must be loaded by
a host with a compatible sanitizer runtime initialized before the library.
Traps still terminate the host process; they do not become foreign exceptions.

## Diagnostics and Limits

Validation returns structured diagnostics with `code`, `phase`, `message`, source
span, and available function/block/register identifiers. Type mismatches include
`expected` and `actual`. Execution traps emit a JSON diagnostic to stderr and
exit with status 70. Generated C includes `#line` mappings for compiler and
sanitizer locations. There is no language-level call stack trace yet.

Configuration bounds source size, nested types, aggregate allocation declarations,
diagnostic count, compiler duration, process duration, and captured output.
Heap allocations are bounded by native address space, not the static aggregate
limit. There is no recursion-depth guard; native stack limits apply. Execution
and compiler subprocess groups are terminated on time or output exhaustion.
These are operational limits, not an operating-system security sandbox.
