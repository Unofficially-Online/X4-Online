/* Both displaced instructions are SysV calls. Their enclosing methods keep
 * the weapon in RBX and the engine component in R12, respectively. */
.text
.hidden player_stock_view_weapon_dispatch
.hidden player_stock_view_engine_dispatch
.globl player_stock_view_weapon_bridge
.hidden player_stock_view_weapon_bridge
.type player_stock_view_weapon_bridge,@function
player_stock_view_weapon_bridge:
    .cfi_startproc
    mov %rbx,%rdi
    jmp player_stock_view_weapon_dispatch
    .cfi_endproc
.size player_stock_view_weapon_bridge,.-player_stock_view_weapon_bridge
.globl player_stock_view_engine_bridge
.hidden player_stock_view_engine_bridge
.type player_stock_view_engine_bridge,@function
player_stock_view_engine_bridge:
    .cfi_startproc
    mov %r12,%rdi
    jmp player_stock_view_engine_dispatch
    .cfi_endproc
.size player_stock_view_engine_bridge,.-player_stock_view_engine_bridge
.section .note.GNU-stack,"",@progbits
