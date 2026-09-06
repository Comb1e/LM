.text
FUNC select_source
 cmp qword ptr [v3_active],0
 je .select_v3_ok
 cmp qword ptr [cli_block],0
 jne .v3_block_error
.select_v3_ok:
 cmp qword ptr [cli_function],0
 je usage_error
 C find, "qword ptr [functions]", "qword ptr [cli_function]"
 test rax,rax
 jz .function_error
 cmp qword ptr [rax+ORIGIN],0
 jne .library_source_error
 mov r12,rax
 cmp qword ptr [cli_block],0
 je .select_done
 C find, "qword ptr [r12+BODY]", "qword ptr [cli_block]"
 test rax,rax
 jz .target_missing
 mov rdx,r12
 RETURN
.select_done:
 mov rdx,rax
 RETURN

FUNC source_slice
 mov rax,[rdi+TOKEN]
 mov rdx,[rdi+END]
 mov r12,[rax+TS]
 mov r13,[rdx+TE]
 sub r13,r12
 add r12,[source]
 C slice, r12, r13
 RETURN

FUNC json_params
 mov r12,rdi
 C text_out, "offset s_lbracket"
.json_params_loop:
 test r12,r12
 jz .json_params_end
 C text_out, "offset j_name"
 C json_string, "qword ptr [r12+NAME]"
 C text_out, "offset j_type"
 mov rax,[r12+TYPE]
 C json_string, "qword ptr [rax+NAME]"
 C text_out, "offset s_rbrace"
 mov r12,[r12+NEXT]
 test r12,r12
 jz .json_params_end
 C text_out, "offset s_comma"
 jmp .json_params_loop
.json_params_end:
 C text_out, "offset s_rbracket"
 RETURN

FUNC json_signature
 mov r12,rdi
 call v3_signature
 test eax,eax
 jnz .signature_done
 mov rdi,r12
 mov r12,rdi
 C text_out, "offset j_name"
 C json_string, "qword ptr [r12+NAME]"
 C text_out, "offset j_params"
 C json_params, "qword ptr [r12+LIST]"
 C text_out, "offset j_returns"
 mov rax,[r12+TYPE]
 C json_string, "qword ptr [rax+NAME]"
 C text_out, "offset j_external"
 xor edi,edi
 cmp qword ptr [r12+FLAGS],1
 sete dil
 call json_bool
 C text_out, "offset j_exported"
 xor edi,edi
 cmp qword ptr [r12+FLAGS],2
 sete dil
 call json_bool
 C text_out, "offset s_rbrace"
.signature_done:
 RETURN
STR j_name, "{\"name\":"
STR j_type, ",\"type\":"
STR j_params, ",\"params\":"
STR j_returns, ",\"returns\":"
STR j_external, ",\"external\":"
STR j_exported, ",\"exported\":"
STR j_inspect_version, ",\"version\":%lu"
STR j_revision, ",\"revision\":"
STR j_inspect_function, ",\"function\":"
STR j_inspect_functions, ",\"functions\":["
STR j_source, ",\"source\":"
STR j_blocks, ",\"blocks\":["
STR j_callees, ",\"callees\":["
STR j_types, ",\"types\":["
STR j_fields, ",\"fields\":"
STR j_data, ",\"data\":["
STR j_data_bytes, ",\"bytes\":%lu"
STR j_instructions, ",\"instructions\":{"
STR j_instruction_syntax, "{\"syntax\":"
STR j_instruction_result, ",\"result\":"
STR j_instruction_rule, ",\"rule\":"
STR j_instruction_inference, ",\"inference\":"
STR j_instruction_description, ",\"description\":"
STR j_instruction_terminator, ",\"terminator\":"

FUNC require_type
 mov r12,rdi
 test r12,r12
 jz .require_type_end
 cmp qword ptr [r12+D],0
 jne .require_type_end
 mov qword ptr [r12+D],1
 cmp qword ptr [r12+FLAGS],TY_PTR
 je .require_element
 cmp qword ptr [r12+FLAGS],TY_ARRAY
 je .require_element
 cmp qword ptr [r12+FLAGS],TY_STRUCT
 jne .require_type_end
 mov r13,[r12+LIST]
.require_fields:
 test r13,r13
 jz .require_type_end
 C require_type, "qword ptr [r13+TYPE]"
 mov r13,[r13+NEXT]
 jmp .require_fields
.require_element:
 C require_type, "qword ptr [r12+TYPE]"
.require_type_end:
 RETURN

FUNC require_params
 mov r12,rdi
.require_params_loop:
 test r12,r12
 jz .require_params_end
 C require_type, "qword ptr [r12+TYPE]"
 mov r12,[r12+NEXT]
 jmp .require_params_loop
.require_params_end:
 RETURN

FUNC inspect_module
 C text_out, "offset j_check"
 C json_string, "qword ptr [module_name]"
 mov rax,[module_version]
 cmp qword ptr [v3_active],0
 je .inspect_version_ready
 mov eax,3
.inspect_version_ready:
 C fprintf, "qword ptr [jout]", "offset j_inspect_version",rax
 call json_revision
 cmp qword ptr [compact_view],0
 je .inspect_validation_done
 C text_out, "offset j_validation"
 C text_out, "qword ptr [inspection_validation]"
.inspect_validation_done:
 cmp qword ptr [cli_module],0
 jne .inspect_whole
 call select_source
 mov r12,rax
 mov r13,rdx
 mov [inspection_function],rdx
 C text_out, "offset j_inspect_function"
 C json_signature, r13
 C text_out, "offset j_source"
 C source_slice, r12
 C json_string, rax
 C require_type, "qword ptr [r13+TYPE]"
 C require_params, "qword ptr [r13+LIST]"
 mov r14,[r13+BODY]
 cmp qword ptr [cli_block],0
 je .inspect_scan_start
 mov r14,r12
.inspect_scan_start:
 mov qword ptr [rbp-48],0
.inspect_scan_blocks:
 test r14,r14
 jz .inspect_blocks_output
 mov qword ptr [r14+B],1
 C require_params, "qword ptr [r14+LIST]"
 mov r15,[r14+BODY]
.inspect_scan_ins:
 test r15,r15
 jz .inspect_scan_next
 mov rax,[r15+FLAGS]
 bts qword ptr [rbp-48],rax
 mov rbx,rax
 mov rax,[r15+TYPE]
 test rax,rax
 jz .inspect_dependencies
 C require_type, "qword ptr [rax+TYPE]"
.inspect_dependencies:
 cmp rbx,OP_call
 je .inspect_call
 cmp rbx,OP_address
 je .inspect_data
 cmp rbx,OP_jump
 je .inspect_jump
 cmp rbx,OP_branch
 je .inspect_branch
 cmp rbx,OP_stack
 je .inspect_storage
 cmp rbx,OP_alloc
 je .inspect_storage
 cmp rbx,OP_sizeof
 je .inspect_storage
 cmp rbx,OP_alignof
 jne .inspect_scan_ins_next
.inspect_storage:
 C require_type, "qword ptr [r15+A]"
 jmp .inspect_scan_ins_next
.inspect_call:
 mov rax,[r15+A]
 cmp qword ptr [inspection_unverified],0
 je .inspect_call_found
 C find, "qword ptr [functions]", rax
 test rax,rax
 jnz .inspect_call_found
 C unresolved_dependency, "offset word_function", "qword ptr [r15+A]"
 jmp .inspect_scan_ins_next
.inspect_call_found:
 mov qword ptr [rax+B],1
 mov rbx,rax
 C require_type, "qword ptr [rbx+TYPE]"
 C require_params, "qword ptr [rbx+LIST]"
 jmp .inspect_scan_ins_next
.inspect_data:
 mov rax,[r15+A]
 cmp qword ptr [inspection_unverified],0
 je .inspect_data_found
 C find, "qword ptr [data_nodes]", rax
 test rax,rax
 jnz .inspect_data_found
 C unresolved_dependency, "offset word_data", "qword ptr [r15+A]"
 jmp .inspect_scan_ins_next
.inspect_data_found:
 mov qword ptr [rax+B],1
 jmp .inspect_scan_ins_next
.inspect_jump:
 C inspection_target, "qword ptr [r15+A]"
 test rax,rax
 jz .inspect_scan_ins_next
 mov qword ptr [rax+B],1
 C require_params, "qword ptr [rax+LIST]"
 jmp .inspect_scan_ins_next
.inspect_branch:
 C inspection_target, "qword ptr [r15+B]"
 test rax,rax
 jz .inspect_second_branch
 mov qword ptr [rax+B],1
 C require_params, "qword ptr [rax+LIST]"
.inspect_second_branch:
 C inspection_target, "qword ptr [r15+C_]"
 test rax,rax
 jz .inspect_scan_ins_next
 mov qword ptr [rax+B],1
 C require_params, "qword ptr [rax+LIST]"
.inspect_scan_ins_next:
 mov r15,[r15+NEXT]
 jmp .inspect_scan_ins
.inspect_scan_next:
 cmp qword ptr [cli_block],0
 jne .inspect_blocks_output
 mov r14,[r14+NEXT]
 jmp .inspect_scan_blocks
.inspect_blocks_output:
 call v3_dependencies
 cmp qword ptr [v3_active],0
 jne .inspect_no_incoming
 cmp qword ptr [compact_view],0
 je .inspect_no_incoming
 call inspect_incoming
.inspect_no_incoming:
 C text_out, "offset j_blocks"
 mov r14,[r13+BODY]
 cmp qword ptr [v3_active],0
 je .inspect_v3_blocks_done
 xor r14d,r14d
.inspect_v3_blocks_done:
 xor r15d,r15d
.inspect_blocks:
 test r14,r14
 jz .inspect_callees_start
 cmp qword ptr [r14+B],0
 je .inspect_block_next
 test r15,r15
 jz .inspect_block_first
 C text_out, "offset s_comma"
.inspect_block_first:
 inc r15
 C text_out, "offset j_name"
 C json_string, "qword ptr [r14+NAME]"
 C text_out, "offset j_params"
 C json_params, "qword ptr [r14+LIST]"
 C text_out, "offset s_rbrace"
.inspect_block_next:
 mov r14,[r14+NEXT]
 jmp .inspect_blocks
.inspect_callees_start:
 C text_out, "offset s_rbracket"
 C text_out, "offset j_callees"
 jmp .inspect_signatures
.inspect_whole:
 C text_out, "offset j_inspect_functions"
.inspect_signatures:
 mov r14,[functions]
 xor r15d,r15d
.inspect_sigs:
 test r14,r14
 jz .inspect_types_start
 cmp qword ptr [cli_module],0
 jne .inspect_sig_include
 cmp qword ptr [r14+B],0
 je .inspect_sig_next
.inspect_sig_include:
 test r15,r15
 jz .inspect_sig_first
 C text_out, "offset s_comma"
.inspect_sig_first:
 inc r15
 C json_signature, r14
.inspect_sig_next:
 mov r14,[r14+NEXT]
 jmp .inspect_sigs
.inspect_types_start:
 C text_out, "offset s_rbracket"
 C text_out, "offset j_types"
 mov r14,[structs]
 xor r15d,r15d
.inspect_types:
 test r14,r14
 jz .inspect_data_start
 mov rbx,[r14+TYPE]
 cmp qword ptr [cli_module],0
 jne .inspect_type_include
 cmp qword ptr [rbx+D],0
 je .inspect_type_next
.inspect_type_include:
 test r15,r15
 jz .inspect_type_first
 C text_out, "offset s_comma"
.inspect_type_first:
 inc r15
 C text_out, "offset j_name"
 C json_string, "qword ptr [rbx+NAME]"
 C text_out, "offset j_fields"
 C json_params, "qword ptr [rbx+LIST]"
 C text_out, "offset s_rbrace"
.inspect_type_next:
 mov r14,[r14+NEXT]
 jmp .inspect_types
.inspect_data_start:
 C text_out, "offset s_rbracket"
 C text_out, "offset j_data"
 mov r14,[data_nodes]
 xor r15d,r15d
.inspect_data_loop:
 test r14,r14
 jz .inspect_ops_start
 cmp qword ptr [cli_module],0
 jne .inspect_data_include
 cmp qword ptr [r14+B],0
 je .inspect_data_next
.inspect_data_include:
 test r15,r15
 jz .inspect_data_first
 C text_out, "offset s_comma"
.inspect_data_first:
 inc r15
 C text_out, "offset j_name"
 C json_string, "qword ptr [r14+NAME]"
 C fprintf, "qword ptr [jout]", "offset j_data_bytes", "qword ptr [r14+SIZE]"
 cmp qword ptr [compact_view],0
 jne .inspect_data_close
 C text_out, "offset j_source"
 C source_slice, r14
 C json_string, rax
.inspect_data_close:
 C text_out, "offset s_rbrace"
.inspect_data_next:
 mov r14,[r14+NEXT]
 jmp .inspect_data_loop
.inspect_ops_start:
 C text_out, "offset s_rbracket"
 cmp qword ptr [cli_module],0
 jne .inspect_end
 cmp qword ptr [compact_view],0
 jne .inspect_end
 C output_instructions, "qword ptr [rbp-48]"
.inspect_end:
 cmp qword ptr [compact_view],0
 je .inspect_no_unresolved
 call inspect_unresolved
.inspect_no_unresolved:
 call library_inspection
 C text_out, "offset j_output_end"
 RETURN

FUNC output_instructions
 mov [rbp-48],rdi
 C text_out, "offset j_instructions"
 xor r14d,r14d
 xor r15d,r15d
.inspect_ops:
 bt qword ptr [rbp-48],r14
 jnc .inspect_op_next
 test r15,r15
 jz .inspect_op_first
 C text_out, "offset s_comma"
.inspect_op_first:
 inc r15
 imul rbx,r14,40
 C json_string, "qword ptr [ops+rbx]"
 C text_out, "offset s_colon"
 C text_out, "offset j_instruction_syntax"
 mov rax,[ops+rbx+8]
 C json_string, "qword ptr [syntax_names+rax*8]"
 C text_out, "offset j_instruction_result"
 C json_bool, "qword ptr [ops+rbx+16]"
 C text_out, "offset j_instruction_rule"
 mov rax,[ops+rbx+24]
 C json_string, "qword ptr [rule_names+rax*8]"
 C text_out, "offset j_instruction_inference"
 mov rax,[ops+rbx+24]
 C json_string, "qword ptr [inference_descriptions+rax*8]"
 C text_out, "offset j_instruction_description"
 C json_string, "qword ptr [op_descriptions+r14*8]"
 C text_out, "offset j_instruction_terminator"
 C json_bool, "qword ptr [ops+rbx+32]"
 C text_out, "offset s_rbrace"
.inspect_op_next:
 inc r14
 cmp r14,OP_COUNT
 jb .inspect_ops
 C text_out, "offset s_rbrace"
 RETURN

FUNC describe_ops
 mov r12,[cli_describe_ops]
 test r12,r12
 jz usage_error
 xor r13d,r13d
.describe_names:
 xor r14d,r14d
.describe_find:
 imul r15,r14,40
 C strcmp, "qword ptr [r12+NAME]", "qword ptr [ops+r15]"
 test eax,eax
 jz .describe_found
 inc r14
 cmp r14,OP_COUNT
 jb .describe_find
 mov rax,[r12+NAME]
 mov [diag_actual],rax
 FAIL e_opcode,m_opcode
.describe_found:
 bts r13,r14
 mov r12,[r12+NEXT]
 test r12,r12
 jnz .describe_names
 C text_out, "offset j_ok"
 C output_instructions, r13
 C text_out, "offset j_output_end"
 RETURN
STR j_ok,"{\"ok\":true"

.bss
inspection_validation: .zero 8
inspection_validation_size: .zero 8
inspection_unverified: .zero 8
inspection_function: .zero 8
inspection_missing: .zero 8
.text
STR j_validation,",\"validation\":"
STR word_function,"function"
STR word_data,"data"
STR word_block,"block"
STR j_unresolved,",\"unresolved\":["
STR j_missing_kind,"{\"kind\":"
STR j_missing_name,",\"name\":"
STR j_incoming,",\"incoming\":["

# Capture validation independently; reparse on failure before structural lookup.
FUNC inspect_validation
 C open_memstream, "offset inspection_validation", "offset inspection_validation_size"
 test rax,rax
 jz .read_bad
 mov [diagnostic_capture],rax
 mov [jout],rax
 C __sigsetjmp, "offset diagnostic_jump", 0
 test eax,eax
 jnz .inspect_invalid
 call verify
 C text_out, "offset j_ok"
 C text_out, "offset j_output_end"
 jmp .inspect_validation_close
.inspect_invalid:
 mov qword ptr [inspection_unverified],1
.inspect_validation_close:
 C fclose, "qword ptr [diagnostic_capture]"
 mov qword ptr [diagnostic_capture],0
 mov rax,[inspection_validation]
 mov rdx,[inspection_validation_size]
 test rdx,rdx
 jz .inspect_validation_stream_closed
 cmp byte ptr [rax+rdx-1],10
 jne .inspect_validation_stream_closed
 mov byte ptr [rax+rdx-1],0
.inspect_validation_stream_closed:
 mov rax,[stdout]
 mov [jout],rax
 cmp qword ptr [inspection_unverified],0
 je .inspect_validated
 mov qword ptr [cur_fn],0
 mov qword ptr [cur_block],0
 mov qword ptr [cur_ins],0
 mov qword ptr [diag_reg],0
 mov qword ptr [diag_actual],0
 mov qword ptr [diag_expected],0
 call parse_module
.inspect_validated:
 RETURN

FUNC unresolved_dependency
 mov r12,rdi
 mov r13,rsi
 mov r14,[inspection_missing]
.missing_find:
 test r14,r14
 jz .missing_new
 cmp [r14+TYPE],r12
 jne .missing_next
 C strcmp, "qword ptr [r14+NAME]", r13
 test eax,eax
 jz .missing_done
.missing_next:
 mov r14,[r14+NEXT]
 jmp .missing_find
.missing_new:
 call node
 mov [rax+TYPE],r12
 mov [rax+NAME],r13
 C append, "offset inspection_missing", rax
.missing_done:
 RETURN

FUNC inspection_target
 mov r12,rdi
 cmp qword ptr [inspection_unverified],0
 jne .inspection_raw_target
 mov rax,[r12+TYPE]
 RETURN
.inspection_raw_target:
 mov rax,[inspection_function]
 C find, "qword ptr [rax+BODY]", "qword ptr [r12+NAME]"
 test rax,rax
 jnz .inspection_target_end
 C unresolved_dependency, "offset word_block", "qword ptr [r12+NAME]"
 xor eax,eax
.inspection_target_end:
 RETURN

FUNC inspect_unresolved
 call v3_unresolved
 mov r12,[types]
.inspect_missing_types:
 test r12,r12
 jz .inspect_missing_output
 cmp qword ptr [r12+D],0
 je .inspect_missing_type_next
 cmp qword ptr [r12+FLAGS],TY_STRUCT
 jne .inspect_missing_type_next
 cmp qword ptr [r12+B],0
 jne .inspect_missing_type_next
 C unresolved_dependency, "offset word_type", "qword ptr [r12+NAME]"
.inspect_missing_type_next:
 mov r12,[r12+NEXT]
 jmp .inspect_missing_types
.inspect_missing_output:
 C text_out, "offset j_unresolved"
 mov r12,[inspection_missing]
.inspect_missing_loop:
 test r12,r12
 jz .inspect_missing_end
 C text_out, "offset j_missing_kind"
 C json_string, "qword ptr [r12+TYPE]"
 C text_out, "offset j_missing_name"
 C json_string, "qword ptr [r12+NAME]"
 C text_out, "offset s_rbrace"
 mov r12,[r12+NEXT]
 test r12,r12
 jz .inspect_missing_end
 C text_out, "offset s_comma"
 jmp .inspect_missing_loop
.inspect_missing_end:
 C text_out, "offset s_rbracket"
 RETURN

FUNC inspect_incoming
 C text_out, "offset j_incoming"
 cmp qword ptr [cli_block],0
 je .incoming_end
 mov rax,[inspection_function]
 mov r12,[rax+BODY]
 xor r14d,r14d
.incoming_blocks:
 test r12,r12
 jz .incoming_end
 mov r13,[r12+BODY]
 test r13,r13
 jz .incoming_next
.incoming_last:
 mov rax,[r13+NEXT]
 test rax,rax
 jz .incoming_test
 mov r13,rax
 jmp .incoming_last
.incoming_test:
 cmp qword ptr [r13+FLAGS],OP_jump
 je .incoming_jump
 cmp qword ptr [r13+FLAGS],OP_branch
 jne .incoming_next
 mov rax,[r13+B]
 C strcmp, "qword ptr [rax+NAME]", "qword ptr [cli_block]"
 test eax,eax
 jz .incoming_found
 mov rax,[r13+C_]
 jmp .incoming_compare
.incoming_jump:
 mov rax,[r13+A]
.incoming_compare:
 C strcmp, "qword ptr [rax+NAME]", "qword ptr [cli_block]"
 test eax,eax
 jnz .incoming_next
.incoming_found:
 mov qword ptr [r12+B],1
 C require_params, "qword ptr [r12+LIST]"
 test r14,r14
 jz .incoming_first
 C text_out, "offset s_comma"
.incoming_first:
 inc r14
 C text_out, "offset j_name"
 C json_string, "qword ptr [r12+NAME]"
 C text_out, "offset j_source"
 C source_slice, r13
 C json_string, rax
 C text_out, "offset s_rbrace"
.incoming_next:
 mov r12,[r12+NEXT]
 jmp .incoming_blocks
.incoming_end:
 C text_out, "offset s_rbracket"
 RETURN

FUNC source_revision
 C alloc,65
 C lm0_sha256, "qword ptr [source]", "qword ptr [source_len]", rax
 test rax,rax
 jz .alloc_bad
 RETURN

FUNC json_revision
 call source_revision
 mov r12,rax
 C text_out, "offset j_revision"
 C json_string, r12
 RETURN

.section .rodata
STR word_none, "none"
STR word_literal, "literal"
STR word_unary, "unary"
STR word_binary, "binary"
STR word_ternary, "ternary"
STR word_call, "call"
STR word_type, "type"
STR word_type_count, "type_count"
STR word_type_reg, "type_reg"
STR word_field, "field"
STR word_symbol, "symbol"
STR word_target, "target"
STR word_branch, "branch"
STR word_optional_reg, "optional_reg"
syntax_names: .quad word_none,word_literal,word_unary,word_binary,word_ternary,word_call,word_type,word_type_count,word_type_reg,word_field,word_symbol,word_target,word_branch,word_optional_reg
STR word_constant, "constant"
STR word_null, "null"
STR word_numeric, "numeric"
STR word_integer, "integer"
STR word_equality, "equality"
STR word_ordered, "ordered"
STR word_negate, "negate"
STR word_complement, "complement"
STR word_cast, "cast"
STR word_ptrtoint, "ptrtoint"
STR word_inttoptr, "inttoptr"
STR word_stack, "stack"
STR word_alloc, "alloc"
STR word_free, "free"
STR word_offset, "offset"
STR word_load, "load"
STR word_store, "store"
STR word_copy, "copy"
STR word_size, "size"
STR word_address, "address"
STR word_jump, "jump"
STR word_return, "return"
STR word_trap, "trap"
rule_names: .quad word_constant,word_null,word_numeric,word_integer,word_equality,word_ordered,word_negate,word_complement,word_cast,word_ptrtoint,word_inttoptr,word_call,word_stack,word_alloc,word_free,word_offset,word_field,word_load,word_store,word_copy,word_size,word_address,word_jump,word_branch,word_return,word_trap
.text

FUNC replace_source
 cmp qword ptr [v3_active],0
 je .replace_legacy
 call v3_replace
 RETURN
.replace_legacy:
 cmp qword ptr [cli_output],0
 je usage_error
 cmp qword ptr [cli_replacement],0
 je usage_error
 cmp qword ptr [cli_revision],0
 je .replace_revision_ok
 call source_revision
 C strcmp, rax, "qword ptr [cli_revision]"
 test eax,eax
 jnz .stale_revision
.replace_revision_ok:
 mov rax,[module_version]
 mov [rbp-128],rax
 call select_source
 mov r12,rax
 mov [rbp-48],rdx
 mov r13,[source]
 mov [rbp-56],r13
 mov rax,[source_len]
 mov [rbp-64],rax
 mov rax,[functions]
 mov [rbp-72],rax
 mov rax,[r12+TOKEN]
 mov rax,[rax+TS]
 mov [rbp-80],rax
 mov rax,[r12+END]
 mov rax,[rax+TE]
 mov [rbp-88],rax
 C read_file, "qword ptr [cli_replacement]", "qword ptr [cfg_source]"
 mov r14,rax
 mov r15,rdx
 mov [rbp-96],r14
 mov [rbp-104],r15
 # Validate the fragment's declaration boundary independently of the original.
 lea rdi,[rbp-112]
 cmp qword ptr [cli_block],0
 jne .replace_block_fragment
 C asprintf, rdi, "offset fragment_function", "qword ptr [rbp-128]", r14
 jmp .replace_fragment_ready
.replace_block_fragment:
 C asprintf, rdi, "offset fragment_block", "qword ptr [rbp-128]", r14
.replace_fragment_ready:
 cmp eax,0
 jl .alloc_bad
 mov [source_len],rax
 mov rax,[rbp-112]
 mov [source],rax
 call parse_module
 cmp qword ptr [structs],0
 jne replace_error
 cmp qword ptr [data_nodes],0
 jne replace_error
 mov rax,[functions]
 test rax,rax
 jz replace_error
 cmp qword ptr [rax+NEXT],0
 jne replace_error
 cmp qword ptr [cli_block],0
 jne .replace_fragment_block
 C strcmp, "qword ptr [rax+NAME]", "qword ptr [cli_function]"
 test eax,eax
 jnz replace_error
 jmp .replace_join
.replace_fragment_block:
 mov rax,[rax+BODY]
 test rax,rax
 jz replace_error
 cmp qword ptr [rax+NEXT],0
 jne replace_error
 C strcmp, "qword ptr [rax+NAME]", "qword ptr [cli_block]"
 test eax,eax
 jnz replace_error
.replace_join:
 mov rdi,[rbp-64]
 sub rdi,[rbp-88]
 add rdi,[rbp-80]
 add rdi,r15
 inc rdi
 cmp rdi,[cfg_source]
 ja .read_limit
 mov [rbp-120],rdi
 inc rdi
 call alloc
 mov rbx,rax
 C memcpy, rbx, r13, "qword ptr [rbp-80]"
 mov rdi,rbx
 add rdi,[rbp-80]
 C memcpy, rdi, r14, r15
 mov rdi,rbx
 add rdi,[rbp-80]
 add rdi,r15
 mov byte ptr [rdi],10
 inc rdi
 mov rsi,r13
 add rsi,[rbp-88]
 mov rdx,[rbp-64]
 sub rdx,[rbp-88]
 call memcpy
 mov [source],rbx
 mov rax,[rbp-120]
 mov [source_len],rax
 call parse_module
 call verify
 mov r13,[rbp-72]
 mov r14,[functions]
.replace_identities:
 test r13,r13
 jz .replace_identity_end
 test r14,r14
 jz replace_error
 C strcmp, "qword ptr [r13+NAME]", "qword ptr [r14+NAME]"
 test eax,eax
 jnz replace_error
 mov r13,[r13+NEXT]
 mov r14,[r14+NEXT]
 jmp .replace_identities
.replace_identity_end:
 test r14,r14
 jnz replace_error
 cmp qword ptr [cli_block],0
 je .replace_write
 call select_source
 mov r13,[rdx+BODY]
 mov rax,[rbp-48]
 mov r14,[rax+BODY]
.replace_block_identities:
 test r13,r13
 jz .replace_blocks_end
 test r14,r14
 jz replace_error
 C strcmp, "qword ptr [r13+NAME]", "qword ptr [r14+NAME]"
 test eax,eax
 jnz replace_error
 mov r13,[r13+NEXT]
 mov r14,[r14+NEXT]
 jmp .replace_block_identities
.replace_blocks_end:
 test r14,r14
 jnz replace_error
.replace_write:
 call write_source
 RETURN

FUNC write_source
 C prepare_output, "qword ptr [cli_output]"
 C fopen, "qword ptr [temp_assembly]", "offset mode_wb"
 test rax,rax
 jz .read_bad
 mov r12,rax
 C fwrite, "qword ptr [source]", 1, "qword ptr [source_len]", r12
 cmp rax,[source_len]
 jne .read_bad
 C fclose, r12
 test eax,eax
 jnz .read_bad
 C rename, "qword ptr [temp_assembly]", "qword ptr [cli_output]"
 test eax,eax
 jnz .read_bad
 RETURN
.stale_revision:
 FAIL e_stale,m_stale
STR e_stale,"E_STALE"
STR m_stale,"Source revision changed; inspect the current source before replacing"
replace_error:
 FAIL e_replace, m_replace
STR m_replace, "Replacement must contain exactly the selected declaration and preserve declaration identities"
STR fragment_function, "module fragment version %lu\n%s\n"
STR fragment_block, "module fragment version %lu\nfn @fragment() -> void {\n%s\n}\n"
.v3_block_error:
 FAIL e_unsupported,v3_block_message
STR v3_block_message,"V3 repair selects a function; generated blocks are internal"
