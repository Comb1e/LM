# ID, spelling, syntax, result, rule, terminator. Shared by every compiler phase.
.equ S_NONE, 0
.equ S_LITERAL, 1
.equ S_UNARY, 2
.equ S_BINARY, 3
.equ S_TERNARY, 4
.equ S_CALL, 5
.equ S_TYPE, 6
.equ S_COUNT, 7
.equ S_ALLOC, 8
.equ S_FIELD, 9
.equ S_SYMBOL, 10
.equ S_TARGET, 11
.equ S_BRANCH, 12
.equ S_RETURN, 13
.equ R_CONST, 0
.equ R_NULL, 1
.equ R_NUM, 2
.equ R_INT, 3
.equ R_EQ, 4
.equ R_ORDER, 5
.equ R_NEG, 6
.equ R_NOT, 7
.equ R_CAST, 8
.equ R_P2I, 9
.equ R_I2P, 10
.equ R_CALL, 11
.equ R_STACK, 12
.equ R_ALLOC, 13
.equ R_FREE, 14
.equ R_OFFSET, 15
.equ R_FIELD, 16
.equ R_LOAD, 17
.equ R_STORE, 18
.equ R_COPY, 19
.equ R_SIZE, 20
.equ R_ADDRESS, 21
.equ R_JUMP, 22
.equ R_BRANCH, 23
.equ R_RETURN, 24
.equ R_TRAP, 25
.macro OP id, spelling, syntax, result, rule, term=0
.equ OP_\spelling, \id
STR op_name_\id, "\spelling"
.quad op_name_\id, \syntax, \result, \rule, \term
.endm
.section .rodata
.align 8
ops:
OP 0, const, S_LITERAL, 1, R_CONST
OP 1, null, S_NONE, 1, R_NULL
OP 2, add, S_BINARY, 1, R_NUM
OP 3, sub, S_BINARY, 1, R_NUM
OP 4, mul, S_BINARY, 1, R_NUM
OP 5, div, S_BINARY, 1, R_NUM
OP 6, rem, S_BINARY, 1, R_INT
OP 7, and, S_BINARY, 1, R_INT
OP 8, or, S_BINARY, 1, R_INT
OP 9, xor, S_BINARY, 1, R_INT
OP 10, shl, S_BINARY, 1, R_INT
OP 11, shr, S_BINARY, 1, R_INT
OP 12, eq, S_BINARY, 1, R_EQ
OP 13, ne, S_BINARY, 1, R_EQ
OP 14, lt, S_BINARY, 1, R_ORDER
OP 15, le, S_BINARY, 1, R_ORDER
OP 16, gt, S_BINARY, 1, R_ORDER
OP 17, ge, S_BINARY, 1, R_ORDER
OP 18, neg, S_UNARY, 1, R_NEG
OP 19, not, S_UNARY, 1, R_NOT
OP 20, cast, S_UNARY, 1, R_CAST
OP 21, ptrtoint, S_UNARY, 1, R_P2I
OP 22, inttoptr, S_UNARY, 1, R_I2P
OP 23, call, S_CALL, 1, R_CALL
OP 24, stack, S_COUNT, 1, R_STACK
OP 25, alloc, S_ALLOC, 1, R_ALLOC
OP 26, free, S_UNARY, 0, R_FREE
OP 27, offset, S_BINARY, 1, R_OFFSET
OP 28, field, S_FIELD, 1, R_FIELD
OP 29, load, S_UNARY, 1, R_LOAD
OP 30, store, S_BINARY, 0, R_STORE
OP 31, copy, S_TERNARY, 0, R_COPY
OP 32, move, S_TERNARY, 0, R_COPY
OP 33, sizeof, S_TYPE, 1, R_SIZE
OP 34, alignof, S_TYPE, 1, R_SIZE
OP 35, address, S_SYMBOL, 1, R_ADDRESS
OP 36, jump, S_TARGET, 0, R_JUMP, 1
OP 37, branch, S_BRANCH, 0, R_BRANCH, 1
OP 38, return, S_RETURN, 0, R_RETURN, 1
OP 39, trap, S_NONE, 0, R_TRAP, 1
.equ OP_COUNT, 40
# Contextual normalization handlers, indexed by the shared semantic rule.
.section .rodata
normalizers:
.quad norm_required,norm_required,norm_same,norm_same,norm_same,norm_same
.quad norm_same,norm_same,norm_cast,norm_p2i,norm_i2p,norm_call
.quad norm_stack,norm_alloc,norm_free,norm_offset,norm_field,norm_load
.quad norm_store,norm_copy,norm_u64_result,norm_address,norm_jump,norm_branch
.quad norm_return,norm_none
STR infer_const,"Destination type required, except true/false infer bool"
STR infer_null,"Destination pointer type required"
STR infer_same,"Operands have one exact type, selected by typed operand or destination; result has that type"
STR infer_compare,"Operands have one exact type, selected by a typed operand; result is bool"
STR infer_cast,"Destination type required; source literal needs its own type"
STR infer_p2i,"Operand needs a pointer type; result is u64"
STR infer_i2p,"Operand is u64; destination pointer type required"
STR infer_call,"Argument and result types come from the function signature"
STR infer_stack,"Result is ptr<T>; count is a positive integer literal"
STR infer_alloc,"Result is ptr<T>; count is u64"
STR infer_free,"Operand needs an explicit or previously established pointer type"
STR infer_offset,"Base and result have the same pointer type; index is i64"
STR infer_field,"Base needs ptr<Struct>; result is pointer to the named field type"
STR infer_load,"Operand is ptr<T>; result is T; either type can establish the other"
STR infer_store,"Operands are ptr<T> and T; either type can establish the other"
STR infer_copy,"Pointer operands need established types; byte count is u64"
STR infer_size,"Result is u64"
STR infer_address,"Result is ptr<u8>"
STR infer_jump,"Argument types come from the target block parameters"
STR infer_branch,"Condition is bool; argument types come from each target block"
STR infer_return,"Operand type comes from the function return type; absent for void"
STR infer_none,"No operands or result"
.section .rodata
inference_descriptions:
.quad infer_const,infer_null,infer_same,infer_same,infer_compare,infer_compare
.quad infer_same,infer_same,infer_cast,infer_p2i,infer_i2p,infer_call
.quad infer_stack,infer_alloc,infer_free,infer_offset,infer_field,infer_load
.quad infer_store,infer_copy,infer_size,infer_address,infer_jump,infer_branch
.quad infer_return,infer_none
.section .rodata
STR op_desc_const, "Typed scalar literal"
STR op_desc_null, "Null pointer"
STR op_desc_numeric, "Same-type numeric arithmetic"
STR op_desc_integer, "Same-type integer operation"
STR op_desc_equality, "Same-type scalar equality, producing bool"
STR op_desc_ordered, "Same-type numeric comparison, producing bool"
STR op_desc_neg, "Numeric negation"
STR op_desc_not, "Integer bit complement or Boolean negation"
STR op_desc_cast, "Explicit numeric conversion or pointer reinterpretation"
STR op_desc_p2i, "Convert pointer to u64"
STR op_desc_i2p, "Convert u64 to pointer"
STR op_desc_call, "Direct function call; omit destination for void"
STR op_desc_stack, "Fixed-size allocation in the function entry block"
STR op_desc_alloc, "Allocate count elements on the heap; count is u64"
STR op_desc_free, "Release a heap base pointer; null is allowed"
STR op_desc_offset, "Offset typed pointer by i64 elements"
STR op_desc_field, "Address of a named struct field"
STR op_desc_load, "Load a scalar from memory"
STR op_desc_store, "Store scalar to pointer"
STR op_desc_copy, "Copy u64 bytes between disjoint memory ranges"
STR op_desc_move, "Copy u64 bytes; overlapping memory ranges are allowed"
STR op_desc_sizeof, "Target storage size as u64"
STR op_desc_alignof, "Target alignment as u64"
STR op_desc_address, "Address of static byte data as ptr<u8>"
STR op_desc_jump, "Unconditional block transfer"
STR op_desc_branch, "Boolean conditional block transfer"
STR op_desc_return, "Return scalar or void"
STR op_desc_trap, "Terminate with E_TRAP"
op_descriptions:
.quad op_desc_const,op_desc_null
.quad op_desc_numeric,op_desc_numeric,op_desc_numeric,op_desc_numeric
.quad op_desc_integer,op_desc_integer,op_desc_integer,op_desc_integer,op_desc_integer,op_desc_integer
.quad op_desc_equality,op_desc_equality
.quad op_desc_ordered,op_desc_ordered,op_desc_ordered,op_desc_ordered
.quad op_desc_neg,op_desc_not,op_desc_cast,op_desc_p2i,op_desc_i2p,op_desc_call
.quad op_desc_stack,op_desc_alloc,op_desc_free,op_desc_offset,op_desc_field,op_desc_load,op_desc_store
.quad op_desc_copy,op_desc_move,op_desc_sizeof,op_desc_alignof,op_desc_address
.quad op_desc_jump,op_desc_branch,op_desc_return,op_desc_trap
.text
