/* Pinned ship mode bridge: native CFA is RSP+64 at 0x129bb21. */
.text
.hidden player_stock_view_ship_dispatch
.macro SAVE_MODE
    pushfq
    .cfi_def_cfa_offset 72
    push %rax
    .cfi_def_cfa_offset 80
    push %rcx
    .cfi_def_cfa_offset 88
    push %rdx
    .cfi_def_cfa_offset 96
    push %rsi
    .cfi_def_cfa_offset 104
    push %rdi
    .cfi_def_cfa_offset 112
    push %r8
    .cfi_def_cfa_offset 120
    push %r9
    .cfi_def_cfa_offset 128
    push %r10
    .cfi_def_cfa_offset 136
    push %r11
    .cfi_def_cfa_offset 144
    sub $256,%rsp
    .cfi_def_cfa_offset 400
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
        movdqu %xmm\n,16*\n(%rsp)
    .endr
.endm
.macro RESTORE_MODE
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
        movdqu 16*\n(%rsp),%xmm\n
    .endr
    add $256,%rsp
    .cfi_def_cfa_offset 144
    pop %r11
    .cfi_def_cfa_offset 136
    pop %r10
    .cfi_def_cfa_offset 128
    pop %r9
    .cfi_def_cfa_offset 120
    pop %r8
    .cfi_def_cfa_offset 112
    pop %rdi
    .cfi_def_cfa_offset 104
    pop %rsi
    .cfi_def_cfa_offset 96
    pop %rdx
    .cfi_def_cfa_offset 88
    pop %rcx
    .cfi_def_cfa_offset 80
    pop %rax
    .cfi_def_cfa_offset 72
    popfq
    .cfi_def_cfa_offset 64
.endm
.macro FDE_START
    .cfi_startproc
    .cfi_def_cfa %rsp,64
    .cfi_offset %r13,-16
    .cfi_offset %r12,-24
    .cfi_offset %rbp,-32
    .cfi_offset %rbx,-40
.endm
/* Ship predicate mode load at 0x129bb21. Native this is RBP. */
.globl player_stock_view_ship_bridge
.hidden player_stock_view_ship_bridge
.type player_stock_view_ship_bridge,@function
player_stock_view_ship_bridge:
    FDE_START
    mov 0x880(%rdx),%ecx
    SAVE_MODE
    mov %rbp,%rdi
    mov 312(%rsp),%esi
    call player_stock_view_ship_dispatch
    mov %eax,312(%rsp)
    RESTORE_MODE
#ifdef PLAYER_STOCK_VIEW_BRIDGE_TEST
    jmp player_stock_view_test_ship_continue
#else
    jmp *0(%rip)
    .quad 0x129bb27
#endif
    .cfi_endproc
.size player_stock_view_ship_bridge,.-player_stock_view_ship_bridge
.section .note.GNU-stack,"",@progbits
