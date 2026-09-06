.bss
migration_edits: .zero 8
.text
# Edits use byte offsets; sorted insertion also rejects accidental overlap.
FUNC migration_edit
 mov r12,rdi
 mov r13,rsi
 mov r14,rdx
 call node
 mov [rax+A],r12
 mov [rax+B],r13
 mov [rax+NAME],r14
 mov r15,rax
 mov rbx,offset migration_edits
.edit_find:
 mov rax,[rbx]
 test rax,rax
 jz .edit_insert
 cmp [rax+A],r12
 jae .edit_insert
 cmp [rax+B],r12
 ja .migration_overlap
 mov rbx,rax
 jmp .edit_find
.edit_insert:
 test rax,rax
 jz .edit_link
 cmp r13,[rax+A]
 ja .migration_overlap
.edit_link:
 mov [r15+NEXT],rax
 mov [rbx],r15
 RETURN
.migration_overlap:
 FAIL e_tool,m_migration_overlap
STR m_migration_overlap,"Overlapping migration edits"

FUNC migrate_source
 cmp qword ptr [v3_active],0
 jne unsupported_error
 cmp qword ptr [cli_output],0
 je usage_error
 mov qword ptr [migration_edits],0
 mov r12,[tokens]
.migrate_version:
 EQ "qword ptr [r12+TX]",s_version
 jz .migrate_version_found
 mov r12,[r12+NEXT]
 jmp .migrate_version
.migrate_version_found:
 mov rax,[r12+NEXT]
 C migration_edit, "qword ptr [rax+TS]", "qword ptr [rax+TE]", "offset v2_text"
 mov r12,[functions]
.migrate_functions:
 test r12,r12
 jz .migrate_write
 mov r13,[r12+BODY]
.migrate_blocks:
 test r13,r13
 jz .migrate_next_fn
 mov r14,[r13+BODY]
.migrate_instructions:
 test r14,r14
 jz .migrate_next_block
 mov r15,[r14+TYPE]
 test r15,r15
 jz .migrate_next_ins
 cmp qword ptr [r14+FLAGS],OP_const
 je .migrate_constant
 cmp qword ptr [r14+FLAGS],OP_null
 je .migrate_constant
 cmp qword ptr [r14+FLAGS],OP_cast
 je .migrate_next_ins
 cmp qword ptr [r14+FLAGS],OP_inttoptr
 je .migrate_next_ins
 mov rax,[r15+TOKEN]
 mov rdx,[r15+END]
 C migration_edit, "qword ptr [rax+TE]", "qword ptr [rdx+TE]", "offset s_empty"
 jmp .migrate_next_ins
.migrate_constant:
 mov qword ptr [rbp-48],0
 mov qword ptr [rbp-56],0
 mov rbx,[r13+TOKEN]
.migrate_uses:
 cmp qword ptr [rbx+TK],K_REF
 jne .migrate_use_next
 mov rax,[rbx+TX]
 cmp byte ptr [rax],37
 jne .migrate_use_next
 inc rax
 C strcmp, rax, "qword ptr [r15+NAME]"
 test eax,eax
 jnz .migrate_use_next
 inc qword ptr [rbp-48]
 cmp rbx,[r15+TOKEN]
 je .migrate_use_next
 mov [rbp-56],rbx
.migrate_use_next:
 cmp rbx,[r13+END]
 je .migrate_uses_end
 mov rbx,[rbx+NEXT]
 test rbx,rbx
 jnz .migrate_uses
.migrate_uses_end:
 cmp qword ptr [rbp-48],2
 jne .migrate_next_ins
 mov rbx,[rbp-56]
 mov rdx,[r14+A]
 cmp qword ptr [r14+FLAGS],OP_null
 jne .migrate_literal_text
 mov rdx,offset op_name_1
.migrate_literal_text:
 mov rax,[r15+TYPE]
 lea rdi,[rbp-64]
 C asprintf, rdi, "offset typed_literal_text", rdx, "qword ptr [rax+NAME]"
 test eax,eax
 js .alloc_bad
 C migration_edit, "qword ptr [rbx+TS]", "qword ptr [rbx+TE]", "qword ptr [rbp-64]"
 mov rax,[r14+TOKEN]
 mov rdx,[r14+END]
 mov rdi,[rax+TS]
 mov rsi,[rdx+TE]
 mov rcx,[source]
.migrate_line_start:
 test rdi,rdi
 jz .migrate_line_end
 cmp byte ptr [rcx+rdi-1],10
 je .migrate_line_end
 dec rdi
 jmp .migrate_line_start
.migrate_line_end:
 mov al,[rcx+rsi]
 cmp al,32
 je .migrate_line_space
 cmp al,9
 je .migrate_line_space
 cmp al,13
 je .migrate_line_space
 cmp al,10
 jne .migrate_line_edit
 inc rsi
 jmp .migrate_line_edit
.migrate_line_space:
 inc rsi
 jmp .migrate_line_end
.migrate_line_edit:
 C migration_edit, rdi, rsi, "offset s_empty"
.migrate_next_ins:
 mov r14,[r14+NEXT]
 jmp .migrate_instructions
.migrate_next_block:
 mov r13,[r13+NEXT]
 jmp .migrate_blocks
.migrate_next_fn:
 mov r12,[r12+NEXT]
 jmp .migrate_functions
.migrate_write:
 lea rdi,[rbp-72]
 lea rsi,[rbp-80]
 call open_memstream
 test rax,rax
 jz .read_bad
 mov r12,rax
 mov r13,[migration_edits]
 xor r14d,r14d
.migrate_pieces:
 test r13,r13
 jz .migrate_tail
 mov rdi,[source]
 add rdi,r14
 mov rdx,[r13+A]
 sub rdx,r14
 C fwrite, rdi, 1, rdx, r12
 C fputs, "qword ptr [r13+NAME]", r12
 mov r14,[r13+B]
 mov r13,[r13+NEXT]
 jmp .migrate_pieces
.migrate_tail:
 mov rdi,[source]
 add rdi,r14
 mov rdx,[source_len]
 sub rdx,r14
 C fwrite, rdi, 1, rdx, r12
 C fclose, r12
 test eax,eax
 jnz .read_bad
 mov rax,[rbp-72]
 mov [source],rax
 mov rax,[rbp-80]
 mov [source_len],rax
 call parse_module
 call verify
 call write_source
 RETURN
STR v2_text,"2"
STR typed_literal_text,"%s:%s"
