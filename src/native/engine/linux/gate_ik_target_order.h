/* Default-off Linux scalar angular-target arithmetic order candidate.
 * Separate from the read-only IK target observer; they cannot share a process
 * lifetime because their permanent terminal trap identities have the same PC.
 *
 * Only byte 13 of the aligned 0x15661f0 line changes to INT3. The native 11-byte
 * sequence crosses that line; every remaining byte stays untouched. The
 * process-lifetime exclusive stock_trap contract applies, including late traps.
 * There is no C callback or allocation on this fast path.
 *
 * Native: XMM3=N-pi; XMM0=XMM3; XMM0+=current.
 * Candidate preserves native XMM3 and every upper lane, but computes XMM0.low
 * as (N+current)-pi. Four legacy SSE instructions are required. Their natural
 * sticky flags are retained, NOT cleared or replaced by a native shadow result.
 * Control must be round to nearest, all exceptions masked, DAZ off and FTZ off.
 * Other controls latch failure and run the original three operations, then
 * STOP 213; this is never accepted
 * as a successful correction. Counters may advance after STOP: restoration is
 * not proof that an already delivered trap or its stub has become quiescent.
 * OPEN teardown boundary: a delayed unsupported invocation may latch failure
 * after stats.reason=1 was recorded. That reason proves frame completion only;
 * final counters are observations, not proof that delayed failures are absent.
 */
#define IK_TARGET_ORDER_FUNCTION 0x1565b00u
#define IK_TARGET_ORDER_FUNCTION_SIZE 0xc40u
#define IK_TARGET_ORDER_SITE 0x15661fdu
#define IK_TARGET_ORDER_LINE 0x15661f0u
#define IK_TARGET_ORDER_CONTINUE 0x1566208u
#define IK_TARGET_ORDER_TAU 0x2e24944u
#define IK_TARGET_ORDER_PI 0x2e27154u

/* calls counts stub entries, not target stores. corrected counts completed
 * candidate arithmetic; unsupported is latched before native fallback, which
 * may itself fault when the caller unmasks exceptions. active covers only stub
 * execution. late counts entries that observe STOP, not all delayed traps.
 * None of these counters is reset after the permanent identity is published. */
__attribute__((used)) static uint64_t ik_target_order_calls;
__attribute__((used)) static uint64_t ik_target_order_corrected;
__attribute__((used)) static uint64_t ik_target_order_unsupported;
__attribute__((used)) static uint64_t ik_target_order_active;
__attribute__((used)) static uint64_t ik_target_order_late;
__attribute__((used)) static uint64_t ik_target_order_before_flags_or;
__attribute__((used)) static uint64_t ik_target_order_after_flags_or;
__attribute__((used)) static uint64_t ik_target_order_added_flags_or;
__attribute__((used)) static uint32_t ik_target_order_last_unsupported_mxcsr;
__attribute__((used)) static uintptr_t ik_target_order_continue;
static unsigned __int128 ik_target_order_original, ik_target_order_patched;
static int ik_target_order_ever_installed;
static DWORD ik_target_order_owner;
static const char *ik_target_order_stage = "off";
static int (*ik_target_order_swap)(void *, unsigned __int128, unsigned __int128) = stock_trap_swap;
static int (*ik_target_order_close)(void *) = gate_code_close;
#ifdef GATE_TLS_TEST
static void (*ik_target_order_test_arm_hook)(unsigned);
#endif

/* Protect the interrupted red zone, RFLAGS and the only scratch GPRs. There
 * are no calls, packed FP operations or VEX instructions. All controls remain
 * untouched; only the intended arithmetic can add MXCSR sticky flags. The
 * continuation and chosen arithmetic never change after publication, so a
 * trap delivered after byte restoration still has its original meaning. */
static __attribute__((naked, used, noinline)) void ik_target_order_entry(void) {
    __asm__ volatile(
        "lea -256(%rsp),%rsp\n\tpushfq\n\tpush %rax\n\tpush %rdx\n\tlea -16(%rsp),%rsp\n\t"
        "lock incq ik_target_order_active(%rip)\n\tlock incq ik_target_order_calls(%rip)\n\t"
        "cmpl $0,ik_target_order_stopping(%rip)\n\tje 1f\n\tlock incq ik_target_order_late(%rip)\n\t"
        "1:\n\tstmxcsr (%rsp)\n\tmov (%rsp),%eax\n\tand $0x3f,%eax\n\t"
        "lock orq %rax,ik_target_order_before_flags_or(%rip)\n\t"
        "mov (%rsp),%eax\n\tand $0xffc0,%eax\n\tcmp $0x1f80,%eax\n\tjne 2f\n\t"
        "movaps %xmm3,%xmm0\n\tsubss %xmm6,%xmm3\n\taddss %xmm7,%xmm0\n\tsubss %xmm6,%xmm0\n\t"
        "lock incq ik_target_order_corrected(%rip)\n\tjmp 3f\n\t"
        "2:\n\tmov (%rsp),%eax\n\tmov %eax,ik_target_order_last_unsupported_mxcsr(%rip)\n\t"
        "lock incq ik_target_order_unsupported(%rip)\n\t"
        "subss %xmm6,%xmm3\n\tmovaps %xmm3,%xmm0\n\taddss %xmm7,%xmm0\n\t"
        "3:\n\tstmxcsr 4(%rsp)\n\tmov 4(%rsp),%eax\n\tand $0x3f,%eax\n\t"
        "lock orq %rax,ik_target_order_after_flags_or(%rip)\n\t"
        "mov (%rsp),%edx\n\tnot %edx\n\tand %eax,%edx\n\tand $0x3f,%edx\n\t"
        "lock orq %rdx,ik_target_order_added_flags_or(%rip)\n\tlock decq ik_target_order_active(%rip)\n\t"
        "lea 16(%rsp),%rsp\n\tpop %rdx\n\tpop %rax\n\tpopfq\n\tlea 256(%rsp),%rsp\n\t"
        "jmp *ik_target_order_continue(%rip)\n\t");
}
static int ik_target_order_failure(void) {
    return __atomic_load_n(&ik_target_order_unsupported, __ATOMIC_ACQUIRE) != 0;
}
static int ik_target_order_rx(void) {
    char permissions[5], path[PATH_MAX];
    return gate_maps_find((uintptr_t)LX(IK_TARGET_ORDER_LINE), permissions, path, sizeof(path)) &&
        !strcmp(permissions, "r-xp");
}
static int ik_target_order_pins(void) {
    unsigned char function[IK_TARGET_ORDER_FUNCTION_SIZE];
    uint32_t tau = 0, pi = 0;
    if (!image || !gate_read_local(LX(IK_TARGET_ORDER_FUNCTION), function, sizeof(function)) ||
        !gate_read_local(LX(IK_TARGET_ORDER_TAU), &tau, 4) || tau != 0x40c90fdbu ||
        !gate_read_local(LX(IK_TARGET_ORDER_PI), &pi, 4) || pi != 0x40490fdbu) return 0;
    if (ik_target_order_installed) {
        unsigned __int128 current;
        if (!gate_read_local(LX(IK_TARGET_ORDER_LINE), &current, 16) || current != ik_target_order_patched) return 0;
        memcpy(function + IK_TARGET_ORDER_LINE - IK_TARGET_ORDER_FUNCTION, &ik_target_order_original, 16);
    }
    return hash_ok(function, sizeof(function), "5eb63f9985030adbb2ff5271f96f756aa7ae8b15a213d95494d6b0a7d5e2b019");
}
/* Shared trap lifecycle lock held. CAS failure may still mean our byte was
 * written. Byte ownership, RX and SYNC_CORE are independent cleanup duties. */
static void ik_target_order_cleanup_locked(void) {
    unsigned __int128 current = 0;
    int readable = gate_read_local(LX(IK_TARGET_ORDER_LINE), &current, 16);
    if (readable && current == ik_target_order_patched)
        (void)ik_target_order_swap(LX(IK_TARGET_ORDER_LINE), ik_target_order_patched, ik_target_order_original);
    readable = gate_read_local(LX(IK_TARGET_ORDER_LINE), &current, 16);
    pthread_mutex_lock(&gate_code_lock);
    int closed = ik_target_order_close(LX(IK_TARGET_ORDER_LINE));
    pthread_mutex_unlock(&gate_code_lock);
    int synced = stock_trap_sync();
    if (readable && current == ik_target_order_original && closed && ik_target_order_rx() && synced) {
        ik_target_order_installed = 0;
        ik_target_order_cleanup = 0;
        ik_target_order_restored = 1;
        ik_target_order_stage = "restored";
    } else {
        ik_target_order_cleanup = 1;
        ik_target_order_restored = 0;
        ik_target_order_stage = "cleanup_uncertain";
    }
}
static int ik_target_order_arm_locked(void) {
    if (stats.active && ik_target_order_installed && !ik_target_order_stopping &&
        GetCurrentThreadId() == ik_target_order_owner)
        return !ik_target_order_cleanup && !ik_target_order_failure() && stock_trap_handler_owned() &&
            !stock_trap_sync_uncertain && ik_target_order_rx() && ik_target_order_pins();
    if (ik_target_order_ever_installed) {
        ik_target_order_stage = "one-shot";
        return 0;
    }
    DWORD owner = __atomic_load_n(&ui_thread, __ATOMIC_ACQUIRE);
    if (!stats.active || !boot_mode || boot_phase || stats.issued || !owner || GetCurrentThreadId() != owner) {
        ik_target_order_stage = "thread_or_session";
        return 0;
    }
    if (!lxb_configured || !world_identity_installed) {
        ik_target_order_stage = "manifest_or_identities";
        return 0;
    }
    if (fs.state == FS_ARMED || fs.state == FS_RUNNING || cov_armed || lxis_installed || lxis_cleanup_pending() ||
        __atomic_load_n(&stock_trap_ik_entry_ready, __ATOMIC_ACQUIRE) ||
        __atomic_load_n(&stock_trap_ik_terminal_ready, __ATOMIC_ACQUIRE)) {
        ik_target_order_stage = "conflict";
        return 0;
    }
    unsigned char digest[32];
    char hex[65];
    if (!gate_env.exe || !gate_file_sha256(gate_env.exe, LX_FILE_SIZE, digest)) {
        ik_target_order_stage = "file";
        return 0;
    }
    hex32(digest, hex);
    if (strcmp(hex, LX_FILE_SHA) || !ik_target_order_pins()) {
        ik_target_order_stage = "pins";
        return 0;
    }
    if (!ik_target_order_rx() || !stock_trap_prepare()) {
        ik_target_order_stage = "protection_or_trap";
        return 0;
    }
    ik_target_order_owner = owner;
    ik_target_order_continue = (uintptr_t)LX(IK_TARGET_ORDER_CONTINUE);
    memcpy(&ik_target_order_original, LX(IK_TARGET_ORDER_LINE), 16);
    ik_target_order_patched = ik_target_order_original;
    ((unsigned char *)&ik_target_order_patched)[13] = 0xcc;
    /* Neither this identity nor its continuation/counters are ever reset. */
    __atomic_store_n(&ik_target_order_ever_installed, 1, __ATOMIC_RELEASE);
    __atomic_store_n(&stock_trap_ik_order_ready, 1, __ATOMIC_RELEASE);
#ifdef GATE_TLS_TEST
    if (ik_target_order_test_arm_hook) ik_target_order_test_arm_hook(1);
#endif
    if (__atomic_load_n(&ik_target_order_stopping, __ATOMIC_ACQUIRE)) return 0;
    ik_target_order_cleanup = 1;
    if (!ik_target_order_swap(LX(IK_TARGET_ORDER_LINE), ik_target_order_original, ik_target_order_patched)) {
        ik_target_order_cleanup_locked();
        return 0;
    }
    ik_target_order_installed = 1;
#ifdef GATE_TLS_TEST
    if (ik_target_order_test_arm_hook) ik_target_order_test_arm_hook(2);
#endif
    if (__atomic_load_n(&ik_target_order_stopping, __ATOMIC_ACQUIRE) || ik_target_order_failure()) {
        ik_target_order_cleanup_locked();
        if (ik_target_order_failure()) ik_target_order_stage = "unsupported_controls";
        return 0;
    }
    ik_target_order_cleanup = 0;
    ik_target_order_stage = "armed";
    return 1;
}
static int ik_target_order_arm(void) {
    pthread_mutex_lock(&stock_state_lifecycle_lock);
    int ok = !__atomic_load_n(&ik_target_order_stopping, __ATOMIC_ACQUIRE) && ik_target_order_arm_locked();
    pthread_mutex_unlock(&stock_state_lifecycle_lock);
    return ok;
}
static int ik_target_order_check(void) {
    if (!__atomic_load_n(&ik_target_order_ever_installed, __ATOMIC_ACQUIRE)) return 1;
    pthread_mutex_lock(&stock_state_lifecycle_lock);
    int ok;
    if (ik_target_order_failure()) {
        ik_target_order_stage = "unsupported_controls";
        ok = 0;
    } else {
        ok = __atomic_load_n(&ik_target_order_stopping, __ATOMIC_ACQUIRE) ||
            (ik_target_order_installed && !ik_target_order_cleanup && stock_trap_handler_owned() &&
             !stock_trap_sync_uncertain && ik_target_order_rx() && ik_target_order_pins());
        if (!ok) ik_target_order_stage = "ownership";
    }
    pthread_mutex_unlock(&stock_state_lifecycle_lock);
    return ok;
}
static void ik_target_order_restore(void) {
    __atomic_store_n(&ik_target_order_stopping, 1, __ATOMIC_RELEASE);
    pthread_mutex_lock(&stock_state_lifecycle_lock);
    if (ik_target_order_installed || ik_target_order_cleanup) ik_target_order_cleanup_locked();
    pthread_mutex_unlock(&stock_state_lifecycle_lock);
}
static void ik_target_order_begin(void) {
    pthread_mutex_lock(&stock_state_lifecycle_lock);
    if (!ik_target_order_ever_installed && !ik_target_order_cleanup)
        __atomic_store_n(&ik_target_order_stopping, 0, __ATOMIC_RELEASE);
    pthread_mutex_unlock(&stock_state_lifecycle_lock);
}
static int ik_target_order_status_text(char *out, size_t size) {
    int n = snprintf(out, size,
        "installed=%d restored=%d cleanup=%d stage=%s calls=%llu corrected=%llu unsupported=%llu active=%llu late=%llu "
        "owner=%u last_unsupported_mxcsr=%08x before_flags_or=%02llx after_flags_or=%02llx added_flags_or=%02llx "
        "failure=%d scope=all_scalar_angular late_possible=1 quiescence=unproven",
        ik_target_order_installed, ik_target_order_restored, ik_target_order_cleanup, ik_target_order_stage,
        (unsigned long long)__atomic_load_n(&ik_target_order_calls, __ATOMIC_ACQUIRE),
        (unsigned long long)__atomic_load_n(&ik_target_order_corrected, __ATOMIC_ACQUIRE),
        (unsigned long long)__atomic_load_n(&ik_target_order_unsupported, __ATOMIC_ACQUIRE),
        (unsigned long long)__atomic_load_n(&ik_target_order_active, __ATOMIC_ACQUIRE),
        (unsigned long long)__atomic_load_n(&ik_target_order_late, __ATOMIC_ACQUIRE), ik_target_order_owner,
        __atomic_load_n(&ik_target_order_last_unsupported_mxcsr, __ATOMIC_ACQUIRE),
        (unsigned long long)__atomic_load_n(&ik_target_order_before_flags_or, __ATOMIC_ACQUIRE),
        (unsigned long long)__atomic_load_n(&ik_target_order_after_flags_or, __ATOMIC_ACQUIRE),
        (unsigned long long)__atomic_load_n(&ik_target_order_added_flags_or, __ATOMIC_ACQUIRE), ik_target_order_failure());
    return n > 0 && (size_t)n < size ? n : 0;
}
static int lua_ik_target_order(lua_State *L) {
    p_lua_pushnumber(L, accept_ui() && ik_target_order_arm());
    return 1;
}
static int lua_ik_target_order_status(lua_State *L) {
    char out[768];
    pthread_mutex_lock(&stock_state_lifecycle_lock);
    int length = accept_ui() ? ik_target_order_status_text(out, sizeof(out)) : 0;
    pthread_mutex_unlock(&stock_state_lifecycle_lock);
    if (length) p_lua_pushlstring(L, out, (size_t)length);
    else p_lua_pushnil(L);
    return 1;
}
