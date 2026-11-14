.text
.globl test_mode_edx
.type test_mode_edx,@function
test_mode_edx:
    push %r13
    push %r12
    push %rbp
    push %rbx
    sub $72,%rsp
    mov %rsi,%rbx
    pcmpeqd %xmm0,%xmm0
    pcmpeqd %xmm15,%xmm15
    stc
    jmp player_stock_view_mode_edx_bridge
.globl player_stock_view_test_edx_continue
.hidden player_stock_view_test_edx_continue
player_stock_view_test_edx_continue:
    jnc 1f
    pmovmskb %xmm0,%ecx
    cmp $65535,%ecx
    jne 1f
    pmovmskb %xmm15,%ecx
    cmp $65535,%ecx
    jne 1f
    mov %edx,%eax
    jmp 2f
1:  mov $-1,%eax
2:
    add $72,%rsp
    pop %rbx
    pop %rbp
    pop %r12
    pop %r13
    ret
.globl test_mode_eax
.type test_mode_eax,@function
test_mode_eax:
    push %r13
    push %r12
    push %rbp
    push %rbx
    sub $72,%rsp
    mov %rsi,%rbx
    pcmpeqd %xmm0,%xmm0
    pcmpeqd %xmm15,%xmm15
    stc
    jmp player_stock_view_mode_eax_bridge
.globl player_stock_view_test_eax_continue
.hidden player_stock_view_test_eax_continue
player_stock_view_test_eax_continue:
    jnc 3f
    pmovmskb %xmm0,%ecx
    cmp $65535,%ecx
    jne 3f
    pmovmskb %xmm15,%ecx
    cmp $65535,%ecx
    je 4f
3:  mov $-1,%eax
4:  add $72,%rsp
    pop %rbx
    pop %rbp
    pop %r12
    pop %r13
    ret
.section .note.GNU-stack,"",@progbits
.text
.globl test_ship_mode
.type test_ship_mode,@function
test_ship_mode:
    push %r13
    push %r12
    push %rbp
    push %rbx
    sub $24,%rsp
    mov %rdi,%rdx
    mov %rsi,%rbp
    pcmpeqd %xmm0,%xmm0
    pcmpeqd %xmm15,%xmm15
    stc
    jmp player_stock_view_ship_bridge
.globl player_stock_view_test_ship_continue
.hidden player_stock_view_test_ship_continue
player_stock_view_test_ship_continue:
    jnc 1f
    pmovmskb %xmm0,%eax
    cmp $65535,%eax
    jne 1f
    pmovmskb %xmm15,%eax
    cmp $65535,%eax
    jne 1f
    mov %ecx,%eax
    jmp 2f
1:  mov $-1,%eax
2:  add $24,%rsp
    pop %rbx
    pop %rbp
    pop %r12
    pop %r13
    ret
.section .note.GNU-stack,"",@progbits
