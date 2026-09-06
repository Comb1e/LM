# Compiler records use pointer-sized fields. All compiler allocations belong to
# a process-lifetime arena; emitted programs retain explicit allocation/free.
.equ NEXT, 0
.equ NAME, 8
.equ TYPE, 16
.equ LIST, 24
.equ BODY, 32
.equ TOKEN, 40
.equ END, 48
.equ FLAGS, 56
.equ A, 64
.equ B, 72
.equ C_, 80
.equ D, 88
.equ ID, 96
.equ SIZE, 104
.equ ALIGN, 112
.equ AUX, 120
.equ ORIGIN, 128
.equ NODE_BYTES, 144
.equ TX, 8
.equ TS, 16
.equ TE, 24
.equ TL, 32
.equ TC, 40
.equ TEL, 48
.equ TEC, 56
.equ TK, 64
.equ K_EOF, 0
.equ K_WORD, 1
.equ K_REF, 2
.equ K_NUMBER, 3
.equ K_STRING, 4
.equ K_NL, 5
.equ K_PUNCT, 6
.equ TY_VOID, 0
.equ TY_BOOL, 1
.equ TY_I8, 2
.equ TY_I16, 3
.equ TY_I32, 4
.equ TY_I64, 5
.equ TY_U8, 6
.equ TY_U16, 7
.equ TY_U32, 8
.equ TY_U64, 9
.equ TY_F32, 10
.equ TY_F64, 11
.equ TY_PTR, 12
.equ TY_ARRAY, 13
.equ TY_STRUCT, 14

.macro FUNC name
.type \name, @function
\name:
 push rbp
 mov rbp, rsp
 push rbx
 push r12
 push r13
 push r14
 push r15
 sub rsp, 216
.endm
.macro RETURN
 lea rsp, [rbp-40]
 pop r15
 pop r14
 pop r13
 pop r12
 pop rbx
 pop rbp
 ret
.endm
.macro C fn, a=, b=, c=, d=, e=, f=
 .ifnb \f
 push \f
 .endif
 .ifnb \e
 push \e
 .endif
 .ifnb \d
 push \d
 .endif
 .ifnb \c
 push \c
 .endif
 .ifnb \b
 push \b
 .endif
 .ifnb \a
 push \a
 pop rdi
 .endif
 .ifnb \b
 pop rsi
 .endif
 .ifnb \c
 pop rdx
 .endif
 .ifnb \d
 pop rcx
 .endif
 .ifnb \e
 pop r8
 .endif
 .ifnb \f
 pop r9
 .endif
 xor eax, eax
 call \fn
.endm
.macro FAIL code, message
 C fail, "offset \code", "offset \message"
.endm
.macro STR label, value:vararg
.pushsection .rodata.strings,"a",@progbits
\label: .asciz \value
.popsection
.endm
.macro EQ reg, label
 C strcmp, "\reg", "offset \label"
 test eax, eax
.endm
.macro EM fmt, a=, b=, c=, d=
 C fprintf, "qword ptr [asm_file]", "offset \fmt", "\a", "\b", "\c", "\d"
.endm

STR e_tool, "E_TOOL"
STR e_syntax, "E_SYNTAX"
STR e_type, "E_TYPE"
STR e_layout, "E_LAYOUT"
STR e_literal, "E_LITERAL"
STR e_opcode, "E_OPCODE"
STR e_version, "E_VERSION"
STR e_duplicate, "E_DUPLICATE"
STR e_abi, "E_ABI"
STR e_block, "E_BLOCK"
STR e_entry, "E_ENTRY"
STR e_terminator, "E_TERMINATOR"
STR e_register, "E_REGISTER"
STR e_result, "E_RESULT"
STR e_cast, "E_CAST"
STR e_arity, "E_ARITY"
STR e_function, "E_FUNCTION"
STR e_field, "E_FIELD"
STR e_data, "E_DATA"
STR e_stack, "E_STACK"
STR e_limit, "E_LIMIT"
STR e_backend, "E_BACKEND"
STR e_backend_limit, "E_BACKEND_LIMIT"
STR e_target, "E_TARGET"
STR e_replace, "E_REPLACE"
STR e_unsupported, "E_UNSUPPORTED"
STR m_memory, "Compiler allocation failed"
STR m_syntax, "Unexpected token or end of source"
STR m_type, "Invalid or mismatched type"
STR m_layout, "Invalid recursive or excessive storage layout"
STR m_literal, "Literal is invalid or outside its type range"
STR m_duplicate, "Duplicate declaration"
STR m_abi, "Invalid C ABI signature or reserved symbol"
STR m_register, "Register is unavailable in this block"
STR m_opcode, "Unknown instruction"
STR m_arity, "Argument count mismatch"
STR m_entry, "Invalid entry block or executable main signature"
STR m_result, "Incorrect destination presence"
STR m_terminator, "Block must end with exactly one terminator"
STR m_io, "File operation failed"
STR m_config, "Invalid or unsupported configuration"
STR p_parse, "parse"
STR p_verify, "verify"
STR p_tool, "tool"
STR p_backend, "backend"
STR s_empty, ""
STR s_nl, "\n"
STR s_comma, ","
STR s_colon, ":"
STR s_equal, "="
STR s_open, "("
STR s_close, ")"
STR s_lbrace, "{"
STR s_rbrace, "}"
STR s_lbracket, "["
STR s_rbracket, "]"
STR s_lt, "<"
STR s_gt, ">"
STR s_semi, ";"
STR s_arrow, "->"
STR s_module, "module"
STR s_version, "version"
STR s_struct, "struct"
STR s_data, "data"
STR s_fn, "fn"
STR s_extern, "extern"
STR s_export, "export"
STR s_c, "c"
STR s_ptr, "ptr"
STR s_main, "main"
STR s_true, "true"
STR s_false, "false"
STR s_inf, "inf"
STR s_ninf, "-inf"
STR s_nan, "nan"

.bss
.align 8
arena_head: .zero 8
arena_used: .zero 8
arena_capacity: .zero 8
jout: .zero 8
source: .zero 8
source_len: .zero 8
filename: .zero 8
tokens: .zero 8
tok: .zero 8
last_tok: .zero 8
diag_tok: .zero 8
diag_phase: .zero 8
diag_reg: .zero 8
diag_expected: .zero 8
diag_actual: .zero 8
diagnostic_capture: .zero 8
diagnostic_jump: .zero 256
cur_fn: .zero 8
cur_block: .zero 8
cur_ins: .zero 8
module_name: .zero 8
module_version: .zero 8
functions: .zero 8
structs: .zero 8
data_nodes: .zero 8
types: .zero 8
builtin: .zero 96
next_id: .zero 8
asm_file: .zero 8
.text

FUNC alloc
 mov r12, rdi
 add r12, 15
 jc .alloc_bad
 and r12, -16
 mov rax, [arena_used]
 add rax, r12
 jc .alloc_bad
 cmp rax, [arena_capacity]
 jbe .alloc_have
 mov r13, 65536
 cmp r12, r13
 cmova r13, r12
 C calloc, 1, r13
 test rax, rax
 jz .alloc_bad
 mov [arena_head], rax
 mov qword ptr [arena_used], 0
 mov [arena_capacity], r13
.alloc_have:
 mov rax, [arena_head]
 add rax, [arena_used]
 add [arena_used], r12
 RETURN
.alloc_bad:
 FAIL e_tool, m_memory

FUNC node
 C alloc, NODE_BYTES
 inc qword ptr [next_id]
 mov rdx, [next_id]
 mov [rax+ID], rdx
 RETURN

FUNC slice
 mov r12, rdi
 mov r13, rsi
 lea rdi, [r13+1]
 call alloc
 mov r14, rax
 C memcpy, r14, r12, r13
 mov rax, r14
 RETURN

# Append a record to a singly linked list; rdi points to its head.
FUNC append
 mov rax, rdi
.append_loop:
 cmp qword ptr [rax], 0
 je .append_end
 mov rax, [rax]
 jmp .append_loop
.append_end:
 mov [rax], rsi
 RETURN

FUNC find
 mov r12, rdi
 mov r13, rsi
.find_loop:
 test r12, r12
 jz .find_done
 C strcmp, "qword ptr [r12+NAME]", r13
 test eax, eax
 jz .find_done
 mov r12, [r12+NEXT]
 jmp .find_loop
.find_done:
 mov rax, r12
 RETURN

FUNC text_out
 C fputs, rdi, "qword ptr [jout]"
 RETURN

FUNC json_string
 mov r12, rdi
 C strlen, r12
 C json_bytes, r12, rax
 RETURN

FUNC json_bytes
 mov r12, rdi
 lea r14, [rdi+rsi]
 C fputc, 34, "qword ptr [jout]"
.json_loop:
 cmp r12, r14
 jae .json_end
 movzx r13d, byte ptr [r12]
 cmp r13d, 128
 jae .json_utf8
 inc r12
 cmp r13b, 34
 je .json_slash
 cmp r13b, 92
 je .json_slash
 cmp r13b, 32
 jb .json_control
.json_char:
 C fputc, r13, "qword ptr [jout]"
 jmp .json_loop
.json_slash:
 C fputc, 92, "qword ptr [jout]"
 jmp .json_char
.json_control:
 C fprintf, "qword ptr [jout]", "offset j_hex", r13
 jmp .json_loop
.json_utf8:
 mov rsi, r14
 sub rsi, r12
 C utf8_length, r12, rsi
 test rax, rax
 jz .json_invalid_utf8
 mov r15, rax
 C fwrite, r12, 1, r15, "qword ptr [jout]"
 add r12, r15
 jmp .json_loop
.json_invalid_utf8:
 C text_out, "offset j_replacement"
 inc r12
 jmp .json_loop
.json_end:
 C fputc, 34, "qword ptr [jout]"
 RETURN
STR j_hex, "\\u%04x"
STR j_replacement, "\\ufffd"
STR j_number, "%ld"
STR j_start, "{\"ok\":false,\"diagnostics\":[{\"code\":"
STR j_phase, ",\"phase\":"
STR j_message, ",\"message\":"
STR j_function, ",\"function\":"
STR j_block, ",\"block\":"
STR j_register, ",\"register\":"
STR j_expected, ",\"expected\":"
STR j_actual, ",\"actual\":"
STR j_span, ",\"span\":{\"file\":"
STR j_location, ",\"line\":%ld,\"column\":%ld,\"end_line\":%ld,\"end_column\":%ld}"
STR j_end, "}]}\n"

FUNC fail
 mov r12, rdi
 mov r13, rsi
 mov rax, [stdout]
 cmp qword ptr [diagnostic_capture],0
 cmovne rax,[diagnostic_capture]
 mov [jout], rax
 C text_out, "offset j_start"
 C json_string, r12
 C text_out, "offset j_phase"
 mov rdi, [diag_phase]
 test rdi, rdi
 jnz .fail_phase
 mov rdi, offset p_tool
.fail_phase:
 call json_string
 C text_out, "offset j_message"
 C json_string, r13
 mov rax, [cur_fn]
 test rax, rax
 jz .fail_block
 mov r14, [rax+NAME]
 C text_out, "offset j_function"
 C json_string, r14
.fail_block:
 cmp qword ptr [v3_active],0
 jne .fail_types
 mov rax, [cur_block]
 test rax, rax
 jz .fail_reg
 mov r14, [rax+NAME]
 C text_out, "offset j_block"
 C json_string, r14
.fail_reg:
 cmp qword ptr [diag_reg], 0
 je .fail_types
 C text_out, "offset j_register"
 C json_string, "qword ptr [diag_reg]"
.fail_types:
 cmp qword ptr [diag_expected], 0
 je .fail_actual
 C text_out, "offset j_expected"
 C json_string, "qword ptr [diag_expected]"
.fail_actual:
 cmp qword ptr [diag_actual], 0
 je .fail_span
 C text_out, "offset j_actual"
 C json_string, "qword ptr [diag_actual]"
.fail_span:
 mov r14, [diag_tok]
 test r14, r14
 jz .fail_end
 C text_out, "offset j_span"
 C json_string, "qword ptr [filename]"
 C fprintf, "qword ptr [jout]", "offset j_location", "qword ptr [r14+TL]", "qword ptr [r14+TC]", "qword ptr [r14+TEL]", "qword ptr [r14+TEC]"
.fail_end:
 C text_out, "offset j_end"
 cmp qword ptr [diagnostic_capture],0
 je .fail_exit
 C longjmp, "offset diagnostic_jump", 1
.fail_exit:
 C exit, 2

# Read a bounded file into an arena buffer, returning buffer/length in rax/rdx.
FUNC read_file
 mov r12, rdi
 mov r13, rsi
 C fopen, r12, "offset mode_rb"
 test rax, rax
 jz .read_bad
 mov r14, rax
 lea rdi, [r13+2]
 call alloc
 mov r15, rax
 lea rcx, [r13+1]
 C fread, r15, 1, rcx, r14
 mov rbx, rax
 C ferror, r14
 test eax, eax
 jnz .read_bad
 C fclose, r14
 cmp rbx, r13
 ja .read_limit
 mov rax, r15
 mov rdx, rbx
 RETURN
.read_bad:
 FAIL e_tool, m_io
.read_limit:
 FAIL e_limit, m_source_limit
STR mode_rb, "rb"
STR mode_wb, "wb"
STR m_source_limit, "Source exceeds configured byte limit"
