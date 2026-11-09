/* Default-off, Linux-only IK angular-target observer. No arithmetic correction.
 * Two permanent SIGTRAP identities observe the scalar angular normalizer:
 * 1566170 (requested angle) and 15661fd (final native arithmetic). Only the
 * first byte changes; every other original byte remains available to a thread
 * already inside the instruction sequence. The exclusive process-lifetime
 * stock_trap contract applies. This is not a general instrumentation API.
 *
 * Bind fixture skeletons explicitly before the observation window. Callbacks
 * use that bounded cache, never a world/skeleton census. Rows become ready at
 * the terminal callback and may become ready after STOP. native_result_before_store
 * is XMM0 after the original three operations, BEFORE the original target store.
 * Unfinished entry rows remain unavailable; a final drain is not a completeness
 * guarantee. A missed entry produces an explicitly entry_known=0 terminal row.
 */
#define IK_TARGET_FUNCTION 0x1565b00u
#define IK_TARGET_FUNCTION_SIZE 0xc40u
#define IK_TARGET_ENTRY_SITE 0x1566170u
#define IK_TARGET_ENTRY_CONTINUE 0x1566176u
#define IK_TARGET_TERMINAL_SITE 0x15661fdu
#define IK_TARGET_TERMINAL_LINE 0x15661f0u
#define IK_TARGET_TERMINAL_CONTINUE 0x1566208u
#define IK_TARGET_TAU 0x2e24944u
#define IK_TARGET_PI 0x2e27154u
#define IK_TARGET_BACKEND 0x3db6b74u
#define IK_TARGET_ROWS 4096u
#define IK_TARGET_JOINTS 4096u
#define IK_TARGET_OBJECTS (LXB_ACTORS * 33u)
#define IK_TARGET_DEPTH 8u

typedef struct {
    uintptr_t joint, vtable, object, root;
    uint64_t runtime, root_runtime;
    unsigned actor, weapon, group, index;
} ik_target_binding;
typedef struct {
    uint64_t tick, terminal_tick, sequence, entry_time;
    unsigned actor, weapon, group, index, entry_known, angular, backend, late;
    uint32_t thread, owner_context;
    uint32_t requested, entry_current, entry_rate, entry_target, entry_mxcsr;
    uint32_t terminal_n, terminal_current, pi, native_result_before_store, terminal_mxcsr;
    volatile unsigned ready;
} ik_target_row;
typedef struct {uintptr_t joint, stack; unsigned row;} ik_target_pending;
static ik_target_binding ik_target_bindings[IK_TARGET_JOINTS];
static ik_target_binding ik_target_staging[IK_TARGET_JOINTS];
static unsigned ik_target_joint_count, ik_target_object_count;
static struct {uint64_t runtime; unsigned actor, weapon;} ik_target_objects[IK_TARGET_OBJECTS];
static ik_target_row ik_target_rows[IK_TARGET_ROWS];
static uint64_t ik_target_count, ik_target_ready_count, ik_target_overflow;
static uint64_t ik_target_unknown, ik_target_reentry, ik_target_read_failed;
static uint64_t ik_target_unmatched, ik_target_late_ready, ik_target_outside;
static uint64_t ik_target_foreign_thread;
static uint32_t ik_target_first, ik_target_last;
static DWORD ik_target_owner;
static int ik_target_enabled, ik_target_ever_installed;
static const char *ik_target_stage = "off";
static unsigned char *ik_target_replay;
static unsigned char ik_target_replay_bytes[16];
static unsigned __int128 ik_target_original[2], ik_target_patched[2];
static int (*ik_target_swap)(void *, unsigned __int128, unsigned __int128) = stock_trap_swap;
static int (*ik_target_close)(void *) = gate_code_close;
static __thread ik_target_pending ik_target_pending_rows[IK_TARGET_DEPTH];
static __thread unsigned ik_target_pending_count, ik_target_in_callback;
static __thread unsigned ik_target_suppressed_depth;
__attribute__((used)) static uint32_t ik_target_xstate_mask;
__attribute__((used)) static uintptr_t ik_target_entry_replay, ik_target_terminal_continue;
#ifdef GATE_TLS_TEST
static void (*ik_target_test_arm_hook)(unsigned);
static int ik_target_test_clobber;
#endif

static int ik_target_parent(uintptr_t object, uintptr_t root, uint64_t root_runtime) {
    for (unsigned depth = 0; object && depth < 16; depth++) {
        uint64_t runtime = 0;
        if (!gate_read_local((void *)(object + 8), &runtime, 8)) return 0;
        if (object == root) return runtime == root_runtime;
        uintptr_t parent = 0;
        if (object > UINTPTR_MAX - 0x70u ||
            !gate_read_local((void *)(object + 0x70u), &parent, 8) || parent == object) return 0;
        object = parent;
    }
    return 0;
}
static int ik_target_binding_alive(const ik_target_binding *binding) {
    uint64_t runtime = 0;
    uintptr_t vtable = 0;
    return gate_read_local((void *)(binding->object + 8), &runtime, 8) && runtime == binding->runtime &&
        gate_read_local((void *)binding->joint, &vtable, 8) && vtable == binding->vtable &&
        ik_target_parent(binding->object, binding->root, binding->root_runtime);
}
static const ik_target_binding *ik_target_find(uintptr_t joint) {
    unsigned count = __atomic_load_n(&ik_target_joint_count, __ATOMIC_ACQUIRE);
    for (unsigned i = 0; i < count; i++) {
        const ik_target_binding *binding = &ik_target_bindings[i];
        if (binding->joint == joint) return ik_target_binding_alive(binding) ? binding : NULL;
    }
    return NULL;
}
static ik_target_row *ik_target_reserve(const ik_target_binding *binding, uint64_t tick, unsigned *index) {
    uint64_t reserved = __atomic_fetch_add(&ik_target_count, 1, __ATOMIC_RELAXED);
    if (reserved >= IK_TARGET_ROWS) {
        __atomic_add_fetch(&ik_target_overflow, 1, __ATOMIC_RELAXED);
        return NULL;
    }
    *index = (unsigned)reserved;
    ik_target_row *row = &ik_target_rows[*index];
    row->tick = tick;
    row->sequence = reserved + 1;
    row->actor = binding->actor;
    row->weapon = binding->weapon;
    row->group = binding->group;
    row->index = binding->index;
    row->thread = GetCurrentThreadId();
    row->owner_context = row->thread == ik_target_owner;
    return row;
}
static uint32_t ik_target_xmm_word(const unsigned char *state, unsigned reg) {
    uint32_t word;
    memcpy(&word, state + 160u + reg * 16u, 4);
    return word;
}
/* Called after returning from the signal handler, with all machine state saved.
 * saved[13] is original RBX (joint); state is standard XSAVE, not compacted.
 * Terminal state already includes the ORIGINAL arithmetic's XMM0/XMM3/MXCSR.
 * terminal_n was copied before those operations. No callback invokes the engine.
 */
__attribute__((used, noinline)) static void ik_target_observe(
    const uint64_t *saved, const unsigned char *state, unsigned terminal, uint32_t terminal_n) {
    int incoming_errno = errno;
#ifdef GATE_TLS_TEST
    if (ik_target_test_clobber) {
        __asm__ volatile("pxor %%xmm0,%%xmm0; pxor %%xmm3,%%xmm3; fld1; fstp %%st(0)" ::: "xmm0", "xmm3");
        const uint32_t changed_mxcsr = 0x5f80u;
        __asm__ volatile("ldmxcsr %0" :: "m"(changed_mxcsr));
    }
#endif
    if (ik_target_in_callback) {
        __atomic_add_fetch(&ik_target_reentry, 1, __ATOMIC_RELAXED);
        errno = incoming_errno;
        return;
    }
    ik_target_in_callback = 1;
    uintptr_t joint = (uintptr_t)saved[13];
    /* 256 protected red-zone bytes plus flags and15 saved GPRs. The pinned
     * normalizer has the same RSP at both sites, including its fmodf fallbacks. */
    uintptr_t stack = (uintptr_t)saved + 0x180u;
    uint64_t tick = __atomic_load_n(&stats.issued, __ATOMIC_ACQUIRE);
    int enabled = __atomic_load_n(&ik_target_enabled, __ATOMIC_ACQUIRE) &&
        !__atomic_load_n(&ik_target_stopping, __ATOMIC_ACQUIRE);
    ik_target_row *row = NULL;
    if (!terminal) {
        /* A new invocation reusing this frame invalidates any older pending
         * entry even when capture is disabled or its row buffer is full. */
        for (unsigned i = 0; i < ik_target_pending_count; i++) {
            if (ik_target_pending_rows[i].stack != stack) continue;
            __atomic_add_fetch(&ik_target_unmatched, ik_target_pending_count - i, __ATOMIC_RELAXED);
            ik_target_pending_count = i;
            break;
        }
    }
    if (ik_target_suppressed_depth) {
        if (terminal) --ik_target_suppressed_depth;
        else ++ik_target_suppressed_depth;
        goto done;
    }
    if (terminal && ik_target_pending_count &&
        ik_target_pending_rows[ik_target_pending_count - 1].joint == joint &&
        ik_target_pending_rows[ik_target_pending_count - 1].stack == stack) {
        unsigned index = ik_target_pending_rows[--ik_target_pending_count].row;
        row = &ik_target_rows[index];
    }
    if (enabled && GetCurrentThreadId() != ik_target_owner)
        __atomic_add_fetch(&ik_target_foreign_thread, 1, __ATOMIC_RELAXED);
    if (!row) {
        if (!enabled || tick < ik_target_first || tick > ik_target_last) {
            if (enabled) __atomic_add_fetch(&ik_target_outside, 1, __ATOMIC_RELAXED);
            goto done;
        }
        const ik_target_binding *binding = ik_target_find(joint);
        if (!binding) {
            __atomic_add_fetch(&ik_target_unknown, 1, __ATOMIC_RELAXED);
            goto done;
        }
        if (!terminal && ik_target_pending_count == IK_TARGET_DEPTH) {
            __atomic_add_fetch(&ik_target_reentry, 1, __ATOMIC_RELAXED);
            ik_target_suppressed_depth = 1;
            goto done;
        }
        unsigned index = 0;
        row = ik_target_reserve(binding, tick, &index);
        if (!row) goto done;
        if (!terminal) {
            uint8_t modes[2] = {0};
            row->requested = ik_target_xmm_word(state, 0);
            memcpy(&row->entry_mxcsr, state + 24, 4);
            int read_ok = gate_read_local((void *)(joint + 8), &row->entry_current, 4) &&
                gate_read_local((void *)(joint + 0x10u), &row->entry_time, 8) &&
                gate_read_local((void *)(joint + 0x18u), &row->entry_rate, 4) &&
                gate_read_local((void *)(joint + 0x1cu), &row->entry_target, 4) &&
                gate_read_local((void *)(joint + 0x21u), &modes[0], 1) &&
                gate_read_local(LX(IK_TARGET_BACKEND), &modes[1], 1);
            row->angular = modes[0];
            row->backend = modes[1];
            row->entry_known = read_ok != 0;
            if (!read_ok) __atomic_add_fetch(&ik_target_read_failed, 1, __ATOMIC_RELAXED);
            ik_target_pending_rows[ik_target_pending_count++] = (ik_target_pending){joint, stack, index};
            goto done;
        }
        __atomic_add_fetch(&ik_target_unmatched, 1, __ATOMIC_RELAXED);
    }
    row->terminal_tick = tick;
    row->terminal_n = terminal_n;
    row->terminal_current = ik_target_xmm_word(state, 7);
    row->pi = ik_target_xmm_word(state, 6);
    row->native_result_before_store = ik_target_xmm_word(state, 0);
    memcpy(&row->terminal_mxcsr, state + 24, 4);
    row->late = !enabled || __atomic_load_n(&ik_target_stopping, __ATOMIC_ACQUIRE);
    if (row->late) __atomic_add_fetch(&ik_target_late_ready, 1, __ATOMIC_RELAXED);
    __atomic_store_n(&row->ready, 1, __ATOMIC_RELEASE);
    __atomic_add_fetch(&ik_target_ready_count, 1, __ATOMIC_RELEASE);
done:
    ik_target_in_callback = 0;
    errno = incoming_errno;
}

/* Same tested red-zone/XSTATE preservation contract as STATE/RANGE. The entry
 * replay page relocates the ORIGINAL FLD to its ORIGINAL pinned constant.
 * Terminal replay is the original11bytes, once; save its actual SIMD results
 * and MXCSR before C, then restore them before the original jump/store. */
#define IK_TARGET_SAVE \
    "lea -256(%rsp),%rsp\n\tpushfq\n\t" \
    "push %rax\n\tpush %rbx\n\tpush %rcx\n\tpush %rdx\n\tpush %rbp\n\t" \
    "push %rsi\n\tpush %rdi\n\tpush %r8\n\tpush %r9\n\tpush %r10\n\t" \
    "push %r11\n\tpush %r12\n\tpush %r13\n\tpush %r14\n\tpush %r15\n\t" \
    "mov %rsp,%r12\n\tsub $4096,%rsp\n\ttestb $0,(%rsp)\n\t" \
    "sub $256,%rsp\n\tand $-64,%rsp\n\t" \
    "mov %rsp,%rdi\n\txor %eax,%eax\n\tmov $512,%ecx\n\tcld\n\trep stosq\n\t" \
    "mov ik_target_xstate_mask(%rip),%eax\n\txor %edx,%edx\n\txsave64 (%rsp)\n\t"
#define IK_TARGET_RESTORE \
    "mov ik_target_xstate_mask(%rip),%eax\n\txor %edx,%edx\n\txrstor64 (%rsp)\n\t" \
    "mov %r12,%rsp\n\tpop %r15\n\tpop %r14\n\tpop %r13\n\tpop %r12\n\t" \
    "pop %r11\n\tpop %r10\n\tpop %r9\n\tpop %r8\n\tpop %rdi\n\tpop %rsi\n\t" \
    "pop %rbp\n\tpop %rdx\n\tpop %rcx\n\tpop %rbx\n\tpop %rax\n\t" \
    "popfq\n\tlea 256(%rsp),%rsp\n\t"
static __attribute__((naked, used, noinline)) void ik_target_entry(void) {
    __asm__ volatile(IK_TARGET_SAVE
        "mov %r12,%rdi\n\tmov %rsp,%rsi\n\txor %edx,%edx\n\txor %ecx,%ecx\n\t"
        "call ik_target_observe\n\t"
        IK_TARGET_RESTORE
        "jmp *ik_target_entry_replay(%rip)\n\t");
}
static __attribute__((naked, used, noinline)) void ik_target_terminal(void) {
    __asm__ volatile(IK_TARGET_SAVE
        "movd %xmm3,%ecx\n\t"
        "subss %xmm6,%xmm3\n\tmovaps %xmm3,%xmm0\n\taddss %xmm7,%xmm0\n\t"
        "movaps %xmm0,160(%rsp)\n\tmovaps %xmm3,208(%rsp)\n\tstmxcsr 24(%rsp)\n\t"
        "mov %r12,%rdi\n\tmov %rsp,%rsi\n\tmov $1,%edx\n\tcall ik_target_observe\n\t"
        IK_TARGET_RESTORE
        "jmp *ik_target_terminal_continue(%rip)\n\t");
}
#undef IK_TARGET_SAVE
#undef IK_TARGET_RESTORE

static unsigned char *ik_target_line(unsigned site) {
    return LX(site ? IK_TARGET_TERMINAL_LINE : IK_TARGET_ENTRY_SITE);
}
static int ik_target_replay_ok(void) {
    unsigned char bytes[16];
    return ik_target_replay && gate_read_local(ik_target_replay, bytes, sizeof(bytes)) &&
        !memcmp(bytes, ik_target_replay_bytes, sizeof(bytes));
}
static int ik_target_rx(void *address) {
    char permissions[5], path[PATH_MAX];
    return gate_maps_find((uintptr_t)address, permissions, path, sizeof(path)) && !strcmp(permissions, "r-xp");
}
static int ik_target_pins(void) {
    unsigned char function[IK_TARGET_FUNCTION_SIZE];
    uint32_t tau = 0, pi = 0;
    if (!image || !gate_read_local(LX(IK_TARGET_FUNCTION), function, sizeof(function)) ||
        !gate_read_local(LX(IK_TARGET_TAU), &tau, 4) || tau != 0x40c90fdbu ||
        !gate_read_local(LX(IK_TARGET_PI), &pi, 4) || pi != 0x40490fdbu) return 0;
    for (unsigned site = 0; site < 2; site++) {
        if (!(ik_target_installed & (1u << site))) continue;
        unsigned __int128 current;
        if (!gate_read_local(ik_target_line(site), &current, 16) || current != ik_target_patched[site]) return 0;
        unsigned offset = (site ? IK_TARGET_TERMINAL_LINE : IK_TARGET_ENTRY_SITE) - IK_TARGET_FUNCTION;
        memcpy(function + offset, &ik_target_original[site], 16);
    }
    return hash_ok(function, sizeof(function), "5eb63f9985030adbb2ff5271f96f756aa7ae8b15a213d95494d6b0a7d5e2b019");
}
static int ik_target_build_replay(void) {
    unsigned char *page = gate_near_page(LX(IK_TARGET_ENTRY_SITE));
    if (!page) return 0;
    memset(page, 0xcc, GATE_PAGE);
    memcpy(page, LX(IK_TARGET_ENTRY_SITE), 6);
    intptr_t displacement = (intptr_t)LX(IK_TARGET_TAU) - (intptr_t)(page + 6);
    intptr_t back = (intptr_t)LX(IK_TARGET_ENTRY_CONTINUE) - (intptr_t)(page + 11);
    if (displacement < INT32_MIN || displacement > INT32_MAX || back < INT32_MIN || back > INT32_MAX) {
        munmap(page, GATE_PAGE);
        return 0;
    }
    int32_t relative = (int32_t)displacement;
    memcpy(page + 2, &relative, 4);
    page[6] = 0xe9;
    relative = (int32_t)back;
    memcpy(page + 7, &relative, 4);
    if (mprotect(page, GATE_PAGE, PROT_READ | PROT_EXEC)) {
        munmap(page, GATE_PAGE);
        return 0;
    }
    memcpy(ik_target_replay_bytes, page, 16);
    ik_target_replay = page;
    return 1;
}
/* Shared trap lifecycle lock held. Any failed CAS may have installed its byte;
 * inspect ownership and RX independently. Foreign bytes are never overwritten. */
static void ik_target_cleanup_locked(void) {
    for (unsigned site = 0; site < 2; site++) {
        unsigned bit = 1u << site;
        if (!(ik_target_cleanup & bit)) continue;
        unsigned __int128 current = 0;
        int readable = gate_read_local(ik_target_line(site), &current, 16);
        if (readable && current == ik_target_patched[site])
            (void)ik_target_swap(ik_target_line(site), ik_target_patched[site], ik_target_original[site]);
        readable = gate_read_local(ik_target_line(site), &current, 16);
        pthread_mutex_lock(&gate_code_lock);
        int rx = ik_target_close(ik_target_line(site));
        pthread_mutex_unlock(&gate_code_lock);
        int synced = stock_trap_sync();
        if (readable && current == ik_target_original[site] && rx && ik_target_rx(ik_target_line(site)) && synced) {
            ik_target_cleanup &= ~bit;
            ik_target_installed &= ~bit;
        }
    }
    ik_target_restored = !ik_target_cleanup && !ik_target_installed;
    ik_target_stage = ik_target_restored ? "restored" : "cleanup_uncertain";
}
static int ik_target_arm_locked(uint32_t first, uint32_t last) {
    if (stats.active && ik_target_installed == 3 && ik_target_enabled && !ik_target_stopping &&
        first == ik_target_first && last == ik_target_last && GetCurrentThreadId() == ik_target_owner)
        return !ik_target_cleanup && stock_trap_handler_owned() && ik_target_replay_ok() && ik_target_pins();
    if (ik_target_ever_installed) {
        ik_target_stage = "one-shot";
        return 0;
    }
    if (!first || last < first || last - first >= 60u) {
        ik_target_stage = "window";
        return 0;
    }
    DWORD owner = __atomic_load_n(&ui_thread, __ATOMIC_ACQUIRE);
    if (!stats.active || !boot_mode || boot_phase || stats.issued || !owner || GetCurrentThreadId() != owner) {
        ik_target_stage = "thread_or_session";
        return 0;
    }
    if (!lxb_configured || !world_identity_installed) {
        ik_target_stage = "manifest_or_identities";
        return 0;
    }
    if (fs.state == FS_ARMED || fs.state == FS_RUNNING || cov_armed || lxis_installed || lxis_cleanup_pending() ||
        __atomic_load_n(&stock_trap_ik_order_ready, __ATOMIC_ACQUIRE)) {
        ik_target_stage = "conflict";
        return 0;
    }
    unsigned char digest[32];
    char hex[65];
    if (!gate_env.exe || !gate_file_sha256(gate_env.exe, LX_FILE_SIZE, digest)) {
        ik_target_stage = "file";
        return 0;
    }
    hex32(digest, hex);
    if (strcmp(hex, LX_FILE_SHA) || !ik_target_pins()) {
        ik_target_stage = "pins";
        return 0;
    }
    if (!ik_target_rx(ik_target_line(0)) || !ik_target_rx(ik_target_line(1))) {
        ik_target_stage = "protection";
        return 0;
    }
    if (!stock_trap_prepare() || !economy_rng_xstate()) {
        ik_target_stage = "trap_or_xstate";
        return 0;
    }
    if (!ik_target_build_replay()) {
        ik_target_stage = "replay";
        return 0;
    }
    ik_target_first = first;
    ik_target_last = last;
    ik_target_owner = owner;
    ik_target_xstate_mask = economy_rng_xstate_mask;
    ik_target_entry_replay = (uintptr_t)ik_target_replay;
    ik_target_terminal_continue = (uintptr_t)LX(IK_TARGET_TERMINAL_CONTINUE);
    for (unsigned site = 0; site < 2; site++) {
        memcpy(&ik_target_original[site], ik_target_line(site), 16);
        ik_target_patched[site] = ik_target_original[site];
        ((unsigned char *)&ik_target_patched[site])[site ? 13u : 0u] = 0xcc;
    }
    /* Never reset these identities, continuations, cache or rows after they
     * become reachable. Late entries still replay native code after STOP. */
    __atomic_store_n(&ik_target_ever_installed, 1, __ATOMIC_RELEASE);
    __atomic_store_n(&stock_trap_ik_entry_ready, 1, __ATOMIC_RELEASE);
    __atomic_store_n(&stock_trap_ik_terminal_ready, 1, __ATOMIC_RELEASE);
    for (unsigned site = 0; site < 2; site++) {
#ifdef GATE_TLS_TEST
        if (ik_target_test_arm_hook) ik_target_test_arm_hook(site + 1);
#endif
        if (__atomic_load_n(&ik_target_stopping, __ATOMIC_ACQUIRE)) goto failed;
        unsigned bit = 1u << site;
        ik_target_cleanup |= bit;
        if (!ik_target_swap(ik_target_line(site), ik_target_original[site], ik_target_patched[site])) goto failed;
        ik_target_installed |= bit;
        if (__atomic_load_n(&ik_target_stopping, __ATOMIC_ACQUIRE)) goto failed;
    }
    ik_target_cleanup = 0;
    /* No entry can reserve a row while only one trap exists. A terminal
     * whose entry predates publication is explicitly unmatched. */
    __atomic_store_n(&ik_target_enabled, 1, __ATOMIC_RELEASE);
    if (__atomic_load_n(&ik_target_stopping, __ATOMIC_ACQUIRE)) goto failed;
    ik_target_stage = "armed";
    return 1;
failed:
    __atomic_store_n(&ik_target_enabled, 0, __ATOMIC_RELEASE);
    ik_target_cleanup |= ik_target_installed;
    ik_target_cleanup_locked();
    return 0;
}
static int ik_target_arm(uint32_t first, uint32_t last) {
    pthread_mutex_lock(&stock_state_lifecycle_lock);
    int ok = !__atomic_load_n(&ik_target_stopping, __ATOMIC_ACQUIRE) && ik_target_arm_locked(first, last);
    pthread_mutex_unlock(&stock_state_lifecycle_lock);
    return ok;
}
static int ik_target_check(void) {
    if (!__atomic_load_n(&ik_target_ever_installed, __ATOMIC_ACQUIRE)) return 1;
    pthread_mutex_lock(&stock_state_lifecycle_lock);
    int ok = __atomic_load_n(&ik_target_stopping, __ATOMIC_ACQUIRE) ||
        (ik_target_installed == 3 && !ik_target_cleanup && stock_trap_handler_owned() &&
         !stock_trap_sync_uncertain && ik_target_replay_ok() && ik_target_rx(ik_target_replay) &&
         ik_target_rx(ik_target_line(0)) && ik_target_pins());
    if (!ok) ik_target_stage = "ownership";
    pthread_mutex_unlock(&stock_state_lifecycle_lock);
    return ok;
}
static void ik_target_restore(void) {
    __atomic_store_n(&ik_target_stopping, 1, __ATOMIC_RELEASE);
    __atomic_store_n(&ik_target_enabled, 0, __ATOMIC_RELEASE);
    pthread_mutex_lock(&stock_state_lifecycle_lock);
    ik_target_cleanup |= ik_target_installed;
    if (ik_target_cleanup) ik_target_cleanup_locked();
    pthread_mutex_unlock(&stock_state_lifecycle_lock);
}
static void ik_target_begin(void) {
    pthread_mutex_lock(&stock_state_lifecycle_lock);
    if (!ik_target_ever_installed && !ik_target_cleanup)
        __atomic_store_n(&ik_target_stopping, 0, __ATOMIC_RELEASE);
    pthread_mutex_unlock(&stock_state_lifecycle_lock);
}

static int ik_target_bind(unsigned actor, unsigned weapon, uint64_t runtime) {
    if (!stats.active || GetCurrentThreadId() != ik_target_owner || !ik_target_enabled ||
        ik_target_installed != 3 || ik_target_stopping || stats.issued >= ik_target_first ||
        actor < 1 || actor > LXB_ACTORS || weapon > 32 || !runtime ||
        !lxb_configured || lxb_bound != LXB_ACTORS) return 0;
    const lxb_actor *fixture = &lxb_actors[actor - 1];
    unsigned char *object = lxik_object(runtime);
    if (!fixture->enrolled || !fixture->object || !object ||
        (!weapon && object != fixture->object) ||
        !ik_target_parent((uintptr_t)object, (uintptr_t)fixture->object, fixture->runtime)) return 0;
    for (unsigned i = 0; i < ik_target_object_count; i++) {
        if (ik_target_objects[i].actor == actor && ik_target_objects[i].weapon == weapon)
            return ik_target_objects[i].runtime == runtime;
        if (ik_target_objects[i].runtime == runtime) return 0;
    }
    if (ik_target_object_count == IK_TARGET_OBJECTS) return 0;
    unsigned char *container = NULL, *skeleton = NULL;
    uintptr_t groups[2] = {0};
    if (!gate_read_local(object + LXIK_CONTAINER, &container, 8)) return 0;
    unsigned count = 0;
    if (container) {
        if (!gate_read_local(container + LXIK_SKELETON, &skeleton, 8)) return 0;
        if (skeleton) {
            if (!gate_read_local(skeleton + LXIK_GROUP_VEC, groups, 16) || groups[1] < groups[0] ||
                (groups[1] - groups[0]) % 0x18u || (groups[1] - groups[0]) / 0x18u > LXIK_GROUPS) return 0;
            for (uintptr_t group = groups[0]; group < groups[1]; group += 0x18u) {
                uintptr_t joints[2] = {0};
                if (!gate_read_local((void *)group, joints, 16) || joints[1] < joints[0] ||
                    (joints[1] - joints[0]) % LXIK_JOINT || (joints[1] - joints[0]) / LXIK_JOINT > LXIK_JOINTS) return 0;
                for (uintptr_t joint = joints[0]; joint < joints[1]; joint += LXIK_JOINT) {
                    if (count >= IK_TARGET_JOINTS - ik_target_joint_count) return 0;
                    uintptr_t vtable = 0;
                    if (!gate_read_local((void *)joint, &vtable, 8) || !vtable) return 0;
                    for (unsigned i = 0; i < ik_target_joint_count; i++)
                        if (ik_target_bindings[i].joint == joint) return 0;
                    for (unsigned i = 0; i < count; i++)
                        if (ik_target_staging[i].joint == joint) return 0;
                    ik_target_staging[count++] = (ik_target_binding){joint, vtable, (uintptr_t)object,
                        (uintptr_t)fixture->object, runtime, fixture->runtime, actor, weapon,
                        (unsigned)((group - groups[0]) / 0x18u), (unsigned)((joint - joints[0]) / LXIK_JOINT)};
                }
            }
        }
    }
    memcpy(ik_target_bindings + ik_target_joint_count, ik_target_staging, count * sizeof(ik_target_binding));
    __atomic_store_n(&ik_target_joint_count, ik_target_joint_count + count, __ATOMIC_RELEASE);
    ik_target_objects[ik_target_object_count].actor = actor;
    ik_target_objects[ik_target_object_count].weapon = weapon;
    ik_target_objects[ik_target_object_count++].runtime = runtime;
    return 1;
}
static int ik_target_status_text(char *out, size_t size) {
    uint64_t reserved = __atomic_load_n(&ik_target_count, __ATOMIC_ACQUIRE);
    uint64_t ready = __atomic_load_n(&ik_target_ready_count, __ATOMIC_ACQUIRE);
    uint64_t retained = reserved < IK_TARGET_ROWS ? reserved : IK_TARGET_ROWS;
    uint64_t pending = retained > ready ? retained - ready : 0;
    int n = snprintf(out, size,
        "installed=%d sites=%d restored=%d cleanup=%d stage=%s first=%u last=%u bound_objects=%u bound_joints=%u "
        "rows=%llu ready=%llu pending=%llu overflow=%llu unknown=%llu reentry=%llu read_failed=%llu unmatched=%llu late_ready=%llu outside=%llu foreign_thread=%llu owner=%u result=before_store",
        ik_target_installed == 3, ik_target_installed, ik_target_restored, ik_target_cleanup, ik_target_stage, ik_target_first, ik_target_last,
        ik_target_object_count, ik_target_joint_count, (unsigned long long)reserved,
        (unsigned long long)ready, (unsigned long long)pending,
        (unsigned long long)__atomic_load_n(&ik_target_overflow, __ATOMIC_RELAXED),
        (unsigned long long)__atomic_load_n(&ik_target_unknown, __ATOMIC_RELAXED),
        (unsigned long long)__atomic_load_n(&ik_target_reentry, __ATOMIC_RELAXED),
        (unsigned long long)__atomic_load_n(&ik_target_read_failed, __ATOMIC_RELAXED),
        (unsigned long long)__atomic_load_n(&ik_target_unmatched, __ATOMIC_RELAXED),
        (unsigned long long)__atomic_load_n(&ik_target_late_ready, __ATOMIC_RELAXED),
        (unsigned long long)__atomic_load_n(&ik_target_outside, __ATOMIC_RELAXED),
        (unsigned long long)__atomic_load_n(&ik_target_foreign_thread, __ATOMIC_RELAXED), ik_target_owner);
    return n > 0 && (size_t)n < size ? n : 0;
}
static int ik_target_row_text(unsigned index, char *out, size_t size) {
    if (index >= IK_TARGET_ROWS || !__atomic_load_n(&ik_target_rows[index].ready, __ATOMIC_ACQUIRE)) return 0;
    const ik_target_row *row = &ik_target_rows[index];
    int n = snprintf(out, size,
        "tick=%llu actor=%u weapon=%u group=%u index=%u sequence=%llu entry_known=%u requested=%08x "
        "entry_current=%08x entry_time=%016llx entry_rate=%08x entry_target=%08x angular=%u backend=%u entry_mxcsr=%08x "
        "terminal_n=%08x terminal_current=%08x pi=%08x native_result_before_store=%08x terminal_tick=%llu terminal_mxcsr=%08x late=%u thread=%u owner_context=%u",
        (unsigned long long)row->tick, row->actor, row->weapon, row->group, row->index,
        (unsigned long long)row->sequence, row->entry_known, row->requested, row->entry_current,
        (unsigned long long)row->entry_time, row->entry_rate, row->entry_target, row->angular, row->backend,
        row->entry_mxcsr, row->terminal_n, row->terminal_current, row->pi, row->native_result_before_store,
        (unsigned long long)row->terminal_tick, row->terminal_mxcsr, row->late, row->thread, row->owner_context);
    return n > 0 && (size_t)n < size ? n : 0;
}
static int ik_target_number(double value, double maximum) {
    return isfinite(value) && value >= 0 && value <= maximum && floor(value) == value;
}
static int lua_ik_target_observer(lua_State *L) {
    double first = p_lua_tonumber(L, 1), last = p_lua_tonumber(L, 2);
    int ok = accept_ui() && ik_target_number(first, UINT32_MAX) && ik_target_number(last, UINT32_MAX) &&
        ik_target_arm((uint32_t)first, (uint32_t)last);
    p_lua_pushnumber(L, ok);
    return 1;
}
static int lua_ik_target_bind(lua_State *L) {
    double actor = p_lua_tonumber(L, 1), weapon = p_lua_tonumber(L, 2), runtime = p_lua_tonumber(L, 3);
    int ok = accept_ui() && ik_target_number(actor, LXB_ACTORS) && ik_target_number(weapon, 32) &&
        ik_target_number(runtime, 2199023255551.0) && ik_target_bind((unsigned)actor, (unsigned)weapon, (uint64_t)runtime);
    p_lua_pushnumber(L, ok);
    return 1;
}
static int lua_ik_target_status(lua_State *L) {
    char out[768];
    pthread_mutex_lock(&stock_state_lifecycle_lock);
    int length = accept_ui() ? ik_target_status_text(out, sizeof(out)) : 0;
    pthread_mutex_unlock(&stock_state_lifecycle_lock);
    if (length) p_lua_pushlstring(L, out, (size_t)length);
    else p_lua_pushnil(L);
    return 1;
}
static int lua_ik_target_row(lua_State *L) {
    double index = p_lua_tonumber(L, 1);
    char out[768];
    int length = accept_ui() && ik_target_number(index, IK_TARGET_ROWS - 1) ?
        ik_target_row_text((unsigned)index, out, sizeof(out)) : 0;
    if (length) p_lua_pushlstring(L, out, (size_t)length);
    else p_lua_pushnil(L);
    return 1;
}
