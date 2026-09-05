.section .rodata
.align 8
.Ltwo64: .quad 0x43f0000000000000
.Ltwo63: .quad 0x43e0000000000000
.Lminus63: .quad 0xc3e0000000000000
.Lminusone: .quad 0xbff0000000000000
.text
.Llm0_trap:
push rbp
mov rbp,rsp
push rbx
sub rsp,8
mov rbx,rdi
call strlen@PLT
mov rdx,rax
mov rsi,rbx
mov edi,2
call write@PLT
mov edi,70
call exit@PLT
ud2

.Llm0_alloc:
push rbp
mov rbp,rsp
sub rsp,16
mov QWORD PTR [rbp-8],rdx
mov rax,rdi
mul rsi
test rdx,rdx
jnz .Lalloc_fail
test rax,rax
mov edi,1
cmovne rdi,rax
call malloc@PLT
test rax,rax
jz .Lalloc_fail
leave
ret
.Lalloc_fail:
mov rdi,QWORD PTR [rbp-8]
call .Llm0_trap

.Llm0_offset:
push rbp
mov rbp,rsp
sub rsp,32
mov QWORD PTR [rbp-8],rdi
mov QWORD PTR [rbp-16],rsi
mov QWORD PTR [rbp-24],rcx
mov rcx,rdx
mov rax,rsi
neg rax
test rsi,rsi
cmovns rax,rsi
mul rcx
test rdx,rdx
jnz .Loffset_fail
test rax,rax
js .Loffset_fail
mov rcx,rax
neg rcx
cmp QWORD PTR [rbp-16],0
cmovl rax,rcx
add rax,QWORD PTR [rbp-8]
leave
ret
.Loffset_fail:
mov rdi,QWORD PTR [rbp-24]
call .Llm0_trap
