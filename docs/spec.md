# LM0 Language Specification, Versions 1 and 2

## Representation

Source is UTF-8. Identifiers match `[A-Za-z_][A-Za-z_0-9]*`; names are
case-sensitive. `%name` identifies a register, `@name` a function or data symbol,
and `^name` a block. Horizontal whitespace and `#` comments are insignificant.
Newlines terminate declarations and instructions. Blank lines are allowed;
indentation has no meaning. New files start with `module NAME version 2`.
Version 1 remains accepted with its original explicit syntax and scoping rules.

Functions and data share a module-wide namespace. Struct types have a separate
namespace and cannot redefine built-in types. Block names are unique within a
function. In v2, block parameters and instruction destinations share a register
namespace local to their block. Function parameters are visible in every block
and cannot be shadowed. Redefinition within a block is an error. Version 1 instead
requires every register name to be unique throughout its function.

There are no expressions inside operands, overload declarations, macros,
implicit casts, or alternative operator spellings.
`%sum:i32 = add %left, %right` uses the operand/result types to select the
instruction's numeric behavior. Every non-void result must be assigned.

## V2 Contextual Types and Literals

A destination may omit `:T` when its type follows from the instruction and local
operands: `%sum = add %left, 1`. Function and block interfaces remain explicitly
typed. This is local inference; subsequent uses never determine a destination's
type, and no whole-function type unification or default numeric type is used.

Any scalar/pointer register operand may instead be a literal. Write `1:i32`,
`1.5:f64`, `true:bool`, or `null:ptr<i32>` for an explicit literal type. A bare
literal uses the operand's required type; `true` and `false` establish `bool`
without context. Numeric literals and null pointers otherwise have no default.

- Arithmetic and unary operations use a typed operand or an explicit destination
  to establish their common type. Comparisons use a typed operand to establish
  operand types and produce `bool`. `%p = lt 1, 2` is ambiguous.
- Calls, returns, and block arguments take their types from the declared
  interface. Branch conditions are `bool`; pointer offsets are `i64`; heap and
  byte-copy counts are `u64`.
- `stack`/`alloc` infer `ptr<T>`, `field` infers the field pointer, `address`
  infers `ptr<u8>`, and `sizeof`/`alignof`/`ptrtoint` infer `u64`.
- `load` relates result T and operand `ptr<T>`; `store` relates `ptr<T>` and T.
  Either established type can supply the other. `offset` relates the same base
  and result pointer type. Other pointer operands need an established type:
  use `free null:ptr<void>`; `free null` is ambiguous.
- `cast`, `inttoptr`, and standalone `null` require a destination type. Numeric
  `const` also requires one; Boolean `const` can infer `bool`. A cast's source
  type is independent: `%x:f64 = cast 1:i32` is valid; `cast 1` is ambiguous.

Explicit annotations must agree with these constraints. No annotation converts
an operand. Literal range checks and runtime arithmetic behavior are unchanged.
Ambiguity reports `E_INFER`; conflicts report `E_TYPE`. The native compiler
normalizes literals into internal typed constants with original source spans.
Version 1 requires destination types and separate constant/null instructions.

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
alignments, and 64-bit pointers. The native assembly compiler verifies GCC's
target triple before using it as an assembler/linker driver. Changing the
configured triple does not port the language to another architecture.

## Functions and Control Flow

```text
fn @count(%limit:u64) -> u64 {
^entry:
    jump ^loop(0)
^loop(%index:u64):
    %more = lt %index, %limit
    branch %more, ^step(%index), ^done(%index)
^step(%previous:u64):
    %next = add %previous, 1
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
`return` takes one value for a scalar/pointer return or none for `void`.
An executable requires an internal `fn @main() -> i32`; an object file does not.
Normal process exit exposes the platform's low eight bits of main's return.

## Numeric Semantics

Integer literals are decimal or `0x` hexadecimal with an optional minus sign.
They must lie within the declared type's range. Boolean literals are `true`
and `false`. Float literals use decimal/exponent notation or `inf`, `-inf`,
and `nan`. A finite literal that overflows its declared type is an error.
`null` is a separate instruction for pointers and a contextual operand in v2.

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
  The x86-64 backend accepts unaligned addresses directly. Memory must be
  live, large enough, and initialized with a valid representation when read.
- `copy %destination, %source, %bytes` copies a `u64` number of bytes. Positive
  counts require valid disjoint ranges. Zero counts perform no access and permit
  null pointers. Overlapping copies are undefined.
- `move %destination, %source, %bytes` has the same pointer and count types as
  `copy`, but permits overlapping ranges. It behaves as if the source bytes were
  read into temporary storage before any destination bytes were written. Zero
  counts perform no access and permit null pointers. The backend calls `memmove`.

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

Version 2 accepts `use std_MODULE` declarations. The compiler's bundled catalogue
supplies that module's exact foreign signatures, shared types, and transitive
dependencies. Imports are order independent and repeated imports are idempotent.
Unknown modules report `E_LIBRARY`; user declarations conflicting with imported
types/functions report `E_DUPLICATE`. Imports are declarations, not textual
inclusion, and do not add initialization effects. There are no wildcard imports,
aliases, arbitrary paths, or third-party dependency resolution. Version 1 does
not accept `use`. See the [standard library](libraries.md).

The compiler verifies calls against imported signatures. Library builds also
verify LM0 exports against the same catalogue, and C adapters include its generated
header. Executables/shared libraries automatically link the standard archive and
libm when a standard module is imported. A catalogue-specific symbol binds each
importing object to the matching archive; `--kind object` and `emit-asm` report
their `link_requirements` for a downstream linker. Library declarations retain
the original import's diagnostic span and cannot be selected for source replacement.

There is no general-purpose module linker or cross-object type checking for
arbitrary C declarations. Multiple objects can communicate through C exports/imports.
Variadic functions, function
pointers, callbacks, and aggregate-by-value signatures are not supported.

`lm0 build --kind shared` produces a position-independent shared library without
a `main` wrapper. Exported functions can be loaded by native hosts; internal
functions and data remain private. Imports must resolve at link time through
`--link` or `--library`. The native compiler rejects sanitizer requests. Traps terminate
the host process; they do not become foreign exceptions.

## Diagnostics and Limits

Validation returns structured diagnostics with `code`, `phase`, `message`, source
span, and available function/block/register identifiers. Type mismatches include
`expected` and `actual`. Execution traps emit a JSON diagnostic to stderr and
exit with status 70. Generated assembly includes `.file` and `.loc` source
mappings. There is no language-level call stack trace yet.

Configuration bounds source size, nested types, aggregate allocation declarations,
compiler duration, process duration, and captured output. The native compiler
stops after its first diagnostic; `limits.diagnostics` remains accepted for
configuration compatibility with the reference implementation.
Heap allocations are bounded by native address space, not the static aggregate
limit. There is no recursion-depth guard; native stack limits apply. Execution
and compiler subprocess groups are terminated on time or output exhaustion.
These are operational limits, not an operating-system security sandbox.
