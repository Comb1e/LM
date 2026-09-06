.macro GLOBAL name
.bss
.align 8
\name: .zero 8
.text
.endm
GLOBAL cfg_cc
GLOBAL cfg_optimization
GLOBAL cfg_target
GLOBAL cfg_build_timeout
GLOBAL cfg_source
GLOBAL cfg_type_depth
GLOBAL cfg_aggregate
GLOBAL cfg_run_timeout
GLOBAL cfg_output
GLOBAL cfg_diagnostics
GLOBAL cfg_repairs
GLOBAL cli_command
GLOBAL cli_output
GLOBAL cli_kind
GLOBAL cli_config
GLOBAL cli_function
GLOBAL cli_block
GLOBAL cli_replacement
GLOBAL cli_entry
GLOBAL cli_module
GLOBAL cli_timeout_ms
GLOBAL cli_links
GLOBAL cli_libraries
GLOBAL cli_optimization
GLOBAL cli_view
GLOBAL cli_revision
GLOBAL cli_describe_ops
GLOBAL cli_stdlib_dir
GLOBAL compact_view
GLOBAL temp_directory
GLOBAL temp_assembly
GLOBAL temp_binary
GLOBAL run_directory
GLOBAL run_binary
GLOBAL process_stdout
GLOBAL process_stderr
GLOBAL process_code
GLOBAL process_timed
GLOBAL process_limited
GLOBAL process_pid
GLOBAL process_out_len
GLOBAL process_err_len
.section .rodata
config_defaults:
.incbin "native/defaults.conf"
.byte 0
config_defaults_end:
.macro CONFIG section,key,dest,number
STR config_\dest, "\section\().\key"
.quad config_\dest,\dest,\number
.endm
config_schema:
CONFIG compiler,cc,cfg_cc,0
CONFIG compiler,optimization,cfg_optimization,0
CONFIG compiler,target,cfg_target,0
CONFIG compiler,build_timeout_seconds,cfg_build_timeout,1
CONFIG limits,source_bytes,cfg_source,1
CONFIG limits,type_depth,cfg_type_depth,1
CONFIG limits,aggregate_bytes,cfg_aggregate,1
CONFIG limits,run_timeout_seconds,cfg_run_timeout,1
CONFIG limits,output_bytes,cfg_output,1
CONFIG limits,diagnostics,cfg_diagnostics,1
CONFIG benchmark,max_repairs,cfg_repairs,1
.text

FUNC trim
 mov r12,rdi
.trim_front:
 mov al,[r12]
 cmp al,32
 je .trim_more
 cmp al,9
 je .trim_more
 cmp al,13
 je .trim_more
 cmp al,10
 jne .trim_back_start
.trim_more:
 inc r12
 jmp .trim_front
.trim_back_start:
 C strlen, r12
 lea r13,[r12+rax]
.trim_back:
 cmp r13,r12
 je .trim_done
 mov al,[r13-1]
 cmp al,32
 je .trim_less
 cmp al,9
 je .trim_less
 cmp al,13
 je .trim_less
 cmp al,10
 jne .trim_done
.trim_less:
 dec r13
 mov byte ptr [r13],0
 jmp .trim_back
.trim_done:
 mov rax,r12
 RETURN

FUNC load_config_text
 mov r12,rdi
 mov r13,offset s_empty
 mov qword ptr [rbp-48],0
 mov qword ptr [rbp-56],0
.config_line:
 lea rdx,[rbp-48]
 C strtok_r, r12, "offset s_nl", rdx
 xor r12d,r12d
 test rax,rax
 jz .config_done
 C trim, rax
 mov r14,rax
 cmp byte ptr [r14],0
 je .config_line
 cmp byte ptr [r14],35
 je .config_line
 cmp byte ptr [r14],91
 je .config_section
 C strchr, r14, 61
 test rax,rax
 jz config_error
 mov byte ptr [rax],0
 lea rdi,[rax+1]
 call trim
 mov r15,rax
 C trim, r14
 mov r14,rax
 lea rdi,[rbp-64]
 C asprintf, rdi, "offset fmt_key", r13, r14
 cmp eax,0
 jl .alloc_bad
 xor ebx,ebx
.config_find:
 imul rax,rbx,24
 C strcmp, "qword ptr [rbp-64]", "qword ptr [config_schema+rax]"
 test eax,eax
 jz .config_found
 inc rbx
 cmp rbx,11
 jb .config_find
 jmp config_error
.config_found:
 bts qword ptr [rbp-56],rbx
 jc config_error
 imul rax,rbx,24
 mov r14,[config_schema+rax+8]
 cmp qword ptr [config_schema+rax+16],0
 jne .config_number
 movzx ebx,byte ptr [r15]
 cmp ebx,34
 je .config_string
 cmp ebx,39
 jne config_error
.config_string:
 lea rdi,[r15+1]
 C strchr, rdi, rbx
 test rax,rax
 jz config_error
 mov [rbp-72],rax
 lea rdi,[rax+1]
 call trim
 cmp byte ptr [rax],0
 je .config_string_end
 cmp byte ptr [rax],35
 jne config_error
.config_string_end:
 mov rax,[rbp-72]
 mov byte ptr [rax],0
 lea r15,[r15+1]
 C strchr, r15, 92
 test rax,rax
 jnz config_error
 cmp byte ptr [r15],0
 je config_error
 mov [r14],r15
 jmp .config_line
.config_number:
 C strchr, r15, 35
 test rax,rax
 jz .config_no_comment
 mov byte ptr [rax],0
.config_no_comment:
 C trim, r15
 C integer_text, rax
 test rdx,rdx
 jnz config_error
 test rax,rax
 jle config_error
 mov rdx,2147483647
 cmp rax,rdx
 ja config_error
 mov [r14],rax
 jmp .config_line
.config_section:
 lea rdi,[r14+1]
 C strchr, rdi, 93
 test rax,rax
 jz config_error
 mov byte ptr [rax],0
 lea r13,[r14+1]
 lea rdi,[rax+1]
 call trim
 cmp byte ptr [rax],0
 je .config_section_name
 cmp byte ptr [rax],35
 jne config_error
.config_section_name:
 EQ r13, c_compiler
 jz .config_line
 EQ r13, c_limits
 jz .config_line
 EQ r13, c_benchmark
 jz .config_line
 jmp config_error
.config_done:
 RETURN
config_error:
 FAIL e_tool, m_config
STR fmt_key, "%s.%s"
STR c_compiler, "compiler"
STR c_limits, "limits"
STR c_benchmark, "benchmark"

.macro OPTION spelling,destination
STR opt_\destination, "\spelling"
.quad opt_\destination,\destination
.endm
.section .rodata
cli_options:
OPTION --config,cli_config
OPTION --output,cli_output
OPTION --kind,cli_kind
OPTION --function,cli_function
OPTION --block,cli_block
OPTION --replacement,cli_replacement
OPTION --optimization,cli_optimization
OPTION --view,cli_view
OPTION --expect-revision,cli_revision
OPTION --stdlib-dir,cli_stdlib_dir
.text
option_value:
 inc r12
 cmp r12,r13
 jae usage_error
 mov rax,[r14+r12*8]
 ret

.globl main
FUNC main
 mov r13,rdi
 mov r14,rsi
 mov rax,[stdout]
 mov [jout],rax
 mov qword ptr [diag_phase],offset p_tool
 mov qword ptr [cli_kind],offset kind_exe
 C slice, "offset config_defaults", config_defaults_end-config_defaults-1
 C load_config_text, rax
 C __cxa_atexit, "offset cleanup", 0, 0
 mov r12d,1
.cli_args:
 cmp r12,r13
 jae .cli_ready
 mov r15,[r14+r12*8]
 xor ebx,ebx
.cli_option_scan:
 mov rax,rbx
 shl rax,4
 C strcmp, r15, "qword ptr [cli_options+rax]"
 test eax,eax
 jz .cli_set_option
 inc rbx
 cmp rbx,10
 jb .cli_option_scan
 EQ r15, opt_version
 jz .cli_version
 EQ r15, opt_help
 jz .cli_help
 EQ r15, opt_output
 jz .cli_output_arg
 EQ r15, opt_entry
 jz .cli_entry_arg
 EQ r15, opt_module
 jz .cli_module_arg
 EQ r15, opt_sanitize
 jz unsupported_error
 EQ r15, opt_opt
 jz .cli_opt_arg
 cmp word ptr [r15],0x4f2d
 je .cli_opt_compact
 EQ r15, opt_timeout
 jz .cli_timeout_arg
 EQ r15, opt_link
 jz .cli_link_arg
 EQ r15, opt_library
 jz .cli_library_arg
 cmp byte ptr [r15],45
 je usage_error
 cmp qword ptr [cli_command],0
 je .cli_command_arg
 EQ "qword ptr [cli_command]",cmd_describe
 jz .cli_describe_arg
 EQ "qword ptr [cli_command]",library_command
 jz .cli_describe_arg
 cmp qword ptr [filename],0
 jne usage_error
 mov [filename],r15
 jmp .cli_next
.cli_set_option:
 call option_value
 shl rbx,4
 mov rdx,[cli_options+rbx+8]
 mov [rdx],rax
 jmp .cli_next
.cli_command_arg:
 mov [cli_command],r15
 jmp .cli_next
.cli_output_arg:
 call option_value
 mov [cli_output],rax
 jmp .cli_next
.cli_entry_arg:
 mov qword ptr [cli_entry],1
 jmp .cli_next
.cli_module_arg:
 mov qword ptr [cli_module],1
 jmp .cli_next
.cli_opt_arg:
 call option_value
 mov [cli_optimization],rax
 jmp .cli_next
.cli_opt_compact:
 lea rax,[r15+2]
 mov [cli_optimization],rax
 jmp .cli_next
.cli_timeout_arg:
 call option_value
 mov r15,rax
 lea rsi,[rbp-48]
 C strtod, r15, rsi
 mov rax,[rbp-48]
 cmp byte ptr [rax],0
 jne usage_error
 cmp rax,r15
 je usage_error
 xorpd xmm1,xmm1
 ucomisd xmm0,xmm1
 jbe usage_error
 jp usage_error
 mulsd xmm0,QWORD PTR [milliseconds]
 cvttsd2si rax,xmm0
 test rax,rax
 jle usage_error
 mov [cli_timeout_ms],rax
 jmp .cli_next
.cli_link_arg:
 mov rbx,offset cli_links
 jmp .cli_list_arg
.cli_library_arg:
 mov rbx,offset cli_libraries
.cli_list_arg:
 call option_value
 mov r15,rax
 call node
 mov [rax+NAME],r15
 C append, rbx, rax
 jmp .cli_next
.cli_describe_arg:
 call node
 mov [rax+NAME],r15
 C append, "offset cli_describe_ops", rax
.cli_next:
 inc r12
 jmp .cli_args
.cli_ready:
 cmp qword ptr [cli_config],0
 je .cli_configured
 C read_file, "qword ptr [cli_config]", 1048576
 C load_config_text, rax
.cli_configured:
 cmp qword ptr [cli_view],0
 je .cli_view_ready
 EQ "qword ptr [cli_view]",view_full
 jz .cli_view_ready
 EQ "qword ptr [cli_view]",view_compact
 jnz usage_error
 mov qword ptr [compact_view],1
.cli_view_ready:
 mov rax,[cli_optimization]
 test rax,rax
 jnz .cli_have_opt
 mov rax,[cfg_optimization]
.cli_have_opt:
 EQ rax, opt_zero
 jnz unsupported_error
 EQ "qword ptr [cfg_target]", target_name
 jnz .target_error
 mov r15,[cli_kind]
 EQ r15, kind_exe
 jz .cli_valid_kind
 EQ r15, kind_object
 jz .cli_valid_kind
 EQ r15, kind_shared
 jnz usage_error
.cli_valid_kind:
 mov rax,[cli_function]
 test rax,rax
 jz .cli_strip_block
 cmp byte ptr [rax],64
 jne .cli_strip_block
 inc qword ptr [cli_function]
.cli_strip_block:
 mov rax,[cli_block]
 test rax,rax
 jz .cli_read
 cmp byte ptr [rax],94
 jne .cli_read
 inc qword ptr [cli_block]
.cli_read:
 cmp qword ptr [cli_command],0
 je usage_error
 mov r12,[cli_command]
 EQ r12,cmd_describe
 jz .cli_describe
 EQ r12,library_command
 jz .cli_library_catalog
 EQ r12, cmd_emit_c
 jz unsupported_error
 EQ r12, cmd_bench
 jz unsupported_error
 cmp qword ptr [filename],0
 je usage_error
 C read_file, "qword ptr [filename]", "qword ptr [cfg_source]"
 mov [source],rax
 mov [source_len],rdx
 call parse_module
 EQ r12, cmd_replace
 jz .cli_replace
 EQ r12,cmd_inspect
 jnz .cli_verify
 cmp qword ptr [compact_view],0
 je .cli_verify
 call inspect_validation
 jmp .cli_inspect
.cli_verify:
 call verify
 EQ r12,cmd_migrate
 jz .cli_migrate
 EQ r12, cmd_check
 jz .cli_check
 EQ r12, cmd_inspect
 jz .cli_inspect
 EQ r12, cmd_emit_asm
 jz .cli_emit
 EQ r12, cmd_build
 jz .cli_build
 EQ r12, cmd_run
 jz .cli_run
 jmp usage_error
.cli_check:
 C text_out, "offset j_check"
 C json_string, "qword ptr [module_name]"
 C list_count, "qword ptr [functions]"
 C fprintf, "qword ptr [jout]", "offset j_check_end", rax
 xor eax,eax
 RETURN
.cli_inspect:
 call inspect_module
 xor eax,eax
 RETURN
.cli_migrate:
 call migrate_source
 call print_output
 xor eax,eax
 RETURN
.cli_describe:
 call describe_ops
 xor eax,eax
 RETURN
.cli_library_catalog:
 call library_cli
 xor eax,eax
 RETURN
.cli_replace:
 call replace_source
 call print_output
 xor eax,eax
 RETURN
.cli_emit:
 cmp qword ptr [cli_output],0
 je usage_error
 C prepare_output, "qword ptr [cli_output]"
 C fopen, "qword ptr [temp_assembly]", "offset mode_wb"
 test rax,rax
 jz .read_bad
 mov r13,rax
 C emit_asm, r13, "qword ptr [cli_entry]"
 C fclose, r13
 test eax,eax
 jnz .read_bad
 C rename, "qword ptr [temp_assembly]", "qword ptr [cli_output]"
 test eax,eax
 jnz .read_bad
 call print_output
 xor eax,eax
 RETURN
.cli_build:
 cmp qword ptr [cli_output],0
 je usage_error
 C native_build, "qword ptr [cli_output]"
 C text_out, "offset j_output"
 C json_string, "qword ptr [cli_output]"
 C text_out, "offset j_kind"
 C json_string, "qword ptr [cli_kind]"
 C text_out, "offset j_build_end"
 call library_link_metadata
 C text_out, "offset j_output_end"
 xor eax,eax
 RETURN
.cli_run:
 C slice, "offset run_template", 19
 C mkdtemp, rax
 test rax,rax
 jz .read_bad
 mov [run_directory],rax
 lea rdi,[rbp-48]
 C asprintf, rdi, "offset fmt_program", rax
 cmp eax,0
 jl .alloc_bad
 mov rax,[rbp-48]
 mov [run_binary],rax
 mov qword ptr [cli_kind],offset kind_exe
 C native_build, rax
 mov rax,[run_binary]
 mov [rbp-64],rax
 mov qword ptr [rbp-56],0
 mov rsi,[cli_timeout_ms]
 test rsi,rsi
 jnz .run_deadline
 imul rsi,qword ptr [cfg_run_timeout],1000
.run_deadline:
 lea rdi,[rbp-64]
 call execute_process
 call print_execution
 RETURN
.cli_version:
 C puts, "offset version_text"
 xor eax,eax
 RETURN
.cli_help:
 C puts, "offset help_text"
 xor eax,eax
 RETURN
usage_error:
 FAIL e_tool, m_usage
unsupported_error:
 FAIL e_unsupported, m_unsupported
.target_error:
 FAIL e_target, m_target
STR version_text, "LM0 0.4.0 native x86_64-linux-gnu"
STR help_text, "lm0 [--config FILE] check|emit-asm|build|run|inspect|replace|migrate SOURCE [OPTIONS]\nOutput: -o FILE; build: --kind exe|object|shared -O0 --link FILE --library NAME\nLibraries: library list; library describe MODULE [std_MODULE_FUNCTION...]\nLibrary installation: --stdlib-dir DIR; v2 source imports: use std_MODULE\nInspection: --function NAME [--block NAME] or --module; --view compact|full\nRepair: --function NAME [--block NAME] --replacement FILE -o FILE [--expect-revision SHA256]\nMigration: migrate SOURCE -o FILE; instruction guidance: describe OP...\nEmission: --entry; execution: --timeout SECONDS\nOnly -O0 is supported. emit-c, bench and --sanitize are unavailable."
STR m_usage, "Invalid command arguments; use lm0 --help"
STR m_unsupported, "Native compiler supports assembly emission and -O0 only; emit-c, bench and --sanitize are unavailable"
STR m_target, "Unsupported compiler target"
STR opt_version, "--version"
STR opt_help, "--help"
STR opt_output, "-o"
STR opt_entry, "--entry"
STR opt_module, "--module"
STR opt_sanitize, "--sanitize"
STR opt_opt, "-O"
STR opt_zero, "0"
STR opt_timeout, "--timeout"
STR opt_link, "--link"
STR opt_library, "--library"
STR kind_exe, "exe"
STR kind_object, "object"
STR kind_shared, "shared"
STR target_name, "x86_64-linux-gnu"
STR cmd_check, "check"
STR cmd_emit_asm, "emit-asm"
STR cmd_emit_c, "emit-c"
STR cmd_bench, "bench"
STR cmd_build, "build"
STR cmd_run, "run"
STR cmd_inspect, "inspect"
STR cmd_replace, "replace"
STR cmd_migrate, "migrate"
STR cmd_describe, "describe"
STR view_full,"full"
STR view_compact,"compact"
STR j_check, "{\"ok\":true,\"module\":"
STR j_check_end, ",\"functions\":%lu,\"diagnostics\":[]}\n"
STR j_output, "{\"ok\":true,\"output\":"
STR j_output_end, "}\n"
STR j_kind, ",\"kind\":"
STR j_build_end, ",\"optimization\":\"0\",\"sanitized\":false"
STR run_template, "/tmp/lm0-run-XXXXXX"
STR fmt_program, "%s/program"
.section .rodata
milliseconds: .double 1000.0
.text

FUNC list_count
 xor eax,eax
.count_loop:
 test rdi,rdi
 jz .count_end
 inc rax
 mov rdi,[rdi+NEXT]
 jmp .count_loop
.count_end:
 RETURN

FUNC print_output
 C text_out, "offset j_output"
 C json_string, "qword ptr [cli_output]"
 call library_link_metadata
 C text_out, "offset j_output_end"
 RETURN

FUNC absolute_path
 mov r12,rdi
 cmp byte ptr [r12],47
 je .absolute_existing
 C getcwd, 0, 0
 test rax,rax
 jz .read_bad
 mov r13,rax
 lea rdi,[rbp-48]
 C asprintf, rdi, "offset fmt_path", r13, r12
 cmp eax,0
 jl .alloc_bad
 C free, r13
 mov rax,[rbp-48]
 RETURN
.absolute_existing:
 mov rax,r12
 RETURN
STR fmt_path, "%s/%s"

FUNC prepare_output
 C absolute_path, rdi
 mov r12,rax
 mov [cli_output],rax
 C strlen, r12
 C slice, r12, rax
 mov r13,rax
 C strrchr, r13, 47
 test rax,rax
 jz .read_bad
 mov byte ptr [rax],0
 lea r14,[r13+1]
.mkdir_loop:
 mov al,[r14]
 test al,al
 jz .mkdir_last
 cmp al,47
 jne .mkdir_next
 mov byte ptr [r14],0
 C mkdir, r13, 493
 mov byte ptr [r14],47
.mkdir_next:
 inc r14
 jmp .mkdir_loop
.mkdir_last:
 C mkdir, r13, 493
 lea rdi,[rbp-48]
 C asprintf, rdi, "offset fmt_temp_dir", r13
 cmp eax,0
 jl .alloc_bad
 C mkdtemp, "qword ptr [rbp-48]"
 test rax,rax
 jz .read_bad
 mov [temp_directory],rax
 mov r14,rax
 lea rdi,[rbp-48]
 C asprintf, rdi, "offset fmt_asm_path", r14
 mov rax,[rbp-48]
 mov [temp_assembly],rax
 lea rdi,[rbp-48]
 C asprintf, rdi, "offset fmt_result_path", r14
 mov rax,[rbp-48]
 mov [temp_binary],rax
 RETURN
STR fmt_temp_dir, "%s/.lm0-build-XXXXXX"
STR fmt_asm_path, "%s/module.s"
STR fmt_result_path, "%s/result"

FUNC cleanup
 cmp qword ptr [process_pid],0
 je .cleanup_files
 mov rdi,[process_pid]
 neg rdi
 C kill, rdi, 9
.cleanup_files:
.irp var,temp_assembly,temp_binary,run_binary
 cmp qword ptr [\var],0
 je .cleanup_skip_\var
 C unlink, "qword ptr [\var]"
.cleanup_skip_\var:
.endr
.irp var,temp_directory,run_directory
 cmp qword ptr [\var],0
 je .cleanup_skip_\var
 C rmdir, "qword ptr [\var]"
.cleanup_skip_\var:
.endr
 RETURN

FUNC native_build
 mov qword ptr [diag_phase],offset p_backend
 C prepare_output, rdi
 mov rax,[cfg_cc]
 mov [rbp-64],rax
 mov qword ptr [rbp-56],offset cc_dumpmachine
 mov qword ptr [rbp-48],0
 lea rdi,[rbp-64]
 imul rsi,qword ptr [cfg_build_timeout],1000
 call execute_process
 cmp qword ptr [process_code],0
 jne .target_error
 C trim, "qword ptr [process_stdout]"
 C strcmp, rax, "qword ptr [cfg_target]"
 test eax,eax
 jnz .target_error
 C fopen, "qword ptr [temp_assembly]", "offset mode_wb"
 test rax,rax
 jz .read_bad
 mov r12,rax
 EQ "qword ptr [cli_kind]", kind_exe
 sete sil
 movzx esi,sil
 C emit_asm, r12, rsi
 C fclose, r12
 test eax,eax
 jnz .read_bad
 C list_count, "qword ptr [cli_links]"
 mov r12,rax
 C list_count, "qword ptr [cli_libraries]"
 add r12,rax
 add r12,20
 shl r12,3
 C alloc, r12
 mov r12,rax
 mov rax,[cfg_cc]
 mov [r12],rax
 mov rax,[temp_assembly]
 mov [r12+8],rax
 mov qword ptr [r12+16],offset opt_output
 mov rax,[temp_binary]
 mov [r12+24],rax
 mov qword ptr [r12+32],offset cc_noexecstack
 mov r13d,5
 EQ "qword ptr [cli_kind]", kind_object
 jnz .build_shared
 cmp qword ptr [cli_links],0
 jne usage_error
 cmp qword ptr [cli_libraries],0
 jne usage_error
 mov qword ptr [r12+r13*8],offset cc_object
 inc r13
 jmp .build_execute
.build_shared:
 EQ "qword ptr [cli_kind]", kind_shared
 jnz .build_links
 mov qword ptr [r12+r13*8],offset cc_shared
 inc r13
 mov qword ptr [r12+r13*8],offset cc_defs
 inc r13
.build_links:
 mov r14,[cli_links]
.build_link_loop:
 test r14,r14
 jz .build_libraries
 C absolute_path, "qword ptr [r14+NAME]"
 mov [r12+r13*8],rax
 inc r13
 mov r14,[r14+NEXT]
 jmp .build_link_loop
.build_libraries:
 mov r14,[cli_libraries]
.build_library_loop:
 test r14,r14
 jz .build_standard
 lea rdi,[rbp-48]
 C asprintf, rdi, "offset fmt_library", "qword ptr [r14+NAME]"
 cmp eax,0
 jl .alloc_bad
 mov rax,[rbp-48]
 mov [r12+r13*8],rax
 inc r13
 mov r14,[r14+NEXT]
 jmp .build_library_loop
.build_standard:
 cmp qword ptr [library_used],0
 je .build_execute
 call library_archive
 mov [r12+r13*8],rax
 inc r13
 EQ "qword ptr [cli_kind]",kind_shared
 jnz .build_standard_math
 mov qword ptr [r12+r13*8],offset library_hide_link
 inc r13
.build_standard_math:
 mov qword ptr [r12+r13*8],offset library_math_link
 inc r13
.build_execute:
 imul rsi,qword ptr [cfg_build_timeout],1000
 C execute_process, r12, rsi
 cmp qword ptr [process_timed],0
 jne .backend_limit
 cmp qword ptr [process_limited],0
 jne .backend_limit
 cmp qword ptr [process_code],0
 jne .backend_error
 C rename, "qword ptr [temp_binary]", "qword ptr [cli_output]"
 test eax,eax
 jnz .read_bad
 RETURN
.backend_error:
 C fail, "offset e_backend", "qword ptr [process_stderr]"
.backend_limit:
 FAIL e_backend_limit, m_backend_limit
STR cc_dumpmachine, "-dumpmachine"
STR cc_noexecstack, "-Wl,-z,noexecstack"
STR cc_object, "-c"
STR cc_shared, "-shared"
STR cc_defs, "-Wl,-z,defs"
STR fmt_library, "-l%s"
STR m_backend_limit, "Backend exceeded time or output limit"

.include "native/process.s"
.include "native/inspect.s"
