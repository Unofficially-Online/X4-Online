/* Entered by a future authenticated jump at 0x166d3ac, with the original
 * Update frame still present. RBX is the PlayerController. No hook or input
 * flag is installed by this file. The stolen compare is performed last so
 * the native branch at 0x166d3ba receives exactly its original flags. */
.text
.hidden player_cursor_native_dispatch
.hidden player_cursor_native_resume
.global player_cursor_native_bridge
.hidden player_cursor_native_bridge
.type player_cursor_native_bridge,@function
player_cursor_native_bridge:
.cfi_startproc
.cfi_def_cfa %rsp, 704
.cfi_offset %r15, -16
.cfi_offset %r14, -24
.cfi_offset %r13, -32
.cfi_offset %r12, -40
.cfi_offset %rbp, -48
.cfi_offset %rbx, -56
.macro save reg
    pushq \reg
    .cfi_adjust_cfa_offset 8
.endm
.macro restore reg
    popq \reg
    .cfi_adjust_cfa_offset -8
.endm
    pushfq
    .cfi_adjust_cfa_offset 8
    save %rax
    save %rbx
    save %rcx
    save %rdx
    save %rsi
    save %rdi
    save %rbp
    save %r8
    save %r9
    save %r10
    save %r11
    save %r12
    save %r13
    save %r14
    save %r15
    subq $256, %rsp
    .cfi_adjust_cfa_offset 256
.irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
    movdqu %xmm\n, (16*\n)(%rsp)
.endr
    movq %rbx, %rdi
    call player_cursor_native_dispatch
.irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
    movdqu (16*\n)(%rsp), %xmm\n
.endr
    addq $256, %rsp
    .cfi_adjust_cfa_offset -256
    restore %r15
    restore %r14
    restore %r13
    restore %r12
    restore %r11
    restore %r10
    restore %r9
    restore %r8
    restore %rbp
    restore %rdi
    restore %rsi
    restore %rdx
    restore %rcx
    restore %rbx
    restore %rax
    popfq
    .cfi_adjust_cfa_offset -8
    /* A scratch register is saved across the absolute address load. Pop and
     * indirect jump do not alter the compare's flags. */
    save %r11
    movabsq $0x3d8da7a, %r11
    cmpb $0, (%r11)
    restore %r11
    jmp *player_cursor_native_resume(%rip)
.cfi_endproc
.size player_cursor_native_bridge, .-player_cursor_native_bridge
.global player_cursor_native_bridge_end
.hidden player_cursor_native_bridge_end
player_cursor_native_bridge_end:
.section .note.GNU-stack,"",@progbits
