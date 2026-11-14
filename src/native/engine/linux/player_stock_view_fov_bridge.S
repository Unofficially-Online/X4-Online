/* Mid-function VIEW mode bridges for the pinned 9.00 coefficient function.
 * Entry is a jump, with the native function's 112-byte CFA already active.
 * Preserve flags, caller-saved GPRs and every XMM register around the C policy.
 * The policy changes only the destination of the displaced mov instruction.
 */
.text
.hidden player_stock_view_mode_dispatch
.macro SAVE_MODE
    pushfq
    .cfi_def_cfa_offset 120
    push %rax
    .cfi_def_cfa_offset 128
    push %rcx
    .cfi_def_cfa_offset 136
    push %rdx
    .cfi_def_cfa_offset 144
    push %rsi
    .cfi_def_cfa_offset 152
    push %rdi
    .cfi_def_cfa_offset 160
    push %r8
    .cfi_def_cfa_offset 168
    push %r9
    .cfi_def_cfa_offset 176
    push %r10
    .cfi_def_cfa_offset 184
    push %r11
    .cfi_def_cfa_offset 192
    sub $256,%rsp
    .cfi_def_cfa_offset 448
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
        movdqu %xmm\n,16*\n(%rsp)
    .endr
.endm
.macro RESTORE_MODE
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
        movdqu 16*\n(%rsp),%xmm\n
    .endr
    add $256,%rsp
    .cfi_def_cfa_offset 192
    pop %r11
    .cfi_def_cfa_offset 184
    pop %r10
    .cfi_def_cfa_offset 176
    pop %r9
    .cfi_def_cfa_offset 168
    pop %r8
    .cfi_def_cfa_offset 160
    pop %rdi
    .cfi_def_cfa_offset 152
    pop %rsi
    .cfi_def_cfa_offset 144
    pop %rdx
    .cfi_def_cfa_offset 136
    pop %rcx
    .cfi_def_cfa_offset 128
    pop %rax
    .cfi_def_cfa_offset 120
    popfq
    .cfi_def_cfa_offset 112
.endm
.macro FDE_START
    .cfi_startproc
    .cfi_def_cfa %rsp,112
    .cfi_offset %r13,-16
    .cfi_offset %r12,-24
    .cfi_offset %rbp,-32
    .cfi_offset %rbx,-40
.endm
.globl player_stock_view_mode_edx_bridge
.hidden player_stock_view_mode_edx_bridge
.type player_stock_view_mode_edx_bridge,@function
player_stock_view_mode_edx_bridge:
    FDE_START
    mov 0x880(%rdi),%edx
    SAVE_MODE
    mov %rbx,%rdi
    mov 304(%rsp),%esi
    xor %edx,%edx
    call player_stock_view_mode_dispatch
    mov %eax,304(%rsp)
    RESTORE_MODE
#ifdef PLAYER_STOCK_VIEW_BRIDGE_TEST
    jmp player_stock_view_test_edx_continue
#else
    jmp *0(%rip)
    .quad 0x1666d30
#endif
    .cfi_endproc
.size player_stock_view_mode_edx_bridge,.-player_stock_view_mode_edx_bridge

.globl player_stock_view_mode_eax_bridge
.hidden player_stock_view_mode_eax_bridge
.type player_stock_view_mode_eax_bridge,@function
player_stock_view_mode_eax_bridge:
    FDE_START
    mov 0x880(%rdi),%eax
    SAVE_MODE
    mov %rbx,%rdi
    mov 320(%rsp),%esi
    mov $1,%edx
    call player_stock_view_mode_dispatch
    mov %eax,320(%rsp)
    RESTORE_MODE
#ifdef PLAYER_STOCK_VIEW_BRIDGE_TEST
    jmp player_stock_view_test_eax_continue
#else
    jmp *0(%rip)
    .quad 0x1666e8d
#endif
    .cfi_endproc
.size player_stock_view_mode_eax_bridge,.-player_stock_view_mode_eax_bridge
.section .note.GNU-stack,"",@progbits
.text
.globl player_stock_view_fov_bridge
.hidden player_stock_view_fov_bridge
.type player_stock_view_fov_bridge,@function
.hidden player_stock_view_fov_dispatch
player_stock_view_fov_bridge:
    .cfi_startproc
    mov %rbx,%rsi
    jmp player_stock_view_fov_dispatch
    .cfi_endproc
.size player_stock_view_fov_bridge,.-player_stock_view_fov_bridge
.section .note.GNU-stack,"",@progbits
