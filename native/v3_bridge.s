.global source,source_len,filename,cli_block,cli_function,cli_output
.global cli_replacement,cli_revision,diag_phase,diag_tok,tokens,jout
.global cfg_source,cfg_type_depth,fail,json_string
.global cfg_v3_expansion,functions,data_nodes
.global parse_module,verify,write_source
.global unresolved_dependency

.global v3_catalog_source
FUNC v3_catalog_source
 mov r12,rdi
 xor ebx,ebx
.v3_catalog_loop:
 mov r13,rbx
 shl r13,5
 C strcmp,r12,"qword ptr [library_catalog+r13]"
 test eax,eax
 jz .v3_catalog_found
 inc rbx
 cmp rbx,LIBRARY_COUNT
 jb .v3_catalog_loop
 xor eax,eax
 RETURN
.v3_catalog_found:
 mov rax,[library_catalog+r13+8]
 RETURN
