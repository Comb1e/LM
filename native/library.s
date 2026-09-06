.include "build/stdlib/catalog.inc"
.bss
library_used: .zero 8
library_importing: .zero 8
.text

FUNC library_find
 mov r12,rdi
 xor ebx,ebx
.library_find_loop:
 mov rax,rbx
 shl rax,5
 C strcmp,r12,"qword ptr [library_catalog+rax]"
 test eax,eax
 jz .library_found
 inc rbx
 cmp rbx,LIBRARY_COUNT
 jb .library_find_loop
 mov [diag_actual],r12
 FAIL library_error,library_unknown
.library_found:
 mov rax,rbx
 shl rax,5
 add rax,offset library_catalog
 mov rdx,rbx
 RETURN

# Parse imported declarations through the ordinary parser, retaining root-source
# spans and restoring its lexer cursor. Generated interfaces have no bodies.
FUNC library_import
 mov r13,rsi
 mov [diag_tok],rsi
 call library_find
 bt qword ptr [library_used],rdx
 jc .library_import_done
 bts qword ptr [library_used],rdx
 mov r12,rax
 .set save_slot,48
 .irp var,source,source_len,tokens,tok,last_tok,cur_fn,cur_block,diag_tok,library_importing
 mov rax,[\var]
 mov [rbp-save_slot],rax
 .set save_slot,save_slot+8
 .endr
 mov [library_importing],r12
 mov rax,[r12+8]
 mov [source],rax
 C strlen,rax
 mov [source_len],rax
 call lex
 mov r14,[tokens]
.library_remap:
 test r14,r14
 jz .library_parse
 .irp off,TS,TE,TL,TC,TEL,TEC
 mov rax,[r13+\off]
 mov [r14+\off],rax
 .endr
 mov r14,[r14+NEXT]
 jmp .library_remap
.library_parse:
 call lines
 call parse_declarations
 .set save_slot,48
 .irp var,source,source_len,tokens,tok,last_tok,cur_fn,cur_block,diag_tok,library_importing
 mov rax,[rbp-save_slot]
 mov [\var],rax
 .set save_slot,save_slot+8
 .endr
.library_import_done:
 RETURN

FUNC library_cli
 cmp qword ptr [cli_syntax],0
 je .library_syntax_ok
 EQ "qword ptr [cli_syntax]",library_syntax_v2
 jz .library_syntax_ok
 EQ "qword ptr [cli_syntax]",library_syntax_v3
 jnz usage_error
.library_syntax_ok:
 mov r12,[cli_describe_ops]
 test r12,r12
 jz usage_error
 EQ "qword ptr [r12+NAME]",library_list_word
 jnz .library_describe_cli
 cmp qword ptr [r12+NEXT],0
 jne usage_error
 C puts,"offset library_listing"
 RETURN
.library_describe_cli:
 EQ "qword ptr [r12+NAME]",library_describe_word
 jnz usage_error
 mov r12,[r12+NEXT]
 test r12,r12
 jz usage_error
 C library_find,"qword ptr [r12+NAME]"
 mov r13,rax
 cmp qword ptr [cli_syntax],0
 je .library_syntax_selected
 mov r15,rdx
 EQ "qword ptr [cli_syntax]",library_syntax_v3
 jnz .library_syntax_selected
 shl r15,5
 lea r13,[library_catalog_v3+r15]
.library_syntax_selected:
 mov r12,[r12+NEXT]
 test r12,r12
 jnz .library_select_apis
 C puts,"qword ptr [r13+16]"
 RETURN
.library_select_apis:
 # Validate every requested name before emitting any JSON.
 mov r14,r12
.library_validate_api:
 test r14,r14
 jz .library_apis_start
 C library_api,r13,"qword ptr [r14+NAME]"
 test rax,rax
 jz .library_symbol_error
 mov r14,[r14+NEXT]
 jmp .library_validate_api
.library_apis_start:
 C text_out,"offset library_apis_json"
 C json_string,"offset library_identity"
 C text_out,"offset library_policy_json"
 C json_string,"offset library_policy"
 C text_out,"offset library_functions_json"
 xor r15d,r15d
.library_print_api:
 test r12,r12
 jz .library_apis_end
 test r15,r15
 jz .library_api_first
 C text_out,"offset s_comma"
.library_api_first:
 inc r15
 C library_api,r13,"qword ptr [r12+NAME]"
 C text_out,rax
 mov r12,[r12+NEXT]
 jmp .library_print_api
.library_apis_end:
 C text_out,"offset library_array_end"
 RETURN

FUNC library_api
 cmp qword ptr [v3_active],0
 je .library_api_version_ready
 mov rax,rdi
 sub rax,offset library_catalog
 cmp rax,LIBRARY_COUNT*32
 jae .library_api_version_ready
 lea rdi,[library_catalog_v3+rax]
.library_api_version_ready:
 mov r12,[rdi+24]
 mov r13,rsi
 cmp byte ptr [r13],64
 jne .library_api_loop
 inc r13
.library_api_loop:
 cmp qword ptr [r12],0
 je .library_api_absent
 C strcmp,r13,"qword ptr [r12]"
 test eax,eax
 jz .library_api_found
 add r12,16
 jmp .library_api_loop
.library_api_found:
 mov rax,[r12+8]
 RETURN
.library_api_absent:
 xor eax,eax
 RETURN

FUNC library_inspection
 cmp qword ptr [library_used],0
 je .library_inspection_done
 C text_out,"offset library_context_json"
 C json_string,"offset library_identity"
 C text_out,"offset library_policy_json"
 C json_string,"offset library_policy"
 C text_out,"offset library_functions_json"
 mov r12,[functions]
 xor r15d,r15d
.library_context_loop:
 test r12,r12
 jz .library_context_end
 cmp qword ptr [r12+ORIGIN],0
 je .library_context_next
 cmp qword ptr [cli_module],0
 jne .library_context_include
 cmp qword ptr [r12+B],0
 je .library_context_next
.library_context_include:
 test r15,r15
 jz .library_context_first
 C text_out,"offset s_comma"
.library_context_first:
 inc r15
 C library_api,"qword ptr [r12+ORIGIN]","qword ptr [r12+NAME]"
 C text_out,rax
.library_context_next:
 mov r12,[r12+NEXT]
 jmp .library_context_loop
.library_context_end:
 C text_out,"offset library_context_end_json"
.library_inspection_done:
 RETURN

FUNC library_link_metadata
 cmp qword ptr [library_used],0
 je .library_link_metadata_done
 C text_out,"offset library_link_json"
 C json_string,"offset library_identity"
 C text_out,"offset library_link_end_json"
.library_link_metadata_done:
 RETURN

FUNC library_archive
 mov r12,[cli_stdlib_dir]
 test r12,r12
 jnz .library_path_ready
 C alloc,4096
 mov r13,rax
 C readlink,"offset library_proc_exe",r13,4095
 test rax,rax
 js .read_bad
 cmp rax,4095
 jae .read_bad
 mov byte ptr [r13+rax],0
 C strrchr,r13,47
 test rax,rax
 jz .read_bad
 mov byte ptr [rax],0
 lea rdi,[rbp-48]
 C asprintf,rdi,"offset library_local_dir",r13
 mov r12,[rbp-48]
 lea rdi,[rbp-56]
 C asprintf,rdi,"offset library_id_path",r12
 C access,"qword ptr [rbp-56]",4
 test eax,eax
 jz .library_path_ready
 lea rdi,[rbp-48]
 C asprintf,rdi,"offset library_installed_dir",r13
 mov r12,[rbp-48]
.library_path_ready:
 lea rdi,[rbp-56]
 C asprintf,rdi,"offset library_id_path",r12
 C access,"qword ptr [rbp-56]",4
 test eax,eax
 jnz .library_install_error
 C read_file,"qword ptr [rbp-56]",128
 C trim,rax
 C strcmp,rax,"offset library_identity"
 test eax,eax
 jnz .library_install_error
 lea rdi,[rbp-64]
 C asprintf,rdi,"offset library_archive_path",r12
 C access,"qword ptr [rbp-64]",4
 test eax,eax
 jnz .library_install_error
 mov rax,[rbp-64]
 RETURN
.library_install_error:
 FAIL library_error,library_install_message
.library_symbol_error:
 FAIL library_error,library_symbol_message
.library_source_error:
 FAIL library_error,library_source_message
STR library_command,"library"
STR library_syntax_v2,"v2"
STR library_syntax_v3,"v3"
STR library_use_word,"use"
STR library_list_word,"list"
STR library_describe_word,"describe"
STR library_error,"E_LIBRARY"
STR library_unknown,"Unknown standard library module"
STR library_install_message,"Missing or incompatible standard library; run make stdlib or set --stdlib-dir"
STR library_symbol_message,"Unknown library symbol; use its full std_MODULE_FUNCTION name"
STR library_source_message,"Imported declarations have no editable source; use library describe"
STR library_proc_exe,"/proc/self/exe"
STR library_local_dir,"%s/stdlib"
STR library_installed_dir,"%s/../lib/lm0"
STR library_id_path,"%s/catalog.id"
STR library_archive_path,"%s/liblm0std.a"
STR library_math_link,"-lm"
STR library_hide_link,"-Wl,--exclude-libs,liblm0std.a"
STR library_apis_json,"{\"ok\":true,\"catalogue\":"
STR library_functions_json,",\"functions\":["
STR library_array_end,"]}\n"
STR library_context_json,",\"library\":{\"catalogue\":"
STR library_context_end_json,"]}"
STR library_policy_json,",\"policy\":"
STR library_link_json,",\"link_requirements\":{\"catalogue\":"
STR library_link_end_json,",\"archives\":[\"liblm0std.a\"],\"libraries\":[\"m\"]}"
STR library_abi_reference,".section .data.rel.ro\n.quad std_abi_%s\n"
