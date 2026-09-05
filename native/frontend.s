.text
# UTF-8 validation returns the number of bytes in one scalar. The caller has a
# NUL-terminated buffer; continuation bytes never include NUL.
FUNC utf8_scalar
 C utf8_length, rdi, 4
 test rax, rax
 jz syntax_error
 RETURN

FUNC utf8_length
 test rsi, rsi
 jz .utf_invalid
 movzx eax, byte ptr [rdi]
 cmp eax, 128
 jb .utf_one
 mov ecx, 2
 mov edx, 128
 cmp eax, 194
 jb .utf_invalid
 cmp eax, 224
 jb .utf_two
 mov ecx, 3
 mov edx, 2048
 cmp eax, 240
 jb .utf_three
 mov ecx, 4
 mov edx, 65536
 cmp eax, 245
 jae .utf_invalid
 and eax, 7
 jmp .utf_tail
.utf_three:
 and eax, 15
 jmp .utf_tail
.utf_two:
 and eax, 31
.utf_tail:
 cmp rsi, rcx
 jb .utf_invalid
 mov r8d, 1
.utf_loop:
 movzx r9d, byte ptr [rdi+r8]
 sub r9d, 128
 cmp r9d, 63
 ja .utf_invalid
 shl eax, 6
 or eax, r9d
 inc r8d
 cmp r8d, ecx
 jb .utf_loop
 cmp eax, edx
 jb .utf_invalid
 cmp eax, 1114111
 ja .utf_invalid
 cmp eax, 55296
 jb .utf_ok
 cmp eax, 57343
 jbe .utf_invalid
.utf_ok:
 mov eax, ecx
 RETURN
.utf_one:
 mov eax, 1
 RETURN
.utf_invalid:
 xor eax, eax
 RETURN

syntax_error:
 FAIL e_syntax, m_syntax

# ASCII identifier character classification: eax=1 for start, 2 for digit.
FUNC ident_char
 xor eax, eax
 cmp dil, 95
 je .ident_start
 cmp dil, 65
 jb .ident_digit
 cmp dil, 90
 jbe .ident_start
 cmp dil, 97
 jb .ident_digit
 cmp dil, 122
 jbe .ident_start
.ident_digit:
 cmp dil, 48
 jb .ident_end
 cmp dil, 57
 ja .ident_end
 mov eax, 2
 RETURN
.ident_start:
 mov eax, 1
.ident_end:
 RETURN

FUNC lex
 mov qword ptr [tokens], 0
 mov qword ptr [diag_phase], offset p_parse
 mov r12, [source]
 xor r13d, r13d
 mov r14d, 1
 mov r15d, 1
 xor ebx, ebx
.lex_next:
 movzx eax, byte ptr [r12+r13]
 test al, al
 jz .lex_eof
 cmp al, 32
 je .lex_space
 cmp al, 9
 je .lex_space
 cmp al, 13
 je .lex_space
 cmp al, 35
 je .lex_comment
 call node
 mov [rbp-48], rax
 mov [rax+TS], r13
 mov [rax+TL], r14
 mov [rax+TC], r15
 mov [diag_tok], rax
 mov [rbp-56], r13
 movzx edi, byte ptr [r12+r13]
 cmp dil, 10
 je .lex_newline
 cmp dil, 34
 je .lex_string
 cmp dil, 37
 je .lex_ref
 cmp dil, 64
 je .lex_ref
 cmp dil, 94
 je .lex_ref
 cmp dil, 45
 je .lex_minus
 call ident_char
 cmp eax, 1
 je .lex_word
 cmp eax, 2
 je .lex_number
 movzx esi, byte ptr [r12+r13]
 C strchr, "offset punctuation", rsi
 test rax, rax
 jz syntax_error
.lex_punct:
 mov qword ptr [rbp-64], K_PUNCT
 inc r13
 inc r15
 jmp .lex_save
.lex_minus:
 cmp byte ptr [r12+r13+1], 62
 jne .lex_negative
 inc r13
 inc r15
 jmp .lex_punct
.lex_negative:
 cmp byte ptr [r12+r13+1], 105
 je .lex_word
 movzx edi, byte ptr [r12+r13+1]
 call ident_char
 cmp eax, 2
 jne syntax_error
.lex_number:
 mov qword ptr [rbp-64], K_NUMBER
 inc r13
 inc r15
.lex_number_loop:
 movzx edi, byte ptr [r12+r13]
 call ident_char
 test eax, eax
 jnz .lex_number_more
 cmp byte ptr [r12+r13], 46
 je .lex_number_more
 cmp byte ptr [r12+r13], 43
 je .lex_number_sign
 cmp byte ptr [r12+r13], 45
 jne .lex_save
.lex_number_sign:
 mov al, [r12+r13-1]
 or al, 32
 cmp al, 101
 jne .lex_save
.lex_number_more:
 inc r13
 inc r15
 jmp .lex_number_loop
.lex_ref:
 inc r13
 inc r15
 movzx edi, byte ptr [r12+r13]
 call ident_char
 cmp eax, 1
 jne syntax_error
 mov qword ptr [rbp-64], K_REF
 jmp .lex_word_more
.lex_word:
 mov qword ptr [rbp-64], K_WORD
.lex_word_more:
 inc r13
 inc r15
 movzx edi, byte ptr [r12+r13]
 call ident_char
 test eax, eax
 jnz .lex_word_more
 jmp .lex_save
.lex_string:
 mov qword ptr [rbp-64], K_STRING
 inc r13
 inc r15
.lex_string_loop:
 mov al, [r12+r13]
 cmp al, 32
 jb syntax_error
 cmp al, 34
 je .lex_string_end
 cmp al, 92
 jne .lex_string_utf
 inc r13
 inc r15
 cmp byte ptr [r12+r13], 32
 jb syntax_error
.lex_string_utf:
 lea rdi, [r12+r13]
 call utf8_scalar
 add r13, rax
 inc r15
 jmp .lex_string_loop
.lex_string_end:
 inc r13
 inc r15
 jmp .lex_save
.lex_newline:
 mov qword ptr [rbp-64], K_NL
 inc r13
 inc r14
 mov r15d, 1
.lex_save:
 mov rdx, [rbp-56]
 lea rdi, [r12+rdx]
 mov rsi, r13
 sub rsi, rdx
 call slice
 mov rcx, [rbp-48]
 mov [rcx+TX], rax
 mov [rcx+TE], r13
 mov [rcx+TEL], r14
 mov [rcx+TEC], r15
 mov rax, [rbp-64]
 mov [rcx+TK], rax
 test rbx, rbx
 jz .lex_first
 mov [rbx+NEXT], rcx
 jmp .lex_linked
.lex_first:
 mov [tokens], rcx
.lex_linked:
 mov rbx, rcx
 jmp .lex_next
.lex_space:
 inc r13
 inc r15
 jmp .lex_next
.lex_comment:
 mov al, [r12+r13]
 test al, al
 jz .lex_next
 cmp al, 10
 je .lex_next
 lea rdi, [r12+r13]
 call utf8_scalar
 add r13, rax
 inc r15
 jmp .lex_comment
.lex_eof:
 cmp r13, [source_len]
 jne syntax_error
 call node
 mov qword ptr [rax+TX], offset s_empty
 mov [rax+TS], r13
 mov [rax+TE], r13
 mov [rax+TL], r14
 mov [rax+TEL], r14
 mov [rax+TC], r15
 mov [rax+TEC], r15
 test rbx, rbx
 jz .lex_empty
 mov [rbx+NEXT], rax
 jmp .lex_done
.lex_empty:
 mov [tokens], rax
.lex_done:
 mov rax, [tokens]
 mov [tok], rax
 mov [diag_tok], rax
 RETURN
STR punctuation, "{}()[]<>:,;="

FUNC take
 mov rax, [tok]
 cmp qword ptr [rax+TK], K_EOF
 je syntax_error
 mov [last_tok], rax
 mov rdx, [rax+NEXT]
 mov [tok], rdx
 mov [diag_tok], rdx
 RETURN

FUNC accept
 mov r12, rdi
 mov rax, [tok]
 C strcmp, "qword ptr [rax+TX]", r12
 test eax, eax
 jnz .accept_no
 call take
 mov eax, 1
 RETURN
.accept_no:
 xor eax, eax
 RETURN

FUNC expect
 call accept
 test eax, eax
 jz syntax_error
 mov rax, [last_tok]
 RETURN

FUNC take_kind
 mov rax, [tok]
 cmp [rax+TK], rdi
 jne syntax_error
 call take
 mov rax, [rax+TX]
 RETURN

FUNC lines
.lines_loop:
 mov rax, [tok]
 cmp qword ptr [rax+TK], K_NL
 jne .lines_end
 call take
 jmp .lines_loop
.lines_end:
 RETURN

FUNC endline
 mov rax, [tok]
 cmp qword ptr [rax+TK], K_EOF
 je .endline_end
 cmp qword ptr [rax+TK], K_NL
 jne syntax_error
 call lines
.endline_end:
 RETURN

FUNC reference
 mov r12, rdi
 C take_kind, K_REF
 cmp byte ptr [rax], r12b
 jne syntax_error
 inc rax
 RETURN

# Parse an integer magnitude with explicit overflow detection. Unlike strtol,
# decimal leading zeroes never select octal; -2^63 and UINT64_MAX remain exact.
FUNC integer_text
 mov r12, rdi
 xor r13d, r13d
 xor r14d, r14d
 mov ebx, 10
 cmp byte ptr [r12], 45
 jne .integer_base
 inc r12
 mov r14d, 1
.integer_base:
 cmp byte ptr [r12], 48
 jne .integer_start
 mov al, [r12+1]
 or al, 32
 cmp al, 120
 jne .integer_start
 add r12, 2
 mov ebx, 16
.integer_start:
 cmp byte ptr [r12], 0
 je literal_error
.integer_loop:
 movzx ecx, byte ptr [r12]
 test cl, cl
 jz .integer_end
 cmp cl, 48
 jb literal_error
 cmp cl, 57
 jbe .integer_digit
 or cl, 32
 sub ecx, 87
 cmp ecx, 10
 jb literal_error
 jmp .integer_acc
.integer_digit:
 sub ecx, 48
.integer_acc:
 cmp ecx, ebx
 jae literal_error
 mov rax, r13
 mul rbx
 test rdx, rdx
 jnz literal_error
 add rax, rcx
 jc literal_error
 mov r13, rax
 inc r12
 jmp .integer_loop
.integer_end:
 mov rax, r13
 mov rdx, r14
 RETURN
literal_error:
 FAIL e_literal, m_literal

FUNC integer
 C take_kind, K_NUMBER
 C integer_text, rax
 test rdx, rdx
 jz .integer_ret
 mov rcx, 0x8000000000000000
 cmp rax, rcx
 ja literal_error
 neg rax
.integer_ret:
 RETURN

FUNC init_types
 mov qword ptr [types], 0
 mov r12, offset builtin_names
 xor r13d, r13d
.init_type_loop:
 call node
 mov r14, rax
 mov rdx, [r12+r13*8]
 mov [r14+NAME], rdx
 mov [r14+FLAGS], r13
 mov [builtin+r13*8], r14
 C append, "offset types", r14
 inc r13
 cmp r13, 12
 jb .init_type_loop
 RETURN
STR ty_void, "void"
STR ty_bool, "bool"
STR ty_i8, "i8"
STR ty_i16, "i16"
STR ty_i32, "i32"
STR ty_i64, "i64"
STR ty_u8, "u8"
STR ty_u16, "u16"
STR ty_u32, "u32"
STR ty_u64, "u64"
STR ty_f32, "f32"
STR ty_f64, "f64"
.section .rodata
builtin_names: .quad ty_void,ty_bool,ty_i8,ty_i16,ty_i32,ty_i64,ty_u8,ty_u16,ty_u32,ty_u64,ty_f32,ty_f64
.text

FUNC intern_type
 mov r12, rdi
 C find, "qword ptr [types]", r12
 test rax, rax
 jnz .intern_end
 call node
 mov [rax+NAME], r12
 mov qword ptr [rax+FLAGS], TY_STRUCT
 mov r13, rax
 C append, "offset types", r13
 mov rax, r13
.intern_end:
 RETURN

FUNC pointer_type
 mov r12, rdi
 lea rdi, [rbp-48]
 C asprintf, rdi, "offset fmt_ptr", "qword ptr [r12+NAME]"
 cmp eax, 0
 jl .alloc_bad
 C intern_type, "qword ptr [rbp-48]"
 mov qword ptr [rax+FLAGS], TY_PTR
 mov [rax+TYPE], r12
 RETURN
STR fmt_ptr, "ptr<%s>"
STR fmt_array, "[%s;%lu]"

FUNC parse_type
 mov r12, rdi
 cmp r12, [cfg_type_depth]
 jae syntax_error
 C accept, "offset s_lbracket"
 test eax, eax
 jnz .type_array
 C take_kind, K_WORD
 mov r13, rax
 EQ r13, s_ptr
 jz .type_pointer
 C intern_type, r13
 RETURN
.type_pointer:
 C expect, "offset s_lt"
 lea rdi, [r12+1]
 call parse_type
 mov r13, rax
 C expect, "offset s_gt"
 C pointer_type, r13
 RETURN
.type_array:
 lea rdi, [r12+1]
 call parse_type
 mov r13, rax
 C expect, "offset s_semi"
 call integer
 mov r14, rax
 C expect, "offset s_rbracket"
 lea rdi, [rbp-48]
 C asprintf, rdi, "offset fmt_array", "qword ptr [r13+NAME]", r14
 cmp eax, 0
 jl .alloc_bad
 C intern_type, "qword ptr [rbp-48]"
 mov qword ptr [rax+FLAGS], TY_ARRAY
 mov [rax+TYPE], r13
 mov [rax+A], r14
 RETURN

FUNC param
 mov r12, [tok]
 call node
 mov r13, rax
 mov [r13+TOKEN], r12
 C reference, 37
 mov [r13+NAME], rax
 C expect, "offset s_colon"
 C parse_type, 0
 mov [r13+TYPE], rax
 mov rax, [last_tok]
 mov [r13+END], rax
 mov rax, r13
 RETURN

FUNC destination
 cmp qword ptr [module_version], 2
 je .destination_v2
 call param
 RETURN
.destination_v2:
 call node
 mov r12, rax
 mov rax, [tok]
 mov [r12+TOKEN], rax
 C reference, 37
 mov [r12+NAME], rax
 C accept, "offset s_colon"
 test eax, eax
 jz .destination_end
 C parse_type, 0
 mov [r12+TYPE], rax
.destination_end:
 mov rax, [last_tok]
 mov [r12+END], rax
 mov rax, r12
 RETURN

# V2 operands retain literal spelling and span until contextual normalization.
FUNC operand
 cmp qword ptr [module_version], 2
 je .operand_v2
 C reference, 37
 RETURN
.operand_v2:
 call node
 mov r12, rax
 mov rax, [tok]
 mov [r12+TOKEN], rax
 cmp qword ptr [rax+TK], K_REF
 jne .operand_literal
 C reference, 37
 mov [r12+NAME], rax
 jmp .operand_end
.operand_literal:
 cmp qword ptr [rax+TK], K_NUMBER
 je .operand_take
 cmp qword ptr [rax+TK], K_WORD
 jne syntax_error
.operand_take:
 call take
 mov rax, [rax+TX]
 mov [r12+NAME], rax
 mov qword ptr [r12+FLAGS], 1
 C accept, "offset s_colon"
 test eax, eax
 jz .operand_end
 C parse_type, 0
 mov [r12+TYPE], rax
.operand_end:
 mov rax, [last_tok]
 mov [r12+END], rax
 mov rax, r12
 RETURN

# Param lists and argument lists have the same next/name/type record shape.
FUNC param_list
 mov qword ptr [rbp-48], 0
 C expect, "offset s_open"
 C accept, "offset s_close"
 test eax, eax
 jnz .params_done
.params_loop:
 call param
 lea rdi, [rbp-48]
 C append, rdi, rax
 C accept, "offset s_comma"
 test eax, eax
 jnz .params_loop
 C expect, "offset s_close"
.params_done:
 mov rax, [rbp-48]
 RETURN

FUNC arg_list
 mov qword ptr [rbp-48], 0
 C expect, "offset s_open"
 C accept, "offset s_close"
 test eax, eax
 jnz .args_done
.args_loop:
 cmp qword ptr [module_version], 2
 je .args_v2
 call node
 mov r12, rax
 C reference, 37
 mov [r12+NAME], rax
 jmp .args_append
.args_v2:
 call operand
 mov r12, rax
.args_append:
 lea rdi, [rbp-48]
 C append, rdi, r12
 C accept, "offset s_comma"
 test eax, eax
 jnz .args_loop
 C expect, "offset s_close"
.args_done:
 mov rax, [rbp-48]
 RETURN

FUNC parse_target
 call node
 mov r12, rax
 C reference, 94
 mov [r12+NAME], rax
 call arg_list
 mov [r12+LIST], rax
 mov rax, r12
 RETURN

FUNC instruction
 call node
 mov r12, rax
 mov rax, [tok]
 mov [r12+TOKEN], rax
 mov rax, [rax+TX]
 cmp byte ptr [rax], 37
 jne .ins_opcode
 call destination
 mov [r12+TYPE], rax
 C expect, "offset s_equal"
.ins_opcode:
 mov r14, [tok]
 C take_kind, K_WORD
 mov r13, rax
 xor ebx, ebx
.ins_find:
 imul r15, rbx, 40
 C strcmp, r13, "qword ptr [ops+r15]"
 test eax, eax
 jz .ins_found
 inc rbx
 cmp rbx, OP_COUNT
 jb .ins_find
 mov [diag_tok], r14
 mov [diag_actual], r13
 FAIL e_opcode, m_opcode
.ins_found:
 mov [r12+FLAGS], rbx
 mov [r12+NAME], r13
 mov r13, [ops+r15+8]
 cmp r13, S_NONE
 je .ins_end
 cmp r13, S_LITERAL
 je .ins_literal
 cmp r13, S_TERNARY
 jbe .ins_regs
 cmp r13, S_CALL
 je .ins_call
 cmp r13, S_ALLOC
 jbe .ins_type
 cmp r13, S_FIELD
 je .ins_field
 cmp r13, S_SYMBOL
 je .ins_symbol
 cmp r13, S_TARGET
 je .ins_target
 cmp r13, S_BRANCH
 je .ins_branch
 mov rax, [tok]
 cmp qword ptr [rax+TK], K_NL
 je .ins_end
 cmp qword ptr [rax+TK], K_EOF
 je .ins_end
 call operand
 mov [r12+A], rax
 jmp .ins_end
.ins_literal:
 mov rax, [tok]
 cmp qword ptr [rax+TK], K_WORD
 je .ins_literal_take
 cmp qword ptr [rax+TK], K_NUMBER
 jne syntax_error
.ins_literal_take:
 call take
 mov rax, [rax+TX]
 mov [r12+A], rax
 jmp .ins_end
.ins_regs:
 xor ebx, ebx
.ins_reg_loop:
 call operand
 mov [r12+A+rbx*8], rax
 inc rbx
 mov rax, r13
 dec rax
 cmp rbx, rax
 jae .ins_end
 C expect, "offset s_comma"
 jmp .ins_reg_loop
.ins_call:
 C reference, 64
 mov [r12+A], rax
 call arg_list
 mov [r12+LIST], rax
 jmp .ins_end
.ins_type:
 C parse_type, 0
 mov [r12+A], rax
 cmp r13, S_TYPE
 je .ins_end
 C expect, "offset s_comma"
 cmp r13, S_COUNT
 jne .ins_alloc
 call integer
 mov [r12+B], rax
 jmp .ins_end
.ins_alloc:
 call operand
 mov [r12+B], rax
 jmp .ins_end
.ins_field:
 call operand
 mov [r12+A], rax
 C expect, "offset s_comma"
 C take_kind, K_WORD
 mov [r12+B], rax
 jmp .ins_end
.ins_symbol:
 C reference, 64
 mov [r12+A], rax
 jmp .ins_end
.ins_target:
 call parse_target
 mov [r12+A], rax
 jmp .ins_end
.ins_branch:
 call operand
 mov [r12+A], rax
 C expect, "offset s_comma"
 call parse_target
 mov [r12+B], rax
 C expect, "offset s_comma"
 call parse_target
 mov [r12+C_], rax
.ins_end:
 mov rax, [last_tok]
 mov [r12+END], rax
 call endline
 mov rax, r12
 RETURN

FUNC parse_function
 mov r12, rdi
 mov r13, [tok]
 C expect, "offset s_fn"
 call node
 mov r14, rax
 mov [r14+FLAGS], r12
 mov [r14+TOKEN], r13
 C reference, 64
 mov [r14+NAME], rax
 mov [cur_fn], r14
 call param_list
 mov [r14+LIST], rax
 C expect, "offset s_arrow"
 C parse_type, 0
 mov [r14+TYPE], rax
 cmp r12, 1
 je .function_end
 C expect, "offset s_lbrace"
 call endline
.function_block:
 C accept, "offset s_rbrace"
 test eax, eax
 jnz .function_end
 call node
 mov r15, rax
 mov rax, [tok]
 mov [r15+TOKEN], rax
 C reference, 94
 mov [r15+NAME], rax
 mov [cur_block], r15
 mov rax, [tok]
 mov rax, [rax+TX]
 EQ rax, s_open
 jnz .block_no_params
 call param_list
 mov [r15+LIST], rax
.block_no_params:
 C expect, "offset s_colon"
 call endline
.block_ins:
 mov rax, [tok]
 mov rax, [rax+TX]
 cmp byte ptr [rax], 94
 je .block_end
 cmp byte ptr [rax], 125
 je .block_end
 call instruction
 lea rdi, [r15+BODY]
 C append, rdi, rax
 jmp .block_ins
.block_end:
 mov rax, [last_tok]
 mov [r15+END], rax
 lea rdi, [r14+BODY]
 C append, rdi, r15
 jmp .function_block
.function_end:
 mov rax, [last_tok]
 mov [r14+END], rax
 mov qword ptr [cur_fn], 0
 mov qword ptr [cur_block], 0
 call endline
 mov rax, r14
 RETURN

FUNC parse_module
 mov qword ptr [functions], 0
 mov qword ptr [structs], 0
 mov qword ptr [data_nodes], 0
 call init_types
 call lex
 call lines
 C expect, "offset s_module"
 C take_kind, K_WORD
 mov [module_name], rax
 C expect, "offset s_version"
 call integer
 mov [module_version], rax
 call endline
.module_loop:
 mov r12, [tok]
 cmp qword ptr [r12+TK], K_EOF
 je .module_done
 C accept, "offset s_struct"
 test eax, eax
 jnz .module_struct
 C accept, "offset s_data"
 test eax, eax
 jnz .module_data
 xor r13d, r13d
 C accept, "offset s_extern"
 test eax, eax
 jz .module_export
 mov r13d, 1
 C expect, "offset s_c"
 jmp .module_function
.module_export:
 C accept, "offset s_export"
 test eax, eax
 jz .module_function
 mov r13d, 2
 C expect, "offset s_c"
.module_function:
 C parse_function, r13
 mov [rax+TOKEN], r12
 C append, "offset functions", rax
 jmp .module_loop
.module_struct:
 C take_kind, K_WORD
 mov r14, rax
 EQ r14, s_ptr
 jz .duplicate_error
 EQ r14, reserved_array
 jz .duplicate_error
 mov rax, r14
 C intern_type, rax
 mov r13, rax
 cmp qword ptr [r13+FLAGS], TY_STRUCT
 jne .duplicate_error
 cmp qword ptr [r13+B], 0
 jne .duplicate_error
 mov qword ptr [r13+B], 1
 mov [r13+TOKEN], r12
 # Types and declarations use separate lists so interning never alters order.
 call node
 mov [rax+TYPE], r13
 mov r14, rax
 C append, "offset structs", r14
 C expect, "offset s_lbrace"
 call endline
.struct_fields:
 C accept, "offset s_rbrace"
 test eax, eax
 jnz .struct_end
 call node
 mov r14, rax
 mov rax, [tok]
 mov [r14+TOKEN], rax
 C take_kind, K_WORD
 mov [r14+NAME], rax
 C expect, "offset s_colon"
 C parse_type, 0
 mov [r14+TYPE], rax
 lea rdi, [r13+LIST]
 C append, rdi, r14
 call endline
 jmp .struct_fields
.struct_end:
 call endline
 jmp .module_loop
.module_data:
 call node
 mov r13, rax
 mov [r13+TOKEN], r12
 C reference, 64
 mov [r13+NAME], rax
 C expect, "offset s_equal"
 C take_kind, K_STRING
 C decode_string, rax
 mov [r13+A], rax
 mov [r13+SIZE], rdx
 mov rax, [last_tok]
 mov [r13+END], rax
 C append, "offset data_nodes", r13
 call endline
 jmp .module_loop
.module_done:
 RETURN
.duplicate_error:
 FAIL e_duplicate, m_duplicate
STR reserved_array, "array"

# JSON string decoder. Unicode escapes are paired and encoded as UTF-8;
# literal UTF-8 was validated by the lexer. Output length includes embedded NUL.
FUNC hex4
 xor eax, eax
 xor ecx, ecx
.hex4_loop:
 movzx edx, byte ptr [rdi+rcx]
 cmp edx, 48
 jb literal_error
 cmp edx, 57
 jbe .hex4_digit
 or edx, 32
 sub edx, 87
 cmp edx, 10
 jb literal_error
 cmp edx, 15
 ja literal_error
 jmp .hex4_acc
.hex4_digit:
 sub edx, 48
.hex4_acc:
 shl eax, 4
 or eax, edx
 inc ecx
 cmp ecx, 4
 jb .hex4_loop
 RETURN

FUNC decode_string
 mov r12, rdi
 C strlen, r12
 C alloc, rax
 mov r13, rax
 mov r14, rax
 inc r12
.decode_loop:
 movzx eax, byte ptr [r12]
 inc r12
 cmp eax, 34
 je .decode_end
 cmp eax, 92
 je .decode_escape
 cmp eax, 32
 jb literal_error
.decode_byte:
 mov [r14], al
 inc r14
 jmp .decode_loop
.decode_escape:
 movzx eax, byte ptr [r12]
 inc r12
 cmp eax, 34
 je .decode_byte
 cmp eax, 92
 je .decode_byte
 cmp eax, 47
 je .decode_byte
 cmp eax, 117
 je .decode_unicode
 mov r15, offset escape_names
 xor ebx, ebx
.decode_simple:
 cmp al, [r15+rbx]
 je .decode_found
 inc rbx
 cmp rbx, 5
 jb .decode_simple
 jmp literal_error
.decode_found:
 movzx eax, byte ptr [escape_values+rbx]
 jmp .decode_byte
.decode_unicode:
 C hex4, r12
 add r12, 4
 cmp eax, 55296
 jb .decode_scalar
 cmp eax, 57343
 ja .decode_scalar
 cmp eax, 56319
 ja literal_error
 mov ebx, eax
 cmp word ptr [r12], 0x755c
 jne literal_error
 add r12, 2
 C hex4, r12
 add r12, 4
 sub eax, 56320
 cmp eax, 1023
 ja literal_error
 sub ebx, 55296
 shl ebx, 10
 add eax, ebx
 add eax, 65536
.decode_scalar:
 cmp eax, 128
 jb .decode_byte
 mov edx, eax
 cmp eax, 2048
 jb .decode_two
 cmp eax, 65536
 jb .decode_three
 shr eax, 18
 or al, 240
 mov [r14], al
 inc r14
 mov eax, edx
 shr eax, 12
 and al, 63
 or al, 128
 jmp .decode_third
.decode_three:
 shr eax, 12
 or al, 224
.decode_third:
 mov [r14], al
 inc r14
 mov eax, edx
 shr eax, 6
 and al, 63
 or al, 128
 jmp .decode_second
.decode_two:
 shr eax, 6
 or al, 192
.decode_second:
 mov [r14], al
 inc r14
 mov eax, edx
 and al, 63
 or al, 128
 jmp .decode_byte
.decode_end:
 cmp byte ptr [r12], 0
 jne literal_error
 mov rax, r13
 mov rdx, r14
 sub rdx, r13
 RETURN
STR escape_names, "bfnrt"
.section .rodata
escape_values: .byte 8,12,10,13,9
.text
