.text
FUNC asm_symbol
 mov r12, rdi
 cmp qword ptr [r12+FLAGS], 0
 jne .symbol_external
 lea rdi, [rbp-48]
 C asprintf, rdi, "offset symbol_format", "qword ptr [r12+ID]"
 cmp eax, 0
 jl .alloc_bad
 mov rax, [rbp-48]
 RETURN
.symbol_external:
 mov rax, [r12+NAME]
 RETURN
STR symbol_format, "lm0_fn_%lu"

FUNC asm_call_symbol
 mov r12,rdi
 cmp qword ptr [r12+FLAGS],0
 je .call_symbol_internal
 lea rdi,[rbp-48]
 C asprintf,rdi,"offset call_symbol_format","qword ptr [r12+NAME]"
 cmp eax,0
 jl .alloc_bad
 mov rax,[rbp-48]
 RETURN
.call_symbol_internal:
 C asm_symbol,r12
 RETURN
STR call_symbol_format, "\"%s\"@PLT"

# Values in compiler-assigned slots are sign/zero extended to 64 bits. Floating
# slots contain their exact IEEE representation; normalization follows results.
FUNC emit_normalize
 mov r12, rdi
 mov rax, [r12+FLAGS]
 cmp rax, TY_I8
 je .norm_i8
 cmp rax, TY_I16
 je .norm_i16
 cmp rax, TY_I32
 je .norm_i32
 cmp rax, TY_BOOL
 je .norm_u8
 cmp rax, TY_U8
 je .norm_u8
 cmp rax, TY_U16
 je .norm_u16
 cmp rax, TY_U32
 je .norm_u32
 cmp rax, TY_F32
 je .norm_u32
 RETURN
.norm_i8:
 EM a_norm_i8
 RETURN
.norm_i16:
 EM a_norm_i16
 RETURN
.norm_i32:
 EM a_norm_i32
 RETURN
.norm_u8:
 EM a_norm_u8
 RETURN
.norm_u16:
 EM a_norm_u16
 RETURN
.norm_u32:
 EM a_norm_u32
 RETURN
STR a_norm_i8, "movsx rax,al\n"
STR a_norm_i16, "movsx rax,ax\n"
STR a_norm_i32, "movsxd rax,eax\n"
STR a_norm_u8, "movzx eax,al\n"
STR a_norm_u16, "movzx eax,ax\n"
STR a_norm_u32, "mov eax,eax\n"

FUNC emit_load_a
 EM a_load_a, "qword ptr [rdi+SIZE]"
 RETURN
FUNC emit_load_b
 EM a_load_b, "qword ptr [rdi+SIZE]"
 RETURN
STR a_load_a, "mov rax,QWORD PTR [rbp-%lu]\n"
STR a_load_b, "mov rcx,QWORD PTR [rbp-%lu]\n"
STR a_store_result, "mov QWORD PTR [rbp-%lu],rax\n"

# Each edge stages every value before overwriting any block parameter slot.
FUNC emit_edge
 mov r12, rdi
 mov r13, [r12+LIST]
 mov rax, [cur_fn]
 mov r14, [rax+D]
 xor r15d, r15d
.edge_stage:
 test r13, r13
 jz .edge_copy_start
 C emit_load_a, "qword ptr [r13+TYPE]"
 lea rax, [r14+r15*8]
 EM a_store_result, rax
 inc r15
 mov r13, [r13+NEXT]
 jmp .edge_stage
.edge_copy_start:
 mov rax, [r12+TYPE]
 mov r13, [rax+LIST]
 xor r15d, r15d
.edge_copy:
 test r13, r13
 jz .edge_jump
 lea rax, [r14+r15*8]
 EM a_load_a, rax
 EM a_store_result, "qword ptr [r13+SIZE]"
 inc r15
 mov r13, [r13+NEXT]
 jmp .edge_copy
.edge_jump:
 mov rax, [r12+TYPE]
 EM a_jump, "qword ptr [rax+ID]"
 RETURN
STR a_jump, "jmp .Lblock%lu\n"

# Assign independent INTEGER and SSE argument registers. Excess arguments use
# eight-byte stack slots in source order. Both caller and callee use this walk.
FUNC abi_assign
 mov r12, rdi
 xor r13d, r13d
 xor r14d, r14d
 xor r15d, r15d
.abi_assign_loop:
 test r12, r12
 jz .abi_assign_end
 mov rax, [r12+TYPE]
 mov rax, [rax+FLAGS]
 cmp rax, TY_F32
 je .abi_assign_float
 cmp rax, TY_F64
 je .abi_assign_float
 cmp r13, 6
 jae .abi_assign_stack
 mov [r12+C_], r13
 inc r13
 jmp .abi_assign_next
.abi_assign_float:
 cmp r14, 8
 jae .abi_assign_stack
 lea rax, [r14+16]
 mov [r12+C_], rax
 inc r14
 jmp .abi_assign_next
.abi_assign_stack:
 lea rax, [r15+32]
 mov [r12+C_], rax
 inc r15
.abi_assign_next:
 mov r12, [r12+NEXT]
 jmp .abi_assign_loop
.abi_assign_end:
 mov rax, r15
 RETURN

FUNC emit_call
 mov r12, rdi
 mov r13, [r12+A]
 C abi_assign, "qword ptr [r13+LIST]"
 lea r14, [rax*8+15]
 and r14, -16
 EM a_sub_sp, r14
 mov r15, [r13+LIST]
 mov rbx, [r12+LIST]
.call_args:
 test r15, r15
 jz .call_emit
 C emit_load_a, "qword ptr [rbx+TYPE]"
 mov rax, [r15+C_]
 cmp rax, 32
 jae .call_stack
 cmp rax, 16
 jae .call_float
 EM a_to_gpr, "qword ptr [abi_regs+rax*8]"
 jmp .call_next
.call_float:
 sub rax, 16
 EM a_to_xmm, rax
 jmp .call_next
.call_stack:
 sub rax, 32
 shl rax, 3
 EM a_to_stack, rax
.call_next:
 mov r15, [r15+NEXT]
 mov rbx, [rbx+NEXT]
 jmp .call_args
.call_emit:
 C asm_call_symbol, r13
 EM a_call, rax
 EM a_add_sp, r14
 mov rax, [r13+TYPE]
 cmp qword ptr [rax+FLAGS], TY_F32
 je .call_f32_result
 cmp qword ptr [rax+FLAGS], TY_F64
 jne .call_end
 EM a_from_xmm0
 RETURN
.call_f32_result:
 EM a_from_xmm0_f32
.call_end:
 RETURN
STR a_sub_sp, "sub rsp,%lu\n"
STR a_add_sp, "add rsp,%lu\n"
STR a_to_gpr, "mov %s,rax\n"
STR a_to_xmm, "movq xmm%lu,rax\n"
STR a_to_stack, "mov QWORD PTR [rsp+%lu],rax\n"
STR a_call, "call %s\n"
STR a_from_xmm0, "movq rax,xmm0\n"
STR a_from_xmm0_f32, "movd eax,xmm0\n"
STR abi_rdi, "rdi"
STR abi_rsi, "rsi"
STR abi_rdx, "rdx"
STR abi_rcx, "rcx"
STR abi_r8, "r8"
STR abi_r9, "r9"
.section .rodata
abi_regs: .quad abi_rdi,abi_rsi,abi_rdx,abi_rcx,abi_r8,abi_r9
.text

FUNC emit_cast
 mov r12, rdi
 mov rax, [r12+A]
 mov r13, [rax+TYPE]
 mov rax, [r12+TYPE]
 mov r14, [rax+TYPE]
 mov r15, [r13+FLAGS]
 mov rbx, [r14+FLAGS]
 cmp r15, TY_PTR
 je .cast_emit_end
 cmp r15, TY_F32
 jae .cast_from_float
 cmp rbx, TY_F32
 jb .cast_emit_end
 # x87's 64-bit significand preserves every u64 before rounding directly to
 # binary32/binary64, avoiding an intermediate binary64 double rounding.
 EM a_int_float_start
 cmp r15, TY_U64
 jne .cast_int_float_store
 EM a_uint_float, "qword ptr [r12+ID]", "qword ptr [r12+ID]"
.cast_int_float_store:
 cmp rbx, TY_F32
 jne .cast_int_f64
 EM a_int_f32
 RETURN
.cast_int_f64:
 EM a_int_f64
 RETURN
.cast_from_float:
 EM a_to_xmm0
 cmp r15, TY_F32
 jne .cast_double_ready
 EM a_widen
.cast_double_ready:
 cmp rbx, TY_F32
 je .cast_to_f32
 cmp rbx, TY_F64
 je .cast_to_f64
 EM a_cast_nan, "qword ptr [r12+ID]"
 cmp rbx, TY_U64
 je .cast_to_u64
 EM a_cast_i64, "qword ptr [r12+ID]", "qword ptr [r12+ID]"
 C layout, r14
 mov r15, rax
 cmp rbx, TY_U8
 jae .cast_small_unsigned
 cmp rbx, TY_I64
 je .cast_emit_end
 lea rcx, [r15*8-1]
 mov rax, 1
 shl rax, cl
 mov r15, rax
 neg r15
 EM a_cast_lower, r15, "qword ptr [r12+ID]"
 dec rax
 # fprintf clobbers rax; recompute the signed upper bound from the lower.
 mov rax, r15
 neg rax
 dec rax
 EM a_cast_upper, rax, "qword ptr [r12+ID]"
 RETURN
.cast_small_unsigned:
 EM a_cast_lower, 0, "qword ptr [r12+ID]"
 lea rcx, [r15*8]
 mov rax, 1
 shl rax, cl
 dec rax
 EM a_cast_upper, rax, "qword ptr [r12+ID]"
 RETURN
.cast_to_u64:
 EM a_cast_u64, "qword ptr [r12+ID]", "qword ptr [r12+ID]", "qword ptr [r12+ID]", "qword ptr [r12+ID]"
 RETURN
.cast_to_f32:
 EM a_narrow
 RETURN
.cast_to_f64:
 EM a_from_xmm0
.cast_emit_end:
 RETURN
STR a_to_xmm0, "movq xmm0,rax\n"
STR a_widen, "cvtss2sd xmm0,xmm0\n"
STR a_narrow, "cvtsd2ss xmm0,xmm0\nmovd eax,xmm0\n"
STR a_int_float_start, "sub rsp,16\nmov QWORD PTR [rsp],rax\nfild QWORD PTR [rsp]\n"
STR a_uint_float, "test rax,rax\njns .Luintfloat%lu\nfadd QWORD PTR [rip+.Ltwo64]\n.Luintfloat%lu:\n"
STR a_int_f32, "fstp DWORD PTR [rsp]\nmov eax,DWORD PTR [rsp]\nadd rsp,16\n"
STR a_int_f64, "fstp QWORD PTR [rsp]\nmov rax,QWORD PTR [rsp]\nadd rsp,16\n"
STR a_cast_nan, "ucomisd xmm0,xmm0\njp .Ltrap%lu\n"
STR a_cast_i64, "comisd xmm0,QWORD PTR [rip+.Lminus63]\njb .Ltrap%lu\ncomisd xmm0,QWORD PTR [rip+.Ltwo63]\njae .Ltrap%lu\ncvttsd2si rax,xmm0\n"
STR a_cast_lower, "movabs rcx,%ld\ncmp rax,rcx\njl .Ltrap%lu\n"
STR a_cast_upper, "movabs rcx,%lu\ncmp rax,rcx\njg .Ltrap%lu\n"
STR a_cast_u64, "comisd xmm0,QWORD PTR [rip+.Lminusone]\njbe .Ltrap%lu\ncomisd xmm0,QWORD PTR [rip+.Ltwo64]\njae .Ltrap%lu\ncomisd xmm0,QWORD PTR [rip+.Ltwo63]\njb .Lcastsmall%lu\nsubsd xmm0,QWORD PTR [rip+.Ltwo63]\ncvttsd2si rax,xmm0\nbtc rax,63\njmp .Lcastdone%lu\n"

FUNC emit_instruction
 mov r12, rdi
 mov r13, [r12+AUX]
.emit_literal_loop:
 test r13, r13
 jz .emit_literals_done
 C emit_instruction, r13
 mov r13, [r13+NEXT]
 jmp .emit_literal_loop
.emit_literals_done:
 mov [cur_ins], r12
 mov r13, [r12+FLAGS]
 mov r14, [builtin+TY_VOID*8]
 mov rax, [r12+TYPE]
 test rax, rax
 jz .emit_ins_location
 mov r14, [rax+TYPE]
.emit_ins_location:
 mov rax, [r12+TOKEN]
 EM a_location, "qword ptr [rax+TL]", "qword ptr [rax+TC]"
 cmp r13, OP_const
 je .emit_const
 cmp r13, OP_null
 je .emit_null
 cmp r13, OP_add
 jb .emit_ins_dispatch
 cmp r13, OP_inttoptr
 ja .emit_ins_dispatch
 C emit_load_a, "qword ptr [r12+A]"
 cmp r13, OP_ge
 ja .emit_ins_dispatch
 C emit_load_b, "qword ptr [r12+B]"
.emit_ins_dispatch:
 cmp r13, OP_ge
 jbe .emit_binary
 cmp r13, OP_neg
 je .emit_neg
 cmp r13, OP_not
 je .emit_not
 cmp r13, OP_cast
 je .emit_cast_op
 cmp r13, OP_inttoptr
 jbe .emit_result
 cmp r13, OP_call
 je .emit_call_op
 cmp r13, OP_stack
 je .emit_stack
 cmp r13, OP_alloc
 je .emit_alloc
 cmp r13, OP_free
 je .emit_free
 cmp r13, OP_offset
 je .emit_offset
 cmp r13, OP_field
 je .emit_field
 cmp r13, OP_load
 je .emit_load
 cmp r13, OP_store
 je .emit_store
 cmp r13, OP_move
 jbe .emit_copy
 cmp r13, OP_alignof
 jbe .emit_const
 cmp r13, OP_address
 je .emit_address
 cmp r13, OP_jump
 je .emit_jump
 cmp r13, OP_branch
 je .emit_branch
 cmp r13, OP_return
 je .emit_return
 EM a_trap_jump, "qword ptr [r12+ID]"
 jmp .emit_ins_end
.emit_const:
 EM a_constant, "qword ptr [r12+D]"
 jmp .emit_result
.emit_null:
 EM a_zero
 jmp .emit_result
.emit_binary:
 mov rax, [r12+A]
 mov r15, [rax+TYPE]
 mov rbx, [r15+FLAGS]
 cmp rbx, TY_F32
 je .emit_float_binary
 cmp rbx, TY_F64
 je .emit_float_binary
 cmp r13, OP_eq
 jae .emit_compare
 cmp r13, OP_div
 je .emit_div
 cmp r13, OP_rem
 je .emit_div
 cmp r13, OP_shl
 jae .emit_shift
 mov rax, [integer_ops+r13*8]
 EM a_binary, rax
 jmp .emit_result
.emit_div:
 EM a_div_zero, "qword ptr [r12+ID]"
 cmp rbx, TY_U8
 jae .emit_unsigned_div
 C layout, r15
 lea rcx, [rax*8-1]
 mov rax, -1
 shl rax, cl
 EM a_div_overflow, rax, "qword ptr [r12+ID]", "qword ptr [r12+ID]", "qword ptr [r12+ID]"
 EM a_signed_div
 jmp .emit_remainder
.emit_unsigned_div:
 EM a_unsigned_div
.emit_remainder:
 cmp r13, OP_rem
 jne .emit_result
 EM a_remainder
 jmp .emit_result
.emit_shift:
 C layout, r15
 shl rax, 3
 EM a_shift_check, rax, "qword ptr [r12+ID]"
 mov rax, offset op_shl_text
 cmp r13, OP_shl
 je .emit_shift_text
 mov rax, offset op_shr_text
 cmp rbx, TY_U8
 jae .emit_shift_text
 mov rax, offset op_sar_text
.emit_shift_text:
 EM a_shift, rax
 jmp .emit_result
.emit_compare:
 lea rax, [r13-OP_eq]
 lea rdx, [signed_conditions]
 cmp rbx, TY_U8
 jb .emit_cmp_text
 lea rdx, [unsigned_conditions]
.emit_cmp_text:
 mov rax, [rdx+rax*8]
 EM a_compare, rax
 jmp .emit_result
.emit_float_binary:
 EM a_float_inputs
 cmp r13, OP_eq
 jae .emit_float_compare
 lea rax, [r13-OP_add]
 mov rdx, [float_ops+rax*8]
 mov rax, offset suffix_sd
 cmp rbx, TY_F32
 jne .emit_float_arith
 mov rax, offset suffix_ss
.emit_float_arith:
 EM a_float_binary, rdx, rax
 EM a_from_xmm0
 jmp .emit_result
.emit_float_compare:
 mov rax, offset suffix_sd
 cmp rbx, TY_F32
 jne .emit_float_cmp_type
 mov rax, offset suffix_ss
.emit_float_cmp_type:
 EM a_float_compare, rax
 lea rax, [r13-OP_eq]
 EM a_float_condition, "qword ptr [unsigned_conditions+rax*8]"
 cmp r13, OP_ne
 je .emit_float_ne
 EM a_float_ordered
 jmp .emit_result
.emit_float_ne:
 EM a_float_unordered
 jmp .emit_result
.emit_neg:
 cmp qword ptr [r14+FLAGS], TY_F32
 je .emit_neg_f32
 cmp qword ptr [r14+FLAGS], TY_F64
 je .emit_neg_f64
 EM a_neg
 jmp .emit_result
.emit_neg_f32:
 EM a_neg_f32
 jmp .emit_result
.emit_neg_f64:
 EM a_neg_f64
 jmp .emit_result
.emit_not:
 cmp qword ptr [r14+FLAGS], TY_BOOL
 je .emit_not_bool
 EM a_not
 jmp .emit_result
.emit_not_bool:
 EM a_not_bool
 jmp .emit_result
.emit_cast_op:
 C emit_cast, r12
 # u64 casts have a second branch that must rejoin before result storage.
 cmp qword ptr [r14+FLAGS], TY_U64
 jne .emit_result
 mov rax, [r12+A]
 mov rax, [rax+TYPE]
 cmp qword ptr [rax+FLAGS], TY_F32
 jb .emit_result
 EM a_cast_u64_end, "qword ptr [r12+ID]", "qword ptr [r12+ID]"
 jmp .emit_result
.emit_call_op:
 C emit_call, r12
 jmp .emit_result
.emit_stack:
 EM a_stack, "qword ptr [r12+SIZE]"
 jmp .emit_result
.emit_alloc:
 C emit_load_a, "qword ptr [r12+B]"
 mov rax, [r12+A]
 EM a_allocate, "qword ptr [rax+SIZE]", "qword ptr [r12+ID]"
 jmp .emit_result
.emit_free:
 C emit_load_a, "qword ptr [r12+A]"
 EM a_free
 jmp .emit_ins_end
.emit_offset:
 C emit_load_a, "qword ptr [r12+A]"
 C emit_load_b, "qword ptr [r12+B]"
 EM a_offset, "qword ptr [r12+D]", "qword ptr [r12+ID]"
 jmp .emit_result
.emit_field:
 C emit_load_a, "qword ptr [r12+A]"
 EM a_field, "qword ptr [r12+D]"
 jmp .emit_result
.emit_load:
 C emit_load_a, "qword ptr [r12+A]"
 mov rax, [r14+FLAGS]
 movzx eax, byte ptr [scalar_sizes+rax]
 EM a_memory_load, "qword ptr [memory_loads+rax*8]"
 jmp .emit_result
.emit_store:
 C emit_load_a, "qword ptr [r12+A]"
 C emit_load_b, "qword ptr [r12+B]"
 mov rax, [r12+B]
 mov rax, [rax+TYPE]
 mov rax, [rax+FLAGS]
 movzx eax, byte ptr [scalar_sizes+rax]
 EM a_memory_store, "qword ptr [memory_stores+rax*8]"
 jmp .emit_ins_end
.emit_copy:
 C emit_load_a, "qword ptr [r12+A]"
 C emit_load_b, "qword ptr [r12+B]"
 mov rax, [r12+C_]
 mov r15, [rax+SIZE]
 mov rax, offset lib_memcpy
 cmp r13, OP_move
 jne .emit_copy_text
 mov rax, offset lib_memmove
.emit_copy_text:
 EM a_copy, r15, "qword ptr [r12+ID]", rax, "qword ptr [r12+ID]"
 jmp .emit_ins_end
.emit_address:
 mov rax, [r12+A]
 EM a_address, "qword ptr [rax+ID]"
 jmp .emit_result
.emit_jump:
 C emit_edge, "qword ptr [r12+A]"
 jmp .emit_ins_end
.emit_branch:
 C emit_load_a, "qword ptr [r12+A]"
 EM a_branch, "qword ptr [r12+ID]"
 C emit_edge, "qword ptr [r12+B]"
 EM a_else, "qword ptr [r12+ID]"
 C emit_edge, "qword ptr [r12+C_]"
 jmp .emit_ins_end
.emit_return:
 cmp qword ptr [r12+A], 0
 je .emit_ret_text
 C emit_load_a, "qword ptr [r12+A]"
 mov rax, [cur_fn]
 mov rax, [rax+TYPE]
 cmp qword ptr [rax+FLAGS], TY_F32
 je .emit_ret_float
 cmp qword ptr [rax+FLAGS], TY_F64
 jne .emit_ret_text
.emit_ret_float:
 EM a_to_xmm0
.emit_ret_text:
 EM a_return
 jmp .emit_ins_end
.emit_result:
 cmp qword ptr [r12+TYPE], 0
 je .emit_ins_end
 C emit_normalize, r14
 mov rax, [r12+TYPE]
 EM a_store_result, "qword ptr [rax+SIZE]"
.emit_ins_end:
 RETURN

STR a_location, ".loc 1 %lu %lu\n"
STR a_constant, "movabs rax,%lu\n"
STR a_zero, "xor eax,eax\n"
STR a_binary, "%s rax,rcx\n"
STR a_div_zero, "test rcx,rcx\njz .Ltrap%lu\n"
STR a_div_overflow, "movabs rdx,%ld\ncmp rax,rdx\njne .Ldivok%lu\ncmp rcx,-1\nje .Ltrap%lu\n.Ldivok%lu:\n"
STR a_signed_div, "cqo\nidiv rcx\n"
STR a_unsigned_div, "xor edx,edx\ndiv rcx\n"
STR a_remainder, "mov rax,rdx\n"
STR a_shift_check, "cmp rcx,%lu\njae .Ltrap%lu\n"
STR a_shift, "%s rax,cl\n"
STR a_compare, "cmp rax,rcx\nset%s al\nmovzx eax,al\n"
STR a_float_inputs, "movq xmm0,rax\nmovq xmm1,rcx\n"
STR a_float_binary, "%s%s xmm0,xmm1\n"
STR a_float_compare, "ucomi%s xmm0,xmm1\n"
STR a_float_condition, "set%s al\n"
STR a_float_ordered, "setnp cl\nand al,cl\nmovzx eax,al\n"
STR a_float_unordered, "setp cl\nor al,cl\nmovzx eax,al\n"
STR a_neg, "neg rax\n"
STR a_not, "not rax\n"
STR a_not_bool, "xor eax,1\n"
STR a_neg_f32, "btc eax,31\n"
STR a_neg_f64, "btc rax,63\n"
STR a_cast_u64_end, ".Lcastsmall%lu:\ncvttsd2si rax,xmm0\n.Lcastdone%lu:\n"
STR a_stack, "lea rax,[rbp-%lu]\n"
STR a_allocate, "mov rdi,rax\nmov rsi,%lu\nlea rdx,[rip+.Lerror%lu]\ncall .Llm0_alloc\n"
STR a_free, "mov rdi,rax\ncall free@PLT\n"
STR a_offset, "mov rdi,rax\nmov rsi,rcx\nmov rdx,%lu\nlea rcx,[rip+.Lerror%lu]\ncall .Llm0_offset\n"
STR a_field, "add rax,%lu\n"
STR a_memory_load, "%s\n"
STR a_memory_store, "%s\n"
STR a_copy, "mov rdx,QWORD PTR [rbp-%lu]\ntest rdx,rdx\njz .Lcopydone%lu\nmov rdi,rax\nmov rsi,rcx\ncall %s@PLT\n.Lcopydone%lu:\n"
STR a_address, "lea rax,[rip+.Ldata%lu]\n"
STR a_branch, "test al,al\njz .Lelse%lu\n"
STR a_else, ".Lelse%lu:\n"
STR a_return, "leave\n.cfi_def_cfa rsp,8\nret\n.cfi_def_cfa rbp,16\n"
STR a_trap_jump, "jmp .Ltrap%lu\n"
STR op_add_text, "add"
STR op_sub_text, "sub"
STR op_mul_text, "imul"
STR op_and_text, "and"
STR op_or_text, "or"
STR op_xor_text, "xor"
STR op_shl_text, "shl"
STR op_shr_text, "shr"
STR op_sar_text, "sar"
STR float_mul_text, "mul"
STR float_div_text, "div"
STR suffix_ss, "ss"
STR suffix_sd, "sd"
STR cond_e, "e"
STR cond_ne, "ne"
STR cond_l, "l"
STR cond_le, "le"
STR cond_g, "g"
STR cond_ge, "ge"
STR cond_b, "b"
STR cond_be, "be"
STR cond_a, "a"
STR cond_ae, "ae"
STR load1, "movzx eax,BYTE PTR [rax]"
STR load2, "movzx eax,WORD PTR [rax]"
STR load4, "mov eax,DWORD PTR [rax]"
STR load8, "mov rax,QWORD PTR [rax]"
STR store1, "mov BYTE PTR [rax],cl"
STR store2, "mov WORD PTR [rax],cx"
STR store4, "mov DWORD PTR [rax],ecx"
STR store8, "mov QWORD PTR [rax],rcx"
STR lib_memcpy, "memcpy"
STR lib_memmove, "memmove"
.section .rodata
integer_ops: .quad 0,0,op_add_text,op_sub_text,op_mul_text,0,0,op_and_text,op_or_text,op_xor_text
float_ops: .quad op_add_text,op_sub_text,float_mul_text,float_div_text
signed_conditions: .quad cond_e,cond_ne,cond_l,cond_le,cond_g,cond_ge
unsigned_conditions: .quad cond_e,cond_ne,cond_b,cond_be,cond_a,cond_ae
memory_loads: .quad 0,load1,load2,0,load4,0,0,0,load8
memory_stores: .quad 0,store1,store2,0,store4,0,0,0,store8
.text

FUNC emit_function
 mov r12, rdi
 mov [cur_fn], r12
 C abi_assign, "qword ptr [r12+LIST]"
 # Reserve stack objects after register slots, then a reusable edge buffer.
 mov r13, [r12+BODY]
 mov r14, [r12+SIZE]
 xor r15d, r15d
.frame_block:
 test r13, r13
 jz .frame_end
 mov rax, [r13+LIST]
 xor ecx, ecx
.frame_params:
 test rax, rax
 jz .frame_objects
 inc rcx
 mov rax, [rax+NEXT]
 jmp .frame_params
.frame_objects:
 cmp r15, rcx
 cmovb r15, rcx
 mov rbx, [r13+BODY]
.frame_ins:
 test rbx, rbx
 jz .frame_next
 cmp qword ptr [rbx+FLAGS], OP_stack
 jne .frame_ins_next
 add r14, [rbx+D]
 jc layout_error
 add r14, 15
 and r14, -16
 mov [rbx+SIZE], r14
.frame_ins_next:
 mov rbx, [rbx+NEXT]
 jmp .frame_ins
.frame_next:
 mov r13, [r13+NEXT]
 jmp .frame_block
.frame_end:
 lea rax, [r14+8]
 mov [r12+D], rax
 lea r14, [r14+r15*8+15]
 and r14, -16
 cmp r14, 2147483000
 ja layout_error
 C asm_symbol, r12
 mov r13, rax
 cmp qword ptr [r12+FLAGS], 2
 jne .emit_function_private
 EM a_global, r13
.emit_function_private:
 EM a_function, r13, r13
 # Probe large frames a page at a time, avoiding jumps over stack guard pages.
 EM a_frame, r14, "qword ptr [r12+ID]", "qword ptr [r12+ID]", "qword ptr [r12+ID]"
 EM a_frame_end, "qword ptr [r12+ID]"
 mov rbx, [r12+LIST]
.emit_incoming:
 test rbx, rbx
 jz .emit_blocks_start
 mov rax, [rbx+C_]
 cmp rax, 32
 jae .incoming_stack
 cmp rax, 16
 jae .incoming_float
 EM a_from_gpr, "qword ptr [abi_regs+rax*8]"
 jmp .incoming_save
.incoming_float:
 sub rax, 16
 EM a_from_xmm, rax
 jmp .incoming_save
.incoming_stack:
 sub rax, 32
 lea rax, [rax*8+16]
 EM a_from_stack, rax
.incoming_save:
 C emit_normalize, "qword ptr [rbx+TYPE]"
 EM a_store_result, "qword ptr [rbx+SIZE]"
 mov rbx, [rbx+NEXT]
 jmp .emit_incoming
.emit_blocks_start:
 mov r14, [r12+BODY]
.emit_blocks:
 test r14, r14
 jz .emit_function_end
 mov [cur_block], r14
 EM a_block, "qword ptr [r14+ID]"
 mov r15, [r14+BODY]
.emit_function_ins:
 test r15, r15
 jz .emit_function_next_block
 C emit_instruction, r15
 mov r15, [r15+NEXT]
 jmp .emit_function_ins
.emit_function_next_block:
 mov r14, [r14+NEXT]
 jmp .emit_blocks
.emit_function_end:
 # Trap thunks are within the function's unwind frame and refer to source JSON.
 mov r14, [r12+BODY]
.emit_thunk_block:
 test r14, r14
 jz .emit_thunk_end
 mov r15, [r14+BODY]
.emit_thunks:
 test r15, r15
 jz .emit_thunk_next
 EM a_trap_thunk, "qword ptr [r15+ID]", "qword ptr [r15+ID]"
 mov r15, [r15+NEXT]
 jmp .emit_thunks
.emit_thunk_next:
 mov r14, [r14+NEXT]
 jmp .emit_thunk_block
.emit_thunk_end:
 EM a_function_end, r13, r13
 RETURN
STR a_global, ".globl \"%s\"\n"
STR a_function, ".text\n.type \"%s\",@function\n\"%s\":\n.cfi_startproc\npush rbp\n.cfi_def_cfa_offset 16\n.cfi_offset rbp,-16\nmov rbp,rsp\n.cfi_def_cfa_register rbp\n"
STR a_frame, "mov r11,%lu\n.Lprobe%lu:\ncmp r11,4096\njb .Lprobed%lu\nsub rsp,4096\nor BYTE PTR [rsp],0\nsub r11,4096\njmp .Lprobe%lu\n"
STR a_frame_end, ".Lprobed%lu:\nsub rsp,r11\n"
STR a_from_gpr, "mov rax,%s\n"
STR a_from_xmm, "movq rax,xmm%lu\n"
STR a_from_stack, "mov rax,QWORD PTR [rbp+%lu]\n"
STR a_block, ".Lblock%lu:\n"
STR a_trap_thunk, ".Ltrap%lu:\nlea rdi,[rip+.Lerror%lu]\ncall .Llm0_trap\n"
STR a_function_end, ".cfi_endproc\n.size \"%s\",.-\"%s\"\n"

# Emit arbitrary bytes as assembler byte directives; neither filenames nor
# runtime JSON are interpolated as assembly syntax.
FUNC asm_bytes
 mov r12, rdi
 mov r13, rsi
 xor r14d, r14d
.bytes_loop:
 cmp r14, r13
 jae .bytes_done
 movzx eax, byte ptr [r12+r14]
 EM a_byte, rax
 inc r14
 jmp .bytes_loop
.bytes_done:
 RETURN
STR a_byte, ".byte %u\n"

FUNC emit_error_data
 mov r12, rdi
 mov r13, [r12+FLAGS]
 mov r14, offset rt_trap_code
 cmp r13, OP_div
 je .error_div
 cmp r13, OP_rem
 je .error_div
 cmp r13, OP_shl
 je .error_shift
 cmp r13, OP_shr
 je .error_shift
 cmp r13, OP_cast
 je .error_cast
 cmp r13, OP_alloc
 je .error_alloc
 cmp r13, OP_offset
 je .error_offset
 jmp .error_selected
.error_div:
 mov r14, offset rt_div_code
 jmp .error_selected
.error_shift:
 mov r14, offset rt_shift_code
 jmp .error_selected
.error_cast:
 mov r14, offset e_cast
 jmp .error_selected
.error_alloc:
 mov r14, offset rt_alloc_code
 jmp .error_selected
.error_offset:
 mov r14, offset rt_offset_code
.error_selected:
 mov qword ptr [rbp-48], 0
 mov qword ptr [rbp-56], 0
 lea rdi, [rbp-48]
 lea rsi, [rbp-56]
 call open_memstream
 test rax, rax
 jz .alloc_bad
 mov r15, rax
 mov [jout], rax
 C text_out, "offset j_start"
 C json_string, r14
 C text_out, "offset j_phase"
 C json_string, "offset rt_phase"
 C text_out, "offset j_message"
 C json_string, "offset rt_message"
 C text_out, "offset j_function"
 mov rax, [cur_fn]
 C json_string, "qword ptr [rax+NAME]"
 C text_out, "offset j_block"
 mov rax, [cur_block]
 C json_string, "qword ptr [rax+NAME]"
 C text_out, "offset j_span"
 C json_string, "qword ptr [filename]"
 mov rbx, [r12+TOKEN]
 C fprintf, r15, "offset j_location", "qword ptr [rbx+TL]", "qword ptr [rbx+TC]", "qword ptr [rbx+TEL]", "qword ptr [rbx+TEC]"
 C text_out, "offset j_end"
 C fclose, r15
 EM a_error_label, "qword ptr [r12+ID]"
 C asm_bytes, "qword ptr [rbp-48]", "qword ptr [rbp-56]"
 EM a_byte, 0
 C free, "qword ptr [rbp-48]"
 mov rax, [stdout]
 mov [jout], rax
 RETURN
STR rt_trap_code, "E_TRAP"
STR rt_div_code, "E_DIVISION"
STR rt_shift_code, "E_SHIFT"
STR rt_alloc_code, "E_ALLOC"
STR rt_offset_code, "E_OFFSET"
STR rt_phase, "runtime"
STR rt_message, "LM0 runtime check failed"
STR a_error_label, ".section .rodata\n.Lerror%lu:\n"

FUNC emit_asm
 mov r12, rdi
 mov r13, rsi
 mov [asm_file], r12
 EM a_header
 # GAS quoted strings use the same basic escaping as JSON for valid filenames.
 mov [jout], r12
 C text_out, "offset a_file"
 C json_string, "qword ptr [filename]"
 C text_out, "offset s_nl"
 mov rax, [stdout]
 mov [jout], rax
 C fwrite, "offset runtime_text", 1, runtime_text_end-runtime_text, r12
 mov r14, [data_nodes]
.emit_data:
 test r14, r14
 jz .emit_all_functions
 EM a_data_label, "qword ptr [r14+ID]"
 mov rsi, [r14+SIZE]
 test rsi, rsi
 jnz .emit_data_bytes
 inc rsi
.emit_data_bytes:
 C asm_bytes, "qword ptr [r14+A]", rsi
 mov r14, [r14+NEXT]
 jmp .emit_data
.emit_all_functions:
 mov r14, [functions]
.emit_fn_loop:
 test r14, r14
 jz .emit_entry
 cmp qword ptr [r14+FLAGS], 1
 je .emit_fn_next
 C emit_function, r14
 mov [cur_fn], r14
 mov r15, [r14+BODY]
.emit_errors_block:
 test r15, r15
 jz .emit_fn_next
 mov [cur_block], r15
 mov rbx, [r15+BODY]
.emit_errors:
 test rbx, rbx
 jz .emit_errors_next
 C emit_error_data, rbx
 mov rbx, [rbx+NEXT]
 jmp .emit_errors
.emit_errors_next:
 mov r15, [r15+NEXT]
 jmp .emit_errors_block
.emit_fn_next:
 mov r14, [r14+NEXT]
 jmp .emit_fn_loop
.emit_entry:
 test r13, r13
 jz .emit_asm_end
 C find, "qword ptr [functions]", "offset s_main"
 test rax, rax
 jz .entry_error
 cmp qword ptr [rax+FLAGS], 0
 jne .entry_error
 mov rcx,[rax+LIST]
 test rcx,rcx
 jz .entry_result
 cmp qword ptr [module_version],2
 jne .entry_error
 mov rdx,[builtin+TY_I32*8]
 cmp [rcx+TYPE],rdx
 jne .entry_error
 mov rcx,[rcx+NEXT]
 test rcx,rcx
 jz .entry_error
 cmp qword ptr [rcx+NEXT],0
 jne .entry_error
 mov rdx,[rcx+TYPE]
 cmp qword ptr [rdx+FLAGS],TY_PTR
 jne .entry_error
 mov rdx,[rdx+TYPE]
 cmp qword ptr [rdx+FLAGS],TY_PTR
 jne .entry_error
 mov rdx,[rdx+TYPE]
 cmp rdx,[builtin+TY_U8*8]
 jne .entry_error
.entry_result:
 mov rdx, [builtin+TY_I32*8]
 cmp [rax+TYPE], rdx
 jne .entry_error
 C asm_symbol, rax
 EM a_main_wrapper, rax
.emit_asm_end:
 cmp qword ptr [library_used],0
 je .emit_no_library
 EM library_abi_reference,"offset library_identity"
.emit_no_library:
 EM a_note
 mov qword ptr [cur_fn], 0
 mov qword ptr [cur_block], 0
 mov qword ptr [cur_ins], 0
 C ferror, r12
 test eax, eax
 jnz .read_bad
 RETURN
STR a_header, "# Generated by the native LM0 assembly compiler\n.intel_syntax noprefix\n"
STR a_file, ".file 1 "
STR a_data_label, ".data\n.Ldata%lu:\n"
STR a_main_wrapper, ".text\n.globl main\n.type main,@function\nmain:\njmp %s\n.size main,.-main\n"
STR a_note, ".section .note.GNU-stack,\"\",@progbits\n"
.section .rodata
runtime_text:
.incbin "native/runtime.asm"
runtime_text_end:
.text
