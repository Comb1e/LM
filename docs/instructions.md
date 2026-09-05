# Instruction Reference

`%r:T =` denotes a required destination. `N` is an integer literal; all other
numeric operands are registers. See the specification for exceptional cases.
The compiler's shared registry is `lm0/ops.py`; `inspect` exposes its entries.

| Form | Type rule / behavior |
| --- | --- |
| `%r:T = const L` | Numeric or Boolean literal in T's range |
| `%r:ptr<T> = null` | Null pointer |
| `%r:T = add/sub/mul/div %a, %b` | Same numeric T; select one named opcode |
| `%r:T = rem/and/or/xor/shl/shr %a, %b` | Same integer T |
| `%r:bool = eq/ne %a, %b` | Same scalar or pointer type |
| `%r:bool = lt/le/gt/ge %a, %b` | Same numeric type |
| `%r:T = neg %a` | Numeric negation, same T |
| `%r:T = not %a` | Integer complement or Boolean negation, same T |
| `%r:T = cast %a` | Numeric-to-numeric or pointer-to-pointer conversion |
| `%r:u64 = ptrtoint %p` | Native pointer address |
| `%r:ptr<T> = inttoptr %bits` | u64 to native pointer |
| `%r:T = call @f(%args)` | Exact argument and return types |
| `call @f(%args)` | Void-returning call |
| `%r:ptr<T> = stack T, N` | Entry-only fixed allocation; positive N |
| `%r:ptr<T> = alloc T, %count` | Heap allocation; u64 element count |
| `free %pointer` | Release live heap base or null |
| `%r:ptr<T> = offset %base, %index` | ptr<T> base and i64 element displacement |
| `%r:ptr<F> = field %base, name` | ptr<Struct> base; field has type F |
| `%r:T = load %pointer` | Read scalar T from ptr<T> |
| `store %pointer, %value` | Write scalar T to ptr<T> |
| `copy %destination, %source, %bytes` | Two pointers and u64 byte count; disjoint ranges |
| `%r:u64 = sizeof T` | Target ABI storage size |
| `%r:u64 = alignof T` | Target ABI alignment |
| `%r:ptr<u8> = address @data` | Static UTF-8 byte data |
| `jump ^target(%args)` | Transfer block arguments simultaneously |
| `branch %condition, ^yes(%args), ^no(%args)` | Boolean condition; execute only chosen edge |
| `return %value` | Scalar/pointer return matching the function |
| `return` | Void return |
| `trap` | JSON diagnostic and process exit 70 |

Notation containing slashes groups individual opcodes; slashes are not source
syntax. Instruction spellings have no aliases. Branch and call argument lists
always use parentheses, including empty lists. A block label may omit its empty
parameter list: `^entry:` and `^entry():` declare the same empty interface.
