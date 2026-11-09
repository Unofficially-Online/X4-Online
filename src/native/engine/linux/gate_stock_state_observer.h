/* Experimental Linux stock STATE dispatch observer. Default off; never sends,
 * suppresses or replays input. This covers only the verified out-of-line state
 * dispatcher, not the full input stack or a stock-v3 record. A row index is
 * reserved before it becomes ready, and a callback already in flight may
 * finish after STOP. Readers must retry a missing row; a final drain is not
 * guaranteed to contain every event. last_issued is not an apply tick. */
#define STOCK_STATE_SITE 0x1050fa0u
#define STOCK_STATE_END 0x10510bcu
#define STOCK_STATE_CONTINUE 0x1050fa7u
#define STOCK_STATE_INPUT_GLOBAL 0x3daaaf8u
#define STOCK_STATE_MAP_OFFSET 0x63f0u
#define STOCK_STATE_ROWS 256u

typedef struct {
    uint64_t tick;
    uint32_t id, value, thread, known_context;
    volatile unsigned ready;
} stock_state_row;
static stock_state_row stock_state_rows[STOCK_STATE_ROWS];
static uint64_t stock_state_count, stock_state_overflow, stock_state_reentry;
static int stock_state_enabled, stock_state_ever_installed;
static DWORD stock_state_owner_thread;
static int (*stock_state_swap)(void *, unsigned __int128, unsigned __int128) = stock_trap_swap;
static int (*stock_state_close)(void *) = gate_code_close;
static unsigned __int128 stock_state_original, stock_state_patched;
static const char *stock_state_stage = "off";
static __thread unsigned stock_state_in_callback;
#ifdef GATE_TLS_TEST
static int stock_state_test_clobber;
static void (*stock_state_test_arm_hook)(int);
#ifdef ECONOMY_RNG_CANARY
static void economy_rng_canary_clobber(void);
#endif
#endif
__attribute__((used)) static uint32_t stock_state_xstate_mask;
__attribute__((used)) static uintptr_t stock_state_continuation;
static const unsigned char stock_state_block[16] = {
    0x48, 0x8b, 0x4f, 0x48, 0x48, 0x85, 0xc9, 0x0f,
    0x84, 0x0e, 0x01, 0x00, 0x00, 0x41, 0x55, 0x4c
};

/* The stub passes its saved-register frame: see the pushes below. Read the
 * singleton safely; an unknown thread or owner is reported, never relabeled as
 * a known frame. The record is diagnostic and contains no game pointer. */
__attribute__((used, noinline)) static void stock_state_observe(const uint64_t *saved_registers) {
    int old_errno = errno;
#ifdef GATE_TLS_TEST
    if (stock_state_test_clobber) __asm__ volatile("pxor %%xmm0,%%xmm0" ::: "xmm0");
#ifdef ECONOMY_RNG_CANARY
    if (stock_state_test_clobber == 2) economy_rng_canary_clobber();
#endif
#endif
    if (stock_state_in_callback) {
        __atomic_add_fetch(&stock_state_reentry, 1, __ATOMIC_RELAXED);
        errno = old_errno;
        return;
    }
    stock_state_in_callback = 1;
    if (__atomic_load_n(&stock_state_enabled, __ATOMIC_ACQUIRE)) {
        uintptr_t input_owner = 0, input_map = (uintptr_t)saved_registers[8];
        uint32_t current_thread = GetCurrentThreadId();
        uint64_t last_issued = __atomic_load_n(&stock_state_last_issued, __ATOMIC_ACQUIRE);
        int thread_and_map_match = current_thread == stock_state_owner_thread && last_issued &&
            gate_read_local(LX(STOCK_STATE_INPUT_GLOBAL), &input_owner, sizeof(input_owner)) &&
            input_owner && input_owner <= UINTPTR_MAX - STOCK_STATE_MAP_OFFSET && input_map == input_owner + STOCK_STATE_MAP_OFFSET;
        uint64_t row_index = __atomic_fetch_add(&stock_state_count, 1, __ATOMIC_RELAXED);
        if (row_index < STOCK_STATE_ROWS) {
            stock_state_row *row = &stock_state_rows[row_index];
            row->tick = last_issued;
            row->id = (uint32_t)saved_registers[9];
            row->value = (unsigned char)saved_registers[11];
            row->thread = current_thread;
            row->known_context = (unsigned)thread_and_map_match;
            __atomic_store_n(&row->ready, 1, __ATOMIC_RELEASE);
        } else {
            __atomic_add_fetch(&stock_state_overflow, 1, __ATOMIC_RELAXED);
        }
    }
    stock_state_in_callback = 0;
    errno = old_errno;
}

/* Keep the interrupted red zone, all registers, flags, x87 and enabled
 * supported vector state across the helper. Restore the original instructions
 * once, including TEST's flags, then jump to the original JE. The near stub
 * remains available for process lifetime, including late traps after disarm. */
__attribute__((naked, used, noinline)) static void stock_state_entry(void) {
    __asm__ volatile(
        "lea -256(%rsp),%rsp\n\tpushfq\n\t"
        "push %rax\n\tpush %rbx\n\tpush %rcx\n\tpush %rdx\n\tpush %rbp\n\t"
        "push %rsi\n\tpush %rdi\n\tpush %r8\n\tpush %r9\n\tpush %r10\n\t"
        "push %r11\n\tpush %r12\n\tpush %r13\n\tpush %r14\n\tpush %r15\n\t"
        "mov %rsp,%r12\n\tsub $4096,%rsp\n\ttestb $0,(%rsp)\n\t"
        "sub $256,%rsp\n\tand $-64,%rsp\n\t"
        "mov %rsp,%rdi\n\txor %eax,%eax\n\tmov $512,%ecx\n\tcld\n\trep stosq\n\t"
        "mov stock_state_xstate_mask(%rip),%eax\n\txor %edx,%edx\n\txsave64 (%rsp)\n\t"
        "mov %r12,%rdi\n\tcall stock_state_observe\n\t"
        "mov stock_state_xstate_mask(%rip),%eax\n\txor %edx,%edx\n\txrstor64 (%rsp)\n\t"
        "mov %r12,%rsp\n\tpop %r15\n\tpop %r14\n\tpop %r13\n\tpop %r12\n\tpop %r11\n\t"
        "pop %r10\n\tpop %r9\n\tpop %r8\n\tpop %rdi\n\tpop %rsi\n\t"
        "pop %rbp\n\tpop %rdx\n\tpop %rcx\n\tpop %rbx\n\tpop %rax\n\t"
        "popfq\n\tlea 256(%rsp),%rsp\n\t"
        "mov 0x48(%rdi),%rcx\n\ttest %rcx,%rcx\n\tjmp *stock_state_continuation(%rip)\n\t");
}
static int stock_state_owned(void) {
    unsigned __int128 current;
    return stock_state_installed && stock_trap_handler_owned() && !stock_trap_sync_uncertain &&
        gate_read_local(LX(STOCK_STATE_SITE), &current, sizeof(current)) && current == stock_state_patched;
}
static int stock_state_pins(void) {
    unsigned char body[STOCK_STATE_END - STOCK_STATE_SITE];
    if (!image || !gate_read_local(LX(STOCK_STATE_SITE), body, sizeof(body))) return 0;
    if (stock_state_installed) {
        if (!stock_state_owned()) return 0;
        memcpy(body, stock_state_block, sizeof(stock_state_block));
    } else if (memcmp(body, stock_state_block, sizeof(stock_state_block))) return 0;
    return hash_ok(body, sizeof(body), "86f89b2beec85f1893a4ad140d4d36bae995ea1fa107ef9136b5d66c50bc41d3");
}
/* Caller holds lifecycle lock. The swap result and RX restoration are separate
 * facts: inspect the whole line, never replace a foreign owner, and keep the
 * cleanup obligation until both original bytes and RX are confirmed. */
static void stock_state_cleanup_locked(void) {
    unsigned __int128 current = 0;
    int readable = gate_read_local(LX(STOCK_STATE_SITE), &current, sizeof(current));
    if (readable && current == stock_state_patched)
        (void)stock_state_swap(LX(STOCK_STATE_SITE), stock_state_patched, stock_state_original);
    readable = gate_read_local(LX(STOCK_STATE_SITE), &current, sizeof(current));
    pthread_mutex_lock(&gate_code_lock);
    int rx = stock_state_close(LX(STOCK_STATE_SITE));
    pthread_mutex_unlock(&gate_code_lock);
    if (readable && current == stock_state_original && rx && stock_trap_sync()) {
        __atomic_store_n(&stock_state_installed, 0, __ATOMIC_RELEASE);
        stock_state_cleanup_uncertain = 0;
        stock_state_restored = 1;
        stock_state_stage = "restored";
    } else {
        stock_state_cleanup_uncertain = 1;
        stock_state_restored = 0;
        stock_state_stage = "restore_ownership";
    }
}
static int stock_state_arm_locked(void) {
    unsigned char sha[32];
    char hex[65];
    if (stock_state_installed) return stock_state_pins();
    if (stock_state_ever_installed) {
        stock_state_stage = "one-shot";
        return 0;
    }
    DWORD owner = __atomic_load_n(&ui_thread, __ATOMIC_ACQUIRE);
    if (!__atomic_load_n(&stats.active, __ATOMIC_ACQUIRE) || !owner || GetCurrentThreadId() != owner) {
        stock_state_stage = "thread_or_session";
        return 0;
    }
    if (fs.state == FS_ARMED || fs.state == FS_RUNNING) {
        stock_state_stage = "fork_save";
        return 0;
    }
    if (cov_armed) {
        stock_state_stage = "coverage";
        return 0;
    }
#ifdef GATE_TLS_TEST
    if (stock_state_test_arm_hook) stock_state_test_arm_hook(1);
#endif
    if (!gate_env.exe || !gate_file_sha256(gate_env.exe, LX_FILE_SIZE, sha)) {
        stock_state_stage = "file";
        return 0;
    }
    if (__atomic_load_n(&stock_state_stopping, __ATOMIC_ACQUIRE)) {
        stock_state_stage = "stopping";
        return 0;
    }
    hex32(sha, hex);
    if (strcmp(hex, LX_FILE_SHA) || !stock_state_pins()) {
        stock_state_stage = "pins";
        return 0;
    }
    if (!stock_trap_prepare()) {
        stock_state_stage = "trap_contract";
        return 0;
    }
    if (!economy_rng_xstate()) {
        stock_state_stage = "xstate";
        return 0;
    }
    stock_state_xstate_mask = economy_rng_xstate_mask;
    /* Published sites stay ready after STOP for late delivered traps. */
    stock_state_ever_installed = 1;
    stock_state_continuation = (uintptr_t)LX(STOCK_STATE_CONTINUE);
    __atomic_store_n(&stock_trap_state_ready, 1, __ATOMIC_RELEASE);
    memcpy(&stock_state_original, stock_state_block, 16);
    stock_state_patched = stock_state_original;
    unsigned char *bytes = (unsigned char *)&stock_state_patched;
    bytes[0] = 0xcc;
    stock_state_count = stock_state_overflow = stock_state_reentry = 0;
    memset(stock_state_rows, 0, sizeof(stock_state_rows));
    stock_state_restored = 0;
    stock_state_owner_thread = owner;
    if (__atomic_load_n(&stock_state_stopping, __ATOMIC_ACQUIRE)) {
        stock_state_stage = "stopping";
        return 0;
    }
    __atomic_store_n(&stock_state_enabled, 1, __ATOMIC_RELEASE);
    stock_state_cleanup_uncertain = 1;
    /* CAS may succeed even if RX restoration fails. */
#ifdef GATE_TLS_TEST
    if (stock_state_test_arm_hook) stock_state_test_arm_hook(2);
#endif
    if (__atomic_load_n(&stock_state_stopping, __ATOMIC_ACQUIRE)) {
        __atomic_store_n(&stock_state_enabled, 0, __ATOMIC_RELEASE);
        stock_state_cleanup_uncertain = 0;
        stock_state_stage = "stopping";
        return 0;
    }
    if (!stock_state_swap(LX(STOCK_STATE_SITE), stock_state_original, stock_state_patched)) {
        unsigned __int128 current;
        stock_state_cleanup_uncertain = 1;
        int maybe_installed = !gate_read_local(LX(STOCK_STATE_SITE), &current, 16) || current == stock_state_patched;
        __atomic_store_n(&stock_state_installed, maybe_installed, __ATOMIC_RELEASE);
        __atomic_store_n(&stock_state_enabled, 0, __ATOMIC_RELEASE);
        stock_state_cleanup_locked();
        if (!stock_state_cleanup_uncertain) stock_state_stage = "cas";
        return 0;
    }
    __atomic_store_n(&stock_state_installed, 1, __ATOMIC_RELEASE);
    if (__atomic_load_n(&stock_state_stopping, __ATOMIC_ACQUIRE)) {
        __atomic_store_n(&stock_state_enabled, 0, __ATOMIC_RELEASE);
        stock_state_cleanup_locked();
        return 0;
    }
    stock_state_cleanup_uncertain = 0;
    stock_state_stage = "armed";
    return 1;
}
static int stock_state_arm(void) {
    pthread_mutex_lock(&stock_state_lifecycle_lock);
    int result = __atomic_load_n(&stock_state_stopping, __ATOMIC_ACQUIRE) ? 0 : stock_state_arm_locked();
    pthread_mutex_unlock(&stock_state_lifecycle_lock);
    return result;
}
static int stock_state_check(void) {
    pthread_mutex_lock(&stock_state_lifecycle_lock);
    int ok = !stock_state_installed || __atomic_load_n(&stock_state_stopping, __ATOMIC_ACQUIRE) || stock_state_pins();
    if (!ok) stock_state_stage = "ownership";
    pthread_mutex_unlock(&stock_state_lifecycle_lock);
    return ok;
}
static void stock_state_restore(void) {
    __atomic_store_n(&stock_state_stopping, 1, __ATOMIC_RELEASE);
    __atomic_store_n(&stock_state_enabled, 0, __ATOMIC_RELEASE);
    pthread_mutex_lock(&stock_state_lifecycle_lock);
    __atomic_store_n(&stock_state_enabled, 0, __ATOMIC_RELEASE);
    if (stock_state_installed || stock_state_cleanup_uncertain) stock_state_cleanup_locked();
    pthread_mutex_unlock(&stock_state_lifecycle_lock);
}
static void stock_state_begin(void) {
    pthread_mutex_lock(&stock_state_lifecycle_lock);
    if (!stock_state_installed && !stock_state_cleanup_uncertain)
        __atomic_store_n(&stock_state_stopping, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&stock_state_last_issued, 0, __ATOMIC_RELEASE);
    pthread_mutex_unlock(&stock_state_lifecycle_lock);
}
static int lua_stock_state_observer(lua_State *L) {
    int ok = accept_ui() && stock_state_arm();
    p_lua_pushnumber(L, ok);
    return 1;
}
static int lua_stock_state_observer_status(lua_State *L) {
    char out[240];
    int n = 0;
    if (accept_ui()) {
        pthread_mutex_lock(&stock_state_lifecycle_lock);
        n = snprintf(out, sizeof(out),
        "installed=%d restored=%d cleanup_uncertain=%d stage=%s events=%llu overflow=%llu reentry=%llu partial=state_only",
        stock_state_installed, stock_state_restored, stock_state_cleanup_uncertain, stock_state_stage,
        (unsigned long long)__atomic_load_n(&stock_state_count, __ATOMIC_ACQUIRE),
        (unsigned long long)__atomic_load_n(&stock_state_overflow, __ATOMIC_RELAXED),
        (unsigned long long)__atomic_load_n(&stock_state_reentry, __ATOMIC_RELAXED));
        pthread_mutex_unlock(&stock_state_lifecycle_lock);
    }
    if (n > 0 && (size_t)n < sizeof(out)) p_lua_pushlstring(L, out, (size_t)n);
    else p_lua_pushnil(L);
    return 1;
}
static int lua_stock_state_observer_row(lua_State *L) {
    double index = p_lua_tonumber(L, 1);
    char out[180];
    int n = 0;
    if (accept_ui() && isfinite(index) && index >= 0 && index < STOCK_STATE_ROWS && floor(index) == index) {
        stock_state_row *r = &stock_state_rows[(unsigned)index];
        if (__atomic_load_n(&r->ready, __ATOMIC_ACQUIRE)) n = snprintf(out, sizeof(out),
            "last_issued=%llu id=%u value=%u thread=%u thread_map_match=%u", (unsigned long long)r->tick, r->id, r->value, r->thread, r->known_context);
    }
    if (n > 0 && (size_t)n < sizeof(out)) p_lua_pushlstring(L, out, (size_t)n);
    else p_lua_pushnil(L);
    return 1;
}
