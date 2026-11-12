#ifndef X4_PLAYER_LIFETIME_PRELOAD_WRAPPERS_H
#define X4_PLAYER_LIFETIME_PRELOAD_WRAPPERS_H

extern void plp_call_original0(void);
extern void plp_call_original1(void);

/* No anonymous frame: these wrappers have static unwind metadata. The owned
 * entry stub has incremented the count before reading the binding. RAX, RDX,
 * vector returns and all callee-saved registers survive the decrement. */
__asm__(
    ".text\n"
    ".hidden plp_entry_count\n"
    ".hidden plp_original_target0\n"
    ".hidden plp_original_target1\n"
    ".hidden plp_call_original0\n"
    ".globl plp_call_original0\n"
    ".type plp_call_original0,@function\n"
    "plp_call_original0:\n"
    ".cfi_startproc\n"
    "subq $8,%rsp\n"
    ".cfi_def_cfa_offset 16\n"
    "call *plp_original_target0(%rip)\n"
    "addq $8,%rsp\n"
    ".cfi_def_cfa_offset 8\n"
    "lock decl plp_entry_count(%rip)\n"
    "ret\n"
    ".cfi_endproc\n"
    ".size plp_call_original0,.-plp_call_original0\n"
    ".hidden plp_call_original1\n"
    ".globl plp_call_original1\n"
    ".type plp_call_original1,@function\n"
    "plp_call_original1:\n"
    ".cfi_startproc\n"
    "subq $8,%rsp\n"
    ".cfi_def_cfa_offset 16\n"
    "call *plp_original_target1(%rip)\n"
    "addq $8,%rsp\n"
    ".cfi_def_cfa_offset 8\n"
    "lock decl plp_entry_count(%rip)\n"
    "ret\n"
    ".cfi_endproc\n"
    ".size plp_call_original1,.-plp_call_original1\n"
);

#endif
