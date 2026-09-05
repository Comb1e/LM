.text
inference_error:
 FAIL e_infer, m_infer
STR e_infer, "E_INFER"
STR m_infer, "Type is ambiguous; annotate the destination or literal"
STR literal_name, ".literal%lu"

# Inspect an operand without choosing a numeric default.
FUNC operand_type
 mov r12, rdi
 test r12, r12
 jz .operand_unknown
 cmp qword ptr [r12+FLAGS], 0
 jne .operand_type_literal
 C resolve_reg, "qword ptr [r12+NAME]"
 mov rax, [rax+TYPE]
 RETURN
.operand_type_literal:
 mov rax, [r12+TYPE]
 test rax, rax
 jnz .operand_type_end
 EQ "qword ptr [r12+NAME]", s_true
 jz .operand_boolean
 EQ "qword ptr [r12+NAME]", s_false
 jnz .operand_unknown
.operand_boolean:
 mov rax, [builtin+TY_BOOL*8]
.operand_type_end:
 RETURN
.operand_unknown:
 xor eax, eax
 RETURN

# Materialize a literal as an internal const/null, retaining its source span.
# The caller's AUX list is emitted immediately before the owning instruction.
FUNC normalize_operand
 mov r12, rdi
 mov r13, rsi
 mov r14, [cur_ins]
 C operand_type, r12
 test rax, rax
 jz .norm_operand_context
 test r13, r13
 jz .norm_operand_known
 C same_type, rax, r13
 jmp .norm_operand_context
.norm_operand_known:
 mov r13, rax
.norm_operand_context:
 test r13, r13
 jz inference_error
 cmp qword ptr [r12+FLAGS], 0
 je .norm_operand_name
 C scalar, r13
 call node
 mov r15, rax
 mov qword ptr [r15+ALIGN], 1
 mov rax, [r12+TOKEN]
 mov [r15+TOKEN], rax
 mov rax, [r12+END]
 mov [r15+END], rax
 mov rax, [r12+NAME]
 mov [r15+A], rax
 mov qword ptr [r15+NAME], offset op_name_0
 EQ rax, op_name_1
 jnz .norm_literal_dest
 mov qword ptr [r15+FLAGS], OP_null
 mov qword ptr [r15+NAME], offset op_name_1
.norm_literal_dest:
 call node
 mov rbx, rax
 mov [r15+TYPE], rax
 mov [rbx+TYPE], r13
 mov [rbx+B], r15
 mov rax, [r12+TOKEN]
 mov [rbx+TOKEN], rax
 lea rdi, [rbp-48]
 C asprintf, rdi, "offset literal_name", "qword ptr [rbx+ID]"
 test eax, eax
 js .alloc_bad
 mov rax, [rbp-48]
 mov [rbx+NAME], rax
 C verify_instruction, r15
 lea rdi, [r14+AUX]
 C append, rdi, r15
 mov rax, [rbx+NAME]
 mov [r12+NAME], rax
 mov [cur_ins], r14
.norm_operand_name:
 mov rax, [r12+NAME]
 RETURN

FUNC normalize_args
 mov r12, rdi
 mov r13, rsi
.norm_args_loop:
 test r12, r12
 jz .norm_args_end
 test r13, r13
 jz .arity_error
 C normalize_operand, r12, "qword ptr [r13+TYPE]"
 mov r12, [r12+NEXT]
 mov r13, [r13+NEXT]
 jmp .norm_args_loop
.norm_args_end:
 test r13, r13
 jnz .arity_error
 RETURN

FUNC normalize_target
 mov r12, rdi
 mov rax, [cur_fn]
 C find, "qword ptr [rax+BODY]", "qword ptr [r12+NAME]"
 test rax, rax
 jz .target_missing
 C normalize_args, "qword ptr [r12+LIST]", "qword ptr [rax+LIST]"
 RETURN

FUNC normalize_instruction
 mov r12, rdi
 mov qword ptr [r12+ALIGN], 1
 mov rax, [r12+FLAGS]
 imul rax, 40
 mov rbx, [ops+rax+24]
 cmp rbx,R_CALL
 je .norm_result_presence_ok
 xor edx,edx
 cmp qword ptr [r12+TYPE],0
 setne dl
 cmp rdx,[ops+rax+16]
 jne .result_error
.norm_result_presence_ok:
 xor r14d, r14d
 mov rax, [r12+TYPE]
 test rax, rax
 jz .norm_dispatch
 mov r14, [rax+TYPE]
.norm_dispatch:
 lea rax, [normalizers]
 sub rsp, 8
 call [rax+rbx*8]
 add rsp, 8
 mov rax, [r12+TYPE]
 test rax, rax
 jz .norm_done
 test r14, r14
 jz inference_error
 cmp qword ptr [rax+TYPE], 0
 jne .norm_done
 mov [rax+TYPE], r14
.norm_done:
 RETURN

# Rule handlers preserve the enclosing normalizer's callee-saved registers.
norm_required:
 test r14, r14
 jnz .norm_required_end
 cmp rbx, R_CONST
 jne inference_error
 EQ "qword ptr [r12+A]", s_true
 jz .norm_const_bool
 EQ "qword ptr [r12+A]", s_false
 jnz inference_error
.norm_const_bool:
 mov r14, [builtin+TY_BOOL*8]
.norm_required_end:
 ret

norm_same:
 xor r15d, r15d
 cmp rbx, R_EQ
 je .norm_same_peer
 cmp rbx, R_ORDER
 je .norm_same_peer
 mov r15, r14
.norm_same_peer:
 test r15, r15
 jnz .norm_same_have
 C operand_type, "qword ptr [r12+A]"
 mov r15, rax
 test r15, r15
 jnz .norm_same_have
 cmp rbx, R_NEG
 jae .norm_same_have
 C operand_type, "qword ptr [r12+B]"
 mov r15, rax
.norm_same_have:
 C normalize_operand, "qword ptr [r12+A]", r15
 mov [r12+A], rax
 cmp rbx, R_NEG
 jae .norm_same_result
 C normalize_operand, "qword ptr [r12+B]", r15
 mov [r12+B], rax
.norm_same_result:
 cmp rbx, R_EQ
 je .norm_bool_result
 cmp rbx, R_ORDER
 je .norm_bool_result
 test r14, r14
 cmovz r14, r15
 ret
.norm_bool_result:
 test r14, r14
 jnz .norm_bool_end
 mov r14, [builtin+TY_BOOL*8]
.norm_bool_end:
 ret

norm_cast:
 test r14, r14
 jz inference_error
 C normalize_operand, "qword ptr [r12+A]", 0
 mov [r12+A], rax
 ret
norm_i2p:
 test r14, r14
 jz inference_error
 C normalize_operand, "qword ptr [r12+A]", "qword ptr [builtin+TY_U64*8]"
 mov [r12+A], rax
 ret
norm_p2i:
 C normalize_operand, "qword ptr [r12+A]", 0
 mov [r12+A], rax
 jmp norm_u64_result
norm_call:
 C find, "qword ptr [functions]", "qword ptr [r12+A]"
 test rax, rax
 jz .function_error
 mov r15, rax
 C normalize_args, "qword ptr [r12+LIST]", "qword ptr [r15+LIST]"
 mov rax, [r15+TYPE]
 jmp norm_result
norm_alloc:
 C normalize_operand, "qword ptr [r12+B]", "qword ptr [builtin+TY_U64*8]"
 mov [r12+B], rax
norm_stack:
 C pointer_type, "qword ptr [r12+A]"
 jmp norm_result
norm_free:
 C normalize_operand, "qword ptr [r12+A]", 0
 mov [r12+A], rax
 ret
norm_offset:
 C normalize_operand, "qword ptr [r12+A]", r14
 mov [r12+A], rax
 C resolve_reg, rax
 mov r15, [rax+TYPE]
 C normalize_operand, "qword ptr [r12+B]", "qword ptr [builtin+TY_I64*8]"
 mov [r12+B], rax
 mov rax, r15
 jmp norm_result
norm_field:
 C normalize_operand, "qword ptr [r12+A]", 0
 mov [r12+A], rax
 C resolve_reg, rax
 C pointer, "qword ptr [rax+TYPE]"
 cmp qword ptr [rax+FLAGS], TY_STRUCT
 jne type_error
 C find, "qword ptr [rax+LIST]", "qword ptr [r12+B]"
 test rax, rax
 jz .field_error
 C pointer_type, "qword ptr [rax+TYPE]"
 jmp norm_result
norm_load:
 xor r15d, r15d
 test r14, r14
 jz .norm_load_operand
 C pointer_type, r14
 mov r15, rax
.norm_load_operand:
 C normalize_operand, "qword ptr [r12+A]", r15
 mov [r12+A], rax
 C resolve_reg, rax
 C pointer, "qword ptr [rax+TYPE]"
 jmp norm_result
norm_store:
 C operand_type, "qword ptr [r12+A]"
 test rax, rax
 jnz .norm_store_pointer
 C operand_type, "qword ptr [r12+B]"
 test rax, rax
 jz inference_error
 C pointer_type, rax
.norm_store_pointer:
 mov r15, rax
 C pointer, r15
 mov [rbp-56], rax
 C normalize_operand, "qword ptr [r12+A]", r15
 mov [r12+A], rax
 C normalize_operand, "qword ptr [r12+B]", "qword ptr [rbp-56]"
 mov [r12+B], rax
 ret
norm_copy:
 C normalize_operand, "qword ptr [r12+A]", 0
 mov [r12+A], rax
 C normalize_operand, "qword ptr [r12+B]", 0
 mov [r12+B], rax
 C normalize_operand, "qword ptr [r12+C_]", "qword ptr [builtin+TY_U64*8]"
 mov [r12+C_], rax
 ret
norm_u64_result:
 mov rax, [builtin+TY_U64*8]
 jmp norm_result
norm_address:
 C pointer_type, "qword ptr [builtin+TY_U8*8]"
 jmp norm_result
norm_jump:
 C normalize_target, "qword ptr [r12+A]"
 ret
norm_branch:
 C normalize_operand, "qword ptr [r12+A]", "qword ptr [builtin+TY_BOOL*8]"
 mov [r12+A], rax
 C normalize_target, "qword ptr [r12+B]"
 C normalize_target, "qword ptr [r12+C_]"
 ret
norm_return:
 cmp qword ptr [r12+A], 0
 je .norm_return_end
 mov rax, [cur_fn]
 C normalize_operand, "qword ptr [r12+A]", "qword ptr [rax+TYPE]"
 mov [r12+A], rax
.norm_return_end:
 ret
norm_result:
 test r14, r14
 cmovz r14, rax
norm_none:
 ret
