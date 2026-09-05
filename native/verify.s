.text
type_error:
 FAIL e_type, m_type
layout_error:
 FAIL e_layout, m_layout

FUNC same_type
 cmp rdi, rsi
 je .same_end
 mov rax, [rdi+NAME]
 mov [diag_actual], rax
 mov rax, [rsi+NAME]
 mov [diag_expected], rax
 jmp type_error
.same_end:
 RETURN

FUNC scalar
 mov rax, [rdi+FLAGS]
 test rax, rax
 jz type_error
 cmp rax, TY_PTR
 ja type_error
 RETURN

FUNC numeric
 mov rax, [rdi+FLAGS]
 cmp rax, TY_I8
 jb type_error
 cmp rax, TY_F64
 ja type_error
 RETURN

FUNC integer_type
 call numeric
 cmp rax, TY_U64
 ja type_error
 RETURN

FUNC pointer
 cmp qword ptr [rdi+FLAGS], TY_PTR
 jne type_error
 mov rax, [rdi+TYPE]
 RETURN

FUNC layout
 mov r12, rdi
 cmp qword ptr [r12+SIZE], 0
 jne .layout_done
 cmp qword ptr [r12+C_], 0
 jne layout_error
 mov qword ptr [r12+C_], 1
 mov rax, [r12+FLAGS]
 test rax, rax
 jz type_error
 cmp rax, TY_PTR
 ja .layout_complex
 movzx edx, byte ptr [scalar_sizes+rax]
 mov [r12+SIZE], rdx
 mov [r12+ALIGN], rdx
 jmp .layout_finish
.layout_complex:
 cmp rax, TY_ARRAY
 jne .layout_struct
 C layout, "qword ptr [r12+TYPE]"
 mov r13, rdx
 mul qword ptr [r12+A]
 test rdx, rdx
 jnz layout_error
 mov [r12+SIZE], rax
 mov [r12+ALIGN], r13
 jmp .layout_limit
.layout_struct:
 cmp qword ptr [r12+B], 0
 je type_error
 mov r13, [r12+LIST]
 test r13, r13
 jz layout_error
 xor r14d, r14d
 mov r15d, 1
.layout_fields:
 C layout, "qword ptr [r13+TYPE]"
 lea rcx, [rdx-1]
 add r14, rcx
 jc layout_error
 neg rdx
 and r14, rdx
 neg rdx
 mov [r13+SIZE], r14
 add r14, rax
 jc layout_error
 cmp r15, rdx
 cmovb r15, rdx
 mov r13, [r13+NEXT]
 test r13, r13
 jnz .layout_fields
 lea rax, [r14+r15-1]
 mov rdx, r15
 neg rdx
 and rax, rdx
 mov [r12+SIZE], rax
 mov [r12+ALIGN], r15
.layout_limit:
 test rax, rax
 jz layout_error
 cmp rax, [cfg_aggregate]
 ja layout_error
.layout_finish:
 mov qword ptr [r12+C_], 0
.layout_done:
 mov rax, [r12+SIZE]
 mov rdx, [r12+ALIGN]
 RETURN
.section .rodata
scalar_sizes: .byte 0,1,1,2,4,8,1,2,4,8,4,8,8
.text

FUNC unique_list
 mov r12, rdi
.unique_outer:
 test r12, r12
 jz .unique_done
 mov rax, [r12+TOKEN]
 mov [diag_tok], rax
 C find, "qword ptr [r12+NEXT]", "qword ptr [r12+NAME]"
 test rax, rax
 jnz .duplicate_error
 mov r12, [r12+NEXT]
 jmp .unique_outer
.unique_done:
 RETURN

FUNC add_register
 mov r12, rdi
 mov r13, rsi
 mov r14, [cur_fn]
 mov r15, [r14+AUX]
.addreg_find:
 test r15, r15
 jz .addreg_new
 C strcmp, "qword ptr [r15+NAME]", "qword ptr [r12+NAME]"
 test eax, eax
 jz .duplicate_error
 mov r15, [r15+AUX]
 jmp .addreg_find
.addreg_new:
 C scalar, "qword ptr [r12+TYPE]"
 mov [r12+A], r13
 mov rax, [r14+AUX]
 mov [r12+AUX], rax
 mov [r14+AUX], r12
 add qword ptr [r14+SIZE], 8
 mov rax, [r14+SIZE]
 mov [r12+SIZE], rax
 RETURN

FUNC resolve_reg
 mov r12, rdi
 mov rax, [cur_fn]
 mov r13, [rax+AUX]
.resolve_loop:
 test r13, r13
 jz .resolve_bad
 C strcmp, "qword ptr [r13+NAME]", r12
 test eax, eax
 jz .resolve_found
 mov r13, [r13+AUX]
 jmp .resolve_loop
.resolve_found:
 mov rax, [r13+A]
 test rax, rax
 jz .resolve_good
 cmp rax, [cur_block]
 jne .resolve_bad
.resolve_good:
 mov rax, r13
 RETURN
.resolve_bad:
 mov [diag_reg], r12
 FAIL e_register, m_register

FUNC verify_args
 mov r12, rdi
 mov r13, rsi
.vargs_loop:
 test r12, r12
 jz .vargs_end
 test r13, r13
 jz .arity_error
 C resolve_reg, "qword ptr [r12+NAME]"
 mov [r12+TYPE], rax
 C same_type, "qword ptr [rax+TYPE]", "qword ptr [r13+TYPE]"
 mov r12, [r12+NEXT]
 mov r13, [r13+NEXT]
 jmp .vargs_loop
.vargs_end:
 test r13, r13
 jnz .arity_error
 RETURN
.arity_error:
 FAIL e_arity, m_arity

FUNC verify_target
 mov r12, rdi
 mov rax, [cur_fn]
 C find, "qword ptr [rax+BODY]", "qword ptr [r12+NAME]"
 test rax, rax
 jz .target_missing
 mov [r12+TYPE], rax
 mov rdx, [cur_fn]
 cmp rax, [rdx+BODY]
 je .entry_error
 C verify_args, "qword ptr [r12+LIST]", "qword ptr [rax+LIST]"
 RETURN
.target_missing:
 FAIL e_block, m_target_block
.entry_error:
 FAIL e_entry, m_entry
STR m_target_block, "Unknown target block"

FUNC verify_constant
 mov r12, rdi
 mov r13, rsi
 mov r14, [r13+FLAGS]
 cmp r14, TY_BOOL
 je .const_bool
 cmp r14, TY_I8
 jb literal_error
 cmp r14, TY_U64
 jbe .const_integer
 cmp r14, TY_F64
 ja literal_error
 C strpbrk, r12, "offset hex_letters"
 test rax, rax
 jnz literal_error
 lea rsi, [rbp-48]
 C strtod, r12, rsi
 mov rax, [rbp-48]
 cmp byte ptr [rax], 0
 jne literal_error
 cmp rax, r12
 je literal_error
 cmp r14, TY_F32
 jne .const_double
 cvtsd2ss xmm0, xmm0
 movd ebx, xmm0
 mov eax, ebx
 and eax, 0x7f800000
 cmp eax, 0x7f800000
 je .const_nonfinite
 jmp .const_bits
.const_double:
 movq rbx, xmm0
 mov rax, rbx
 shr rax, 52
 and eax, 2047
 cmp eax, 2047
 jne .const_bits
.const_nonfinite:
 EQ r12, s_inf
 jz .const_bits
 EQ r12, s_ninf
 jz .const_bits
 EQ r12, s_nan
 jnz literal_error
.const_bits:
 mov rax, rbx
 RETURN
.const_bool:
 EQ r12, s_true
 jz .const_true
 EQ r12, s_false
 jnz literal_error
 xor eax, eax
 RETURN
.const_true:
 mov eax, 1
 RETURN
.const_integer:
 C integer_text, r12
 mov r15, rax
 mov rbx, rdx
 C layout, r13
 lea rcx, [rax*8]
 mov rax, -1
 mov edx, 64
 sub edx, ecx
 mov ecx, edx
 shr rax, cl
 cmp r14, TY_U8
 jae .const_unsigned
 shr rax, 1
 test rbx, rbx
 jz .const_range
 inc rax
 cmp r15, rax
 ja literal_error
 mov rax, r15
 neg rax
 RETURN
.const_unsigned:
 test rbx, rbx
 jnz literal_error
.const_range:
 cmp r15, rax
 ja literal_error
 mov rax, r15
 RETURN
STR hex_letters, "xX"

# Resolve ordinary register operands once. Code generation and inspection use
# the verified record pointers, never re-interpret operand strings.
FUNC verify_instruction
 mov r12, rdi
 mov [cur_ins], r12
 mov rax, [r12+TOKEN]
 mov [diag_tok], rax
 mov rbx, [r12+FLAGS]
 imul rax, rbx, 40
 mov r13, [ops+rax+24]
 mov r14, [builtin+TY_VOID*8]
 mov rdx, [r12+TYPE]
 test rdx, rdx
 jz .verify_dest
 mov r14, [rdx+TYPE]
 C scalar, r14
.verify_dest:
 cmp r13, R_CALL
 je .verify_resolve
 imul rax, rbx, 40
 mov rcx, [ops+rax+16]
 xor edx, edx
 cmp qword ptr [r12+TYPE], 0
 setne dl
 cmp rcx, rdx
 jne .result_error
.verify_resolve:
 imul rax, rbx, 40
 mov rax, [ops+rax+8]
 cmp rax, S_UNARY
 jb .verify_dispatch
 cmp rax, S_TERNARY
 ja .verify_special_reg
 lea r15, [rax-1]
 xor ebx, ebx
.verify_regs:
 C resolve_reg, "qword ptr [r12+A+rbx*8]"
 mov [r12+A+rbx*8], rax
 inc rbx
 cmp rbx, r15
 jb .verify_regs
 jmp .verify_dispatch
.verify_special_reg:
 cmp rax, S_FIELD
 je .verify_first_reg
 cmp rax, S_BRANCH
 je .verify_first_reg
 cmp rax, S_RETURN
 jne .verify_dispatch
 cmp qword ptr [r12+A], 0
 je .verify_dispatch
.verify_first_reg:
 C resolve_reg, "qword ptr [r12+A]"
 mov [r12+A], rax
.verify_dispatch:
 mov rbx, [r12+FLAGS]
 cmp r13, R_CONST
 je .v_const
 cmp r13, R_NULL
 je .v_null
 cmp r13, R_ORDER
 jbe .v_binary
 cmp r13, R_NOT
 jbe .v_unary
 cmp r13, R_CAST
 je .v_cast
 cmp r13, R_P2I
 je .v_p2i
 cmp r13, R_I2P
 je .v_i2p
 cmp r13, R_CALL
 je .v_call
 cmp r13, R_ALLOC
 jbe .v_allocation
 cmp r13, R_FREE
 je .v_free
 cmp r13, R_OFFSET
 je .v_offset
 cmp r13, R_FIELD
 je .v_field
 cmp r13, R_LOAD
 je .v_load
 cmp r13, R_STORE
 je .v_store
 cmp r13, R_COPY
 je .v_copy
 cmp r13, R_SIZE
 je .v_size
 cmp r13, R_ADDRESS
 je .v_address
 cmp r13, R_JUMP
 je .v_jump
 cmp r13, R_BRANCH
 je .v_branch
 cmp r13, R_RETURN
 je .v_return
 jmp .v_done
.v_const:
 C verify_constant, "qword ptr [r12+A]", r14
 mov [r12+D], rax
 jmp .v_done
.v_null:
 C pointer, r14
 jmp .v_done
.v_binary:
 mov rax, [r12+A]
 mov r15, [rax+TYPE]
 mov rax, [r12+B]
 C same_type, "qword ptr [rax+TYPE]", r15
 cmp r13, R_INT
 je .v_binary_integer
 cmp r13, R_EQ
 je .v_binary_equal
 C numeric, r15
 jmp .v_binary_result
.v_binary_integer:
 C integer_type, r15
 jmp .v_binary_result
.v_binary_equal:
 C scalar, r15
.v_binary_result:
 cmp r13, R_EQ
 jb .v_same_result
 mov r15, [builtin+TY_BOOL*8]
.v_same_result:
 C same_type, r14, r15
 jmp .v_done
.v_unary:
 mov rax, [r12+A]
 mov r15, [rax+TYPE]
 cmp r13, R_NEG
 je .v_unary_numeric
 cmp qword ptr [r15+FLAGS], TY_BOOL
 je .v_same_result
 C integer_type, r15
 jmp .v_same_result
.v_unary_numeric:
 C numeric, r15
 jmp .v_same_result
.v_cast:
 mov rax, [r12+A]
 mov r15, [rax+TYPE]
 cmp qword ptr [r15+FLAGS], TY_PTR
 jne .v_cast_numeric
 cmp qword ptr [r14+FLAGS], TY_PTR
 je .v_done
 jmp .cast_error
.v_cast_numeric:
 mov rax, [r15+FLAGS]
 sub rax, TY_I8
 cmp rax, TY_F64-TY_I8
 ja .cast_error
 mov rax, [r14+FLAGS]
 sub rax, TY_I8
 cmp rax, TY_F64-TY_I8
 ja .cast_error
 jmp .v_done
.v_p2i:
 mov rax, [r12+A]
 C pointer, "qword ptr [rax+TYPE]"
 C same_type, r14, "qword ptr [builtin+TY_U64*8]"
 jmp .v_done
.v_i2p:
 mov rax, [r12+A]
 C same_type, "qword ptr [rax+TYPE]", "qword ptr [builtin+TY_U64*8]"
 C pointer, r14
 jmp .v_done
.v_call:
 C find, "qword ptr [functions]", "qword ptr [r12+A]"
 test rax, rax
 jz .function_error
 mov r15, rax
 mov [r12+A], rax
 C verify_args, "qword ptr [r12+LIST]", "qword ptr [r15+LIST]"
 C same_type, r14, "qword ptr [r15+TYPE]"
 jmp .v_done
.v_allocation:
 C layout, "qword ptr [r12+A]"
 mov r15, rax
 C pointer_type, "qword ptr [r12+A]"
 C same_type, r14, rax
 cmp r13, R_ALLOC
 je .v_alloc
 mov rax, [cur_fn]
 mov rdx, [cur_block]
 cmp rdx, [rax+BODY]
 jne .stack_error
 mov rax, [r12+B]
 test rax, rax
 jle layout_error
 mul r15
 test rdx, rdx
 jnz layout_error
 cmp rax, [cfg_aggregate]
 ja layout_error
 mov [r12+D], rax
 jmp .v_done
.v_alloc:
 C resolve_reg, "qword ptr [r12+B]"
 mov [r12+B], rax
 C same_type, "qword ptr [rax+TYPE]", "qword ptr [builtin+TY_U64*8]"
 jmp .v_done
.v_free:
 mov rax, [r12+A]
 C pointer, "qword ptr [rax+TYPE]"
 jmp .v_done
.v_offset:
 mov rax, [r12+A]
 mov r15, [rax+TYPE]
 C pointer, r15
 C layout, rax
 mov [r12+D], rax
 mov rax, [r12+B]
 C same_type, "qword ptr [rax+TYPE]", "qword ptr [builtin+TY_I64*8]"
 jmp .v_same_result
.v_field:
 mov rax, [r12+A]
 C pointer, "qword ptr [rax+TYPE]"
 cmp qword ptr [rax+FLAGS], TY_STRUCT
 jne type_error
 C find, "qword ptr [rax+LIST]", "qword ptr [r12+B]"
 test rax, rax
 jz .field_error
 mov r15, rax
 mov rax, [r15+SIZE]
 mov [r12+D], rax
 C pointer_type, "qword ptr [r15+TYPE]"
 C same_type, r14, rax
 jmp .v_done
.v_load:
 C pointer_type, r14
 mov r15, rax
 mov rax, [r12+A]
 C same_type, "qword ptr [rax+TYPE]", r15
 jmp .v_done
.v_store:
 mov rax, [r12+B]
 C pointer_type, "qword ptr [rax+TYPE]"
 mov r15, rax
 mov rax, [r12+A]
 C same_type, "qword ptr [rax+TYPE]", r15
 jmp .v_done
.v_copy:
 mov rax, [r12+A]
 C pointer, "qword ptr [rax+TYPE]"
 mov rax, [r12+B]
 C pointer, "qword ptr [rax+TYPE]"
 mov rax, [r12+C_]
 C same_type, "qword ptr [rax+TYPE]", "qword ptr [builtin+TY_U64*8]"
 jmp .v_done
.v_size:
 C layout, "qword ptr [r12+A]"
 cmp rbx, OP_alignof
 cmove rax, rdx
 mov [r12+D], rax
 C same_type, r14, "qword ptr [builtin+TY_U64*8]"
 jmp .v_done
.v_address:
 C find, "qword ptr [data_nodes]", "qword ptr [r12+A]"
 test rax, rax
 jz .data_error
 mov [r12+A], rax
 C pointer_type, "qword ptr [builtin+TY_U8*8]"
 C same_type, r14, rax
 jmp .v_done
.v_jump:
 C verify_target, "qword ptr [r12+A]"
 jmp .v_done
.v_branch:
 mov rax, [r12+A]
 C same_type, "qword ptr [rax+TYPE]", "qword ptr [builtin+TY_BOOL*8]"
 C verify_target, "qword ptr [r12+B]"
 C verify_target, "qword ptr [r12+C_]"
 jmp .v_done
.v_return:
 mov r15, [builtin+TY_VOID*8]
 mov rax, [r12+A]
 test rax, rax
 jz .v_return_type
 mov r15, [rax+TYPE]
.v_return_type:
 mov rax, [cur_fn]
 C same_type, r15, "qword ptr [rax+TYPE]"
.v_done:
 mov rdi, [r12+TYPE]
 test rdi, rdi
 jz .v_end
 C add_register, rdi, "qword ptr [cur_block]"
.v_end:
 RETURN
.result_error:
 FAIL e_result, m_result
.cast_error:
 FAIL e_cast, m_cast
.function_error:
 FAIL e_function, m_function
.stack_error:
 FAIL e_stack, m_stack
.field_error:
 FAIL e_field, m_field
.data_error:
 FAIL e_data, m_data
STR m_cast, "cast requires numeric-to-numeric or pointer-to-pointer types"
STR m_function, "Unknown function"
STR m_stack, "stack is only legal in the entry block"
STR m_field, "Unknown struct field"
STR m_data, "Unknown static data"

FUNC verify
 mov qword ptr [diag_phase], offset p_verify
 cmp qword ptr [module_version], 1
 jne .version_error
 C unique_list, "qword ptr [functions]"
 C unique_list, "qword ptr [data_nodes]"
 mov r12, [data_nodes]
.verify_symbols:
 test r12, r12
 jz .verify_types_start
 C find, "qword ptr [functions]", "qword ptr [r12+NAME]"
 test rax, rax
 jnz .duplicate_error
 mov r12, [r12+NEXT]
 jmp .verify_symbols
.verify_types_start:
 mov r12, [types]
.verify_types:
 test r12, r12
 jz .verify_functions_start
 mov rax, [r12+TOKEN]
 mov [diag_tok], rax
 cmp qword ptr [r12+FLAGS], TY_STRUCT
 jne .verify_type_layout
 cmp qword ptr [r12+B], 0
 je type_error
 C unique_list, "qword ptr [r12+LIST]"
.verify_type_layout:
 cmp qword ptr [r12+FLAGS], TY_VOID
 je .verify_type_next
 C layout, r12
.verify_type_next:
 mov r12, [r12+NEXT]
 jmp .verify_types
.verify_functions_start:
 mov r12, [functions]
.verify_functions:
 test r12, r12
 jz .verify_end
 mov [cur_fn], r12
 mov rax, [r12+TOKEN]
 mov [diag_tok], rax
 mov rax, [r12+TYPE]
 cmp qword ptr [rax+FLAGS], TY_PTR
 ja .abi_error
 cmp qword ptr [r12+FLAGS], 0
 je .verify_params_start
 mov r13, [r12+NAME]
 EQ r13, s_main
 jz .abi_error
 C strncmp, r13, "offset reserved_prefix", 4
 test eax, eax
 jz .abi_error
.verify_params_start:
 mov r13, [r12+LIST]
.verify_params:
 test r13, r13
 jz .verify_blocks_start
 mov rax, [r13+TYPE]
 mov rax, [rax+FLAGS]
 test rax, rax
 jz .abi_error
 cmp rax, TY_PTR
 ja .abi_error
 C add_register, r13, 0
 mov r13, [r13+NEXT]
 jmp .verify_params
.verify_blocks_start:
 cmp qword ptr [r12+FLAGS], 1
 je .verify_function_next
 C unique_list, "qword ptr [r12+BODY]"
 mov r13, [r12+BODY]
 test r13, r13
 jz .empty_block_error
 cmp qword ptr [r13+LIST], 0
 jne .entry_error
.verify_blocks:
 mov [cur_block], r13
 mov rax, [r13+TOKEN]
 mov [diag_tok], rax
 mov r14, [r13+LIST]
.verify_block_params:
 test r14, r14
 jz .verify_ins_start
 C add_register, r14, r13
 mov r14, [r14+NEXT]
 jmp .verify_block_params
.verify_ins_start:
 mov r14, [r13+BODY]
 test r14, r14
 jz .terminator_error
.verify_instructions:
 mov rax, [r14+FLAGS]
 imul rax, 40
 mov rdx, [ops+rax+32]
 xor ecx, ecx
 cmp qword ptr [r14+NEXT], 0
 sete cl
 cmp rcx, rdx
 jne .terminator_error
 C verify_instruction, r14
 mov r14, [r14+NEXT]
 test r14, r14
 jnz .verify_instructions
 mov r13, [r13+NEXT]
 test r13, r13
 jnz .verify_blocks
.verify_function_next:
 mov qword ptr [cur_block], 0
 mov r12, [r12+NEXT]
 jmp .verify_functions
.verify_end:
 mov qword ptr [cur_fn], 0
 mov qword ptr [cur_ins], 0
 mov qword ptr [diag_tok], 0
 RETURN
.version_error:
 FAIL e_version, m_version
.abi_error:
 FAIL e_abi, m_abi
.empty_block_error:
 FAIL e_block, m_empty_block
.terminator_error:
 FAIL e_terminator, m_terminator
STR reserved_prefix, "lm0_"
STR m_version, "Unsupported language version"
STR m_empty_block, "Function must have an entry block"
