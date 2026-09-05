.intel_syntax noprefix
.text
.globl lm0_sha256
.type lm0_sha256,@function
# char *lm0_sha256(const void *bytes, size_t length, char hex[65]); null on OOM.
# Shared by the assembly compiler and the C evaluator.
lm0_sha256:
 push rbp
 mov rbp,rsp
 push rbx
 push r12
 push r13
 push r14
 push r15
 sub rsp,104
 mov r12,rdi
 mov rbx,rsi
 mov r14,rdx
 lea r13,[rsi+72]
 and r13,-64
 mov rdi,r13
 mov esi,1
 call calloc@PLT
 test rax,rax
 jz .hash_return
 mov r15,rax
 mov rdi,rax
 mov rsi,r12
 mov rdx,rbx
 call memcpy@PLT
 mov byte ptr [r15+rbx],128
 shl rbx,3
 bswap rbx
 mov [r15+r13-8],rbx
 movdqu xmm0,[rip+.sha_initial]
 movdqu xmm1,[rip+.sha_initial+16]
 movdqu [rbp-80],xmm0
 movdqu [rbp-64],xmm1
 xor ebx,ebx
.hash_blocks:
 lea rdi,[rbp-80]
 lea rsi,[r15+rbx]
 call .sha_block
 add rbx,64
 cmp rbx,r13
 jb .hash_blocks
 mov rdi,r15
 call free@PLT
 xor ecx,ecx
 lea rsi,[rip+.sha_hex]
.hash_words:
 mov eax,[rbp-80+rcx*4]
 mov edi,8
.hash_nibbles:
 mov edx,eax
 shr edx,28
 mov dl,[rsi+rdx]
 mov [r14],dl
 inc r14
 shl eax,4
 dec edi
 jnz .hash_nibbles
 inc ecx
 cmp ecx,8
 jb .hash_words
 mov byte ptr [r14],0
 lea rax,[r14-64]
.hash_return:
 lea rsp,[rbp-40]
 pop r15
 pop r14
 pop r13
 pop r12
 pop rbx
 pop rbp
 ret
.size lm0_sha256,.-lm0_sha256

.sha_block:
 push rbp
 mov rbp,rsp
 push rbx
 push r12
 push r13
 push r14
 push r15
 sub rsp,264
 mov r12,rdi
 xor ecx,ecx
.sha_load:
 mov eax,[rsi+rcx*4]
 bswap eax
 mov [rsp+rcx*4],eax
 inc ecx
 cmp ecx,16
 jb .sha_load
.sha_expand:
 mov eax,[rsp+rcx*4-60]
 mov edx,eax
 mov edi,eax
 ror eax,7
 ror edx,18
 shr edi,3
 xor eax,edx
 xor eax,edi
 mov edx,[rsp+rcx*4-8]
 mov esi,edx
 mov edi,edx
 ror edx,17
 ror esi,19
 shr edi,10
 xor edx,esi
 xor edx,edi
 add eax,edx
 add eax,[rsp+rcx*4-64]
 add eax,[rsp+rcx*4-28]
 mov [rsp+rcx*4],eax
 inc ecx
 cmp ecx,64
 jb .sha_expand
 mov eax,[r12]
 mov ebx,[r12+4]
 mov ecx,[r12+8]
 mov edx,[r12+12]
 mov r8d,[r12+16]
 mov r9d,[r12+20]
 mov r10d,[r12+24]
 mov r11d,[r12+28]
 xor r14d,r14d
 lea r13,[rip+.sha_k]
.sha_round:
 mov esi,r8d
 mov edi,r8d
 mov r15d,r8d
 ror esi,6
 ror edi,11
 ror r15d,25
 xor esi,edi
 xor esi,r15d
 add esi,r11d
 mov edi,r9d
 xor edi,r10d
 and edi,r8d
 xor edi,r10d
 add esi,edi
 add esi,[r13+r14*4]
 add esi,[rsp+r14*4]
 mov [rsp+256],esi
 mov esi,eax
 mov edi,eax
 mov r15d,eax
 ror esi,2
 ror edi,13
 ror r15d,22
 xor esi,edi
 xor esi,r15d
 mov edi,eax
 xor edi,ebx
 and edi,ecx
 mov r15d,eax
 and r15d,ebx
 xor edi,r15d
 add esi,edi
 mov r11d,r10d
 mov r10d,r9d
 mov r9d,r8d
 mov r8d,edx
 add r8d,[rsp+256]
 mov edx,ecx
 mov ecx,ebx
 mov ebx,eax
 mov eax,esi
 add eax,[rsp+256]
 inc r14d
 cmp r14d,64
 jb .sha_round
 add [r12],eax
 add [r12+4],ebx
 add [r12+8],ecx
 add [r12+12],edx
 add [r12+16],r8d
 add [r12+20],r9d
 add [r12+24],r10d
 add [r12+28],r11d
 lea rsp,[rbp-40]
 pop r15
 pop r14
 pop r13
 pop r12
 pop rbx
 pop rbp
 ret
.section .rodata
.sha_hex: .asciz "0123456789abcdef"
.align 16
.sha_initial: .long 0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19
.sha_k:
.long 0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5
.long 0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174
.long 0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da
.long 0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967
.long 0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85
.long 0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070
.long 0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3
.long 0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
.section .note.GNU-stack,"",@progbits
