.text
FUNC monotonic_ms
 lea rsi,[rbp-64]
 C clock_gettime, 1, rsi
 test eax,eax
 jnz .read_bad
 imul r12,qword ptr [rbp-64],1000
 mov rax,[rbp-56]
 xor edx,edx
 mov ecx,1000000
 div rcx
 add rax,r12
 RETURN

# Both pipes are polled and drained, with a single combined output budget.
# All subprocess launches use argv; command text is never sent to a shell.
FUNC execute_process
 mov r12,rdi
 mov r13,rsi
 mov qword ptr [process_timed],0
 mov qword ptr [process_limited],0
 mov qword ptr [process_out_len],0
 mov qword ptr [process_err_len],0
 mov qword ptr [process_code],0
 mov qword ptr [rbp-136],0
 mov rdi,[cfg_output]
 inc rdi
 call alloc
 mov [process_stdout],rax
 mov rdi,[cfg_output]
 inc rdi
 call alloc
 mov [process_stderr],rax
 C alloc, 65536
 mov r15,rax
 lea rdi,[rbp-64]
 C pipe2, rdi, 524288
 test eax,eax
 jnz .read_bad
 lea rdi,[rbp-72]
 C pipe2, rdi, 524288
 test eax,eax
 jnz .read_bad
 call fork
 test eax,eax
 js .read_bad
 jz .process_child
 mov r14,rax
 mov [process_pid],rax
 mov edi,[rbp-60]
 call close
 mov edi,[rbp-68]
 call close
 mov edi,[rbp-64]
 C fcntl, rdi, 4, 2048
 mov edi,[rbp-72]
 C fcntl, rdi, 4, 2048
 mov eax,[rbp-64]
 mov [rbp-112],eax
 mov dword ptr [rbp-108],1
 mov eax,[rbp-72]
 mov [rbp-104],eax
 mov dword ptr [rbp-100],1
 call monotonic_ms
 add r13,rax
.process_loop:
 call monotonic_ms
 cmp rax,r13
 jae .process_timeout
 mov rdx,r13
 sub rdx,rax
 mov eax,50
 cmp rdx,rax
 cmova rdx,rax
 lea rdi,[rbp-112]
 C poll, rdi, 2, rdx
 test eax,eax
 js .process_wait
 xor ebx,ebx
.process_drain:
 cmp dword ptr [rbp-112+rbx*8],-1
 je .process_drain_next
 cmp word ptr [rbp-106+rbx*8],0
 je .process_drain_next
 mov edi,[rbp-112+rbx*8]
 C read, rdi, r15, 65536
 test rax,rax
 js .process_drain_next
 jz .process_pipe_end
 mov [rbp-144],rax
 mov rdx,[process_out_len]
 add rdx,[process_err_len]
 mov rcx,[cfg_output]
 sub rcx,rdx
 cmp rax,rcx
 jbe .process_chunk_fits
 mov qword ptr [process_limited],1
 mov rax,rcx
.process_chunk_fits:
 mov [rbp-152],rax
 lea rcx,[process_out_len]
 lea rdx,[process_stdout]
 test rbx,rbx
 jz .process_copy_chunk
 lea rcx,[process_err_len]
 lea rdx,[process_stderr]
.process_copy_chunk:
 mov [rbp-160],rcx
 mov rdi,[rdx]
 add rdi,[rcx]
 C memcpy, rdi, r15, rax
 mov rax,[rbp-152]
 mov rcx,[rbp-160]
 add [rcx],rax
 cmp qword ptr [process_limited],0
 jne .process_finish
 jmp .process_drain_next
.process_pipe_end:
 mov edi,[rbp-112+rbx*8]
 call close
 mov dword ptr [rbp-112+rbx*8],-1
.process_drain_next:
 inc rbx
 cmp rbx,2
 jb .process_drain
.process_wait:
 cmp qword ptr [rbp-136],0
 jne .process_check_end
 lea rsi,[rbp-120]
 C waitpid, r14, rsi, 1
 cmp rax,r14
 jne .process_check_end
 mov qword ptr [rbp-136],1
.process_check_end:
 cmp qword ptr [rbp-136],0
 je .process_loop
 cmp dword ptr [rbp-112],-1
 jne .process_loop
 cmp dword ptr [rbp-104],-1
 jne .process_loop
 jmp .process_finish
.process_timeout:
 mov qword ptr [process_timed],1
.process_finish:
 mov rdi,r14
 neg rdi
 C kill, rdi, 9
 cmp qword ptr [rbp-136],0
 jne .process_close
 C kill, r14, 9
 lea rsi,[rbp-120]
 C waitpid, r14, rsi, 0
.process_close:
 mov edi,[rbp-112]
 call close
 mov edi,[rbp-104]
 call close
 mov qword ptr [process_pid],0
 mov eax,[rbp-120]
 mov edx,eax
 and edx,127
 jz .process_normal_exit
 mov eax,edx
 neg eax
 cdqe
 jmp .process_return
.process_normal_exit:
 shr eax,8
 and eax,255
.process_return:
 mov [process_code],rax
 RETURN
.process_child:
 call setsid
 C open, "offset dev_null", 0
 C dup2, rax, 0
 mov edi,[rbp-60]
 C dup2, rdi, 1
 mov edi,[rbp-68]
 C dup2, rdi, 2
 C execvp, "qword ptr [r12]", r12
 C perror, "offset m_exec"
 C _exit, 127
STR dev_null, "/dev/null"
STR m_exec, "Unable to execute native tool"

# Locate a complete JSON array while respecting nested arrays, objects, and
# escaped strings. Runtime diagnostics are accepted only for LM0 trap exit 70.
FUNC diagnostic_array
 mov r12,rdi
 C strstr, r12, "offset diagnostics_key"
 test rax,rax
 jz .diag_array_none
 add rax,14
 mov r12,rax
 mov r13,rax
 xor r14d,r14d
 xor r15d,r15d
.diag_array_loop:
 movzx eax,byte ptr [r13]
 test eax,eax
 jz .diag_array_none
 inc r13
 test r15d,r15d
 jz .diag_array_outside
 cmp eax,92
 je .diag_array_escape
 cmp eax,34
 jne .diag_array_loop
 xor r15d,r15d
 jmp .diag_array_loop
.diag_array_escape:
 cmp byte ptr [r13],0
 je .diag_array_none
 inc r13
 jmp .diag_array_loop
.diag_array_outside:
 cmp eax,34
 je .diag_array_quote
 cmp eax,91
 je .diag_array_push
 cmp eax,123
 je .diag_array_push
 cmp eax,93
 je .diag_array_pop
 cmp eax,125
 je .diag_array_pop
 jmp .diag_array_loop
.diag_array_quote:
 mov r15d,1
 jmp .diag_array_loop
.diag_array_push:
 inc r14
 cmp r14,64
 ja .diag_array_none
 jmp .diag_array_loop
.diag_array_pop:
 dec r14
 jnz .diag_array_loop
 mov rsi,r13
 sub rsi,r12
 C slice, r12, rsi
 RETURN
.diag_array_none:
 xor eax,eax
 RETURN
STR diagnostics_key, "\"diagnostics\":"

FUNC print_execution
 xor r12d,r12d
 mov rax,[process_code]
 test rax,rax
 js .execution_bad
 cmp rax,70
 je .execution_trap
 cmp qword ptr [process_timed],0
 jne .execution_bad
 cmp qword ptr [process_limited],0
 jne .execution_bad
 C text_out, "offset j_run_ok"
 jmp .execution_fields
.execution_trap:
 C diagnostic_array, "qword ptr [process_stderr]"
 mov r13,rax
 test r13,r13
 jz .execution_not_trap
 mov r12d,3
 C text_out, "offset j_run_bad"
 C text_out, "offset j_run_diagnostics"
 C text_out, r13
 jmp .execution_fields
.execution_not_trap:
 C text_out, "offset j_run_ok"
 jmp .execution_fields
.execution_bad:
 mov r12d,3
 C text_out, "offset j_run_bad"
.execution_fields:
 C fprintf, "qword ptr [jout]", "offset j_run_code", "qword ptr [process_code]"
 C json_bytes, "qword ptr [process_stdout]", "qword ptr [process_out_len]"
 C text_out, "offset j_run_stderr"
 C json_bytes, "qword ptr [process_stderr]", "qword ptr [process_err_len]"
 C text_out, "offset j_run_timed"
 mov rdi,[process_timed]
 call json_bool
 C text_out, "offset j_run_limited"
 mov rdi,[process_limited]
 call json_bool
 C text_out, "offset j_output_end"
 mov eax,r12d
 RETURN
FUNC json_bool
 test rdi,rdi
 mov rdi,offset s_false
 jz .bool_print
 mov rdi,offset s_true
.bool_print:
 call text_out
 RETURN
STR j_run_ok, "{\"ok\":true"
STR j_run_bad, "{\"ok\":false"
STR j_run_diagnostics, ",\"diagnostics\":"
STR j_run_code, ",\"exit_code\":%ld,\"stdout\":"
STR j_run_stderr, ",\"stderr\":"
STR j_run_timed, ",\"timed_out\":"
STR j_run_limited, ",\"output_limited\":"
