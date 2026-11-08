/* Default-off Linux build-storage scheduling experiment.
 *
 * The pinned shared Container scheduler 0x1838e80 retains its object in RBX and
 * calls FloatRange at 0x1838f16 with the float range in XMM0. The near stub
 * supplies RBX as the wrapper's first integer argument without disturbing XMM0.
 * The original FloatRange 0x1a491c0 is called once and returns its float in
 * XMM0. Its positive-range path advances the calling thread's fs:-0x2720 word.
 *
 * A portable object key and the calling thread's game-time bits select a
 * private word during load and admitted frames. Missing identities fall back
 * to the mapped storage class and are counted; two storages of one class at
 * the same time share that fallback stream. Unknown classes run the native
 * draw, are counted and latch failure. Linux/Windows parity is not claimed.
 * The caller's TLS word is restored on normal return, including nested calls;
 * an exception or process termination does not have an unwind guarantee. */
#define LINUX_BUILD_RNG_DOMAIN 0x58344c4255494c44ULL
#define LINUX_BUILD_RNG_SCHEDULER_SIZE 0x180u
#define LINUX_BUILD_RNG_STUB_SIZE 17u
#define LINUX_BUILD_RNG_STOP_REASON 152u
#define LINUX_BUILD_RNG_ROW_CAPACITY 128u

typedef float (*linux_build_rng_draw_fn)(float);
static const unsigned char linux_build_rng_original_block[16] = {
    0x10,0x05,0x3a,0xe2,0x5e,0x01,0xe8,0xa5,
    0x02,0x21,0x00,0x31,0xd2,0xbe,0x0e,0x00
};
static const char *linux_build_rng_scheduler_hash = "c6b5cd949298ae677d231ff345d92e22295830999b43a2a949b9027fc50ff6e7";
static const char *linux_build_rng_draw_hash = "bd0f5a41ee0f72ece023eec0b6ae4134c62e345874c57fe45dc17194d190c6ea";
typedef struct {
    uint64_t identity, time_bits, seed;
    uint32_t class_rva, caller_rva, range_bits, result_bits;
    unsigned identity_class, fallback, phase;
    volatile int ready;
} linux_build_rng_row;
static linux_build_rng_row linux_build_rng_rows[LINUX_BUILD_RNG_ROW_CAPACITY];
static uint64_t linux_build_rng_row_count;
static unsigned char *linux_build_rng_page;
static unsigned __int128 linux_build_rng_original, linux_build_rng_patched;
static uint64_t linux_build_rng_keyed, linux_build_rng_loading, linux_build_rng_native;
static uint64_t linux_build_rng_unmapped, linux_build_rng_fallback;
static uint64_t linux_build_rng_load_keyed, linux_build_rng_load_fallback;
static uint64_t linux_build_rng_container, linux_build_rng_storage, linux_build_rng_other;
static volatile int64_t linux_build_rng_failures;
static const char *linux_build_rng_stage = "idle";

/* Import writers hold this mutex while resolving nodes. Resolve first, then
 * read the partially populated table under the same lock. Neither the native
 * draw nor a created-ID lookup runs while the import lock is held. */
static uint64_t linux_build_rng_identity(uint64_t runtime, unsigned *identity_class) {
    uint64_t identity = 0;
    *identity_class = WORLD_KEY_NONE;
    if (!runtime || !world_identities) return 0;
    if (!world_identity_sealed) lxw_resolve(1);
    pthread_mutex_lock(&lxw_insert_lock);
    identity = lxw_identity_probe(runtime);
    pthread_mutex_unlock(&lxw_insert_lock);
    if (identity) {
        *identity_class = WORLD_KEY_SAVED;
        return identity;
    }
    if (world_key_portable() && (identity = created_lookup(runtime))) {
        *identity_class = WORLD_KEY_CREATED;
        return identity;
    }
    return 0;
}

static float linux_build_rng_run(linux_build_rng_draw_fn native, const void *storage, float range, uintptr_t caller) {
    unsigned char *tls = gate_tls_self();
    uint64_t *word = gate_tls_rng(tls);
    double *time = gate_tls_time(tls);
    uint64_t runtime = 0, vtable = 0, time_bits = 0;
    if (!word || !time || !storage || caller != (uintptr_t)LX(LX_BUILD_CALL_VA) + 5 || range != 10.0f ||
        !gate_read_local(storage, &vtable, 8) ||
        !gate_read_local((const unsigned char *)storage + 8, &runtime, 8)) {
        __atomic_add_fetch(&linux_build_rng_unmapped, 1, __ATOMIC_RELAXED);
        InterlockedIncrement64(&linux_build_rng_failures);
        __atomic_add_fetch(&linux_build_rng_native, 1, __ATOMIC_RELAXED);
        return native(range);
    }
    double game_time;
    memcpy(&game_time, time, sizeof(game_time));
    if (!isfinite(game_time)) {
        __atomic_add_fetch(&linux_build_rng_unmapped, 1, __ATOMIC_RELAXED);
        InterlockedIncrement64(&linux_build_rng_failures);
        __atomic_add_fetch(&linux_build_rng_native, 1, __ATOMIC_RELAXED);
        return native(range);
    }
    memcpy(&time_bits, &game_time, sizeof(time_bits));
    const lxw_class *object_class = lxw_class_of((uintptr_t)vtable);
    if (!object_class || object_class->family != LXW_FAMILY_NONE) {
        __atomic_add_fetch(&linux_build_rng_unmapped, 1, __ATOMIC_RELAXED);
        InterlockedIncrement64(&linux_build_rng_failures);
        __atomic_add_fetch(&linux_build_rng_native, 1, __ATOMIC_RELAXED);
        return native(range);
    }
    if (vtable == (uintptr_t)LX(0x3c76e90u)) __atomic_add_fetch(&linux_build_rng_container, 1, __ATOMIC_RELAXED);
    else if (vtable == (uintptr_t)LX(0x3c99a10u)) __atomic_add_fetch(&linux_build_rng_storage, 1, __ATOMIC_RELAXED);
    else __atomic_add_fetch(&linux_build_rng_other, 1, __ATOMIC_RELAXED);
    unsigned phase = (unsigned)boot_phase;
    int during_load = !stats.active || phase != 3;
    unsigned identity_class = WORLD_KEY_NONE;
    uint64_t identity = linux_build_rng_identity(runtime, &identity_class);
    uint64_t seed;
    if (identity) {
        seed = lockstep_seed(LINUX_BUILD_RNG_DOMAIN, 0, identity, time_bits, 0, identity_class);
        __atomic_add_fetch(&linux_build_rng_keyed, 1, __ATOMIC_RELAXED);
        if (during_load) __atomic_add_fetch(&linux_build_rng_load_keyed, 1, __ATOMIC_RELAXED);
    } else {
        seed = lockstep_seed(LINUX_BUILD_RNG_DOMAIN, 0, 0, object_class->windows_rva, time_bits, WORLD_KEY_NONE);
        __atomic_add_fetch(&linux_build_rng_fallback, 1, __ATOMIC_RELAXED);
        if (during_load) __atomic_add_fetch(&linux_build_rng_load_fallback, 1, __ATOMIC_RELAXED);
    }
    if (during_load) __atomic_add_fetch(&linux_build_rng_loading, 1, __ATOMIC_RELAXED);
    uint64_t saved_word = *word;
    *word = seed;
    float result = native(range);
    *word = saved_word;
    if (during_load) {
        uint64_t index = __atomic_fetch_add(&linux_build_rng_row_count, 1, __ATOMIC_RELAXED);
        if (index < LINUX_BUILD_RNG_ROW_CAPACITY) {
            linux_build_rng_row *row = &linux_build_rng_rows[index];
            row->identity = identity;
            row->time_bits = time_bits;
            row->seed = seed;
            row->class_rva = object_class->windows_rva;
            row->caller_rva = (uint32_t)(caller - (uintptr_t)image);
            memcpy(&row->range_bits, &range, sizeof(range));
            memcpy(&row->result_bits, &result, sizeof(result));
            row->identity_class = identity_class;
            row->fallback = identity == 0;
            row->phase = phase;
            __atomic_store_n(&row->ready, 1, __ATOMIC_RELEASE);
        }
    }
    return result;
}

static float linux_build_rng_draw(const void *storage, float range) {
    return linux_build_rng_run((linux_build_rng_draw_fn)(void *)LX(LX_BUILD_FLOAT_RANGE_VA), storage, range,
                               (uintptr_t)__builtin_return_address(0));
}

/* mov rdi,rbx; jmp [rip+0]; address of linux_build_rng_draw */
static void linux_build_rng_write_stub(unsigned char *page) {
    static const unsigned char code[9] = {0x48,0x89,0xdf,0xff,0x25,0,0,0,0};
    void *target = (void *)linux_build_rng_draw;
    memcpy(page, code, sizeof(code));
    memcpy(page + sizeof(code), &target, sizeof(target));
}

static int linux_build_rng_stub_owned(void) {
    unsigned char expected[LINUX_BUILD_RNG_STUB_SIZE];
    linux_build_rng_write_stub(expected);
    return linux_build_rng_page && !memcmp(linux_build_rng_page, expected, sizeof(expected));
}

static int linux_build_rng_owned(void) {
    unsigned __int128 current;
    if (!linux_build_rng_installed) return 1;
    if (!gate_read_local(LX(LX_BUILD_CALL_BLOCK_VA), &current, sizeof(current))) return 0;
    return current == linux_build_rng_patched && linux_build_rng_stub_owned();
}

/* RIP-relative constants are data, so the two function hashes do not pin them. */
static int linux_build_rng_literals(void) {
    static const struct {
        uintptr_t address;
        unsigned size;
        unsigned char bytes[8];
    } literals[] = {
        {0x2e25ce0u, 8, {0x1e,0xa7,0xe8,0x48,0x2e,0xff,0xef,0xbf}}, /* -0.9999 sentinel */
        {0x2c76ab0u, 8, {0x00,0x00,0x00,0x00,0x00,0x00,0xf0,0xbf}}, /* -1.0 from callers */
        {0x2e2cce0u, 8, {0x00,0x00,0x00,0x00,0x00,0x00,0x4e,0x40}}, /* 60 seconds */
        {0x2e27150u, 4, {0x00,0x00,0x20,0x41}},                     /* range 10 */
        {0x2cf1c8cu, 4, {0x00,0x00,0x80,0x2f}},                     /* 2^-32 */
    };
    for (unsigned index = 0; index < sizeof(literals) / sizeof(literals[0]); ++index) {
        unsigned char actual[8];
        if (!gate_read_local(LX(literals[index].address), actual, literals[index].size) ||
            memcmp(actual, literals[index].bytes, literals[index].size)) return 0;
    }
    return 1;
}

static int linux_build_rng_pins(void) {
    unsigned char scheduler[LINUX_BUILD_RNG_SCHEDULER_SIZE];
    if (!gate_read_local(LX(LX_BUILD_SCHEDULER_VA), scheduler, sizeof(scheduler))) return 0;
    size_t block_offset = LX_BUILD_CALL_BLOCK_VA - LX_BUILD_SCHEDULER_VA;
    if (linux_build_rng_installed) {
        if (!linux_build_rng_owned()) return 0;
        memcpy(scheduler + block_offset, linux_build_rng_original_block, sizeof(linux_build_rng_original_block));
    } else if (memcmp(scheduler + block_offset, linux_build_rng_original_block, sizeof(linux_build_rng_original_block))) return 0;
    return linux_build_rng_literals() && hash_ok(scheduler, sizeof(scheduler), linux_build_rng_scheduler_hash) &&
           hash_ok(LX(LX_BUILD_FLOAT_RANGE_VA), 0x7e, linux_build_rng_draw_hash);
}

static int linux_build_rng_arm(void) {
    if (linux_build_rng_installed)
        return stats.active && linux_build_rng_owned() && linux_build_rng_pins() && !linux_build_rng_failures;
    if (!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId() != ui_thread || !world_identity_installed) {
        linux_build_rng_stage = "needs";
        return 0;
    }
    if (!linux_build_rng_pins()) { linux_build_rng_stage = "pins"; return 0; }
    if (!linux_build_rng_page) linux_build_rng_page = gate_near_page(LX(LX_BUILD_CALL_VA));
    if (!linux_build_rng_page) { linux_build_rng_stage = "page"; return 0; }
    intptr_t displacement = (intptr_t)linux_build_rng_page - (intptr_t)(LX(LX_BUILD_CALL_VA) + 5);
    if (displacement < INT32_MIN || displacement > INT32_MAX) { linux_build_rng_stage = "range"; return 0; }
    if (mprotect(linux_build_rng_page, GATE_PAGE, PROT_READ | PROT_WRITE)) { linux_build_rng_stage = "page"; return 0; }
    memset(linux_build_rng_page, 0xcc, GATE_PAGE);
    linux_build_rng_write_stub(linux_build_rng_page);
    if (mprotect(linux_build_rng_page, GATE_PAGE, PROT_READ | PROT_EXEC)) { linux_build_rng_stage = "page"; return 0; }
    memcpy(&linux_build_rng_original, linux_build_rng_original_block, sizeof(linux_build_rng_original));
    linux_build_rng_patched = linux_build_rng_original;
    int32_t relative = (int32_t)displacement;
    memcpy((unsigned char *)&linux_build_rng_patched + 7, &relative, sizeof(relative));
    linux_build_rng_keyed = linux_build_rng_loading = linux_build_rng_native = 0;
    linux_build_rng_unmapped = linux_build_rng_fallback = 0;
    linux_build_rng_load_keyed = linux_build_rng_load_fallback = 0;
    linux_build_rng_container = linux_build_rng_storage = linux_build_rng_other = 0;
    memset(linux_build_rng_rows, 0, sizeof(linux_build_rng_rows));
    linux_build_rng_row_count = 0;
    linux_build_rng_failures = 0;
    linux_build_rng_restored = 0;
    if (!gate_code_swap128(LX(LX_BUILD_CALL_BLOCK_VA), linux_build_rng_original, linux_build_rng_patched)) {
        linux_build_rng_stage = "cas";
        return 0;
    }
    linux_build_rng_installed = 1;
    linux_build_rng_stage = "armed";
    return 1;
}

static int linux_build_rng_check(void) {
    if (__atomic_load_n(&linux_build_rng_failures, __ATOMIC_RELAXED)) { linux_build_rng_stage = "failure"; return 0; }
    if (!linux_build_rng_owned() || !linux_build_rng_pins()) { linux_build_rng_stage = "ownership"; return 0; }
    return 1;
}

static void linux_build_rng_restore(void) {
    if (!linux_build_rng_installed) return;
    if (linux_build_rng_owned() && gate_code_swap128(LX(LX_BUILD_CALL_BLOCK_VA), linux_build_rng_patched, linux_build_rng_original))
        linux_build_rng_installed = 0;
    linux_build_rng_restored = !linux_build_rng_installed;
}

static int lua_build_rng(lua_State *L) { p_lua_pushnumber(L, linux_build_rng_arm()); return 1; }
static int lua_build_rng_status(lua_State *L) {
    char out[512];
    int length = accept_ui() ? snprintf(out, sizeof(out),
        "installed=%d frame=%llu keyed=%llu loading=%llu native=%llu unmapped=%llu fallback=%llu load_keyed=%llu load_fallback=%llu container=%llu storage=%llu other=%llu rows=%llu overflow=%llu restored=%d stage=%s failures=%lld",
        linux_build_rng_installed, (unsigned long long)stats.issued, (unsigned long long)__atomic_load_n(&linux_build_rng_keyed, __ATOMIC_RELAXED),
        (unsigned long long)__atomic_load_n(&linux_build_rng_loading, __ATOMIC_RELAXED), (unsigned long long)__atomic_load_n(&linux_build_rng_native, __ATOMIC_RELAXED),
        (unsigned long long)__atomic_load_n(&linux_build_rng_unmapped, __ATOMIC_RELAXED), (unsigned long long)__atomic_load_n(&linux_build_rng_fallback, __ATOMIC_RELAXED),
        (unsigned long long)__atomic_load_n(&linux_build_rng_load_keyed, __ATOMIC_RELAXED), (unsigned long long)__atomic_load_n(&linux_build_rng_load_fallback, __ATOMIC_RELAXED),
        (unsigned long long)__atomic_load_n(&linux_build_rng_container, __ATOMIC_RELAXED), (unsigned long long)__atomic_load_n(&linux_build_rng_storage, __ATOMIC_RELAXED), (unsigned long long)__atomic_load_n(&linux_build_rng_other, __ATOMIC_RELAXED),
        (unsigned long long)__atomic_load_n(&linux_build_rng_row_count, __ATOMIC_RELAXED),
        (unsigned long long)(__atomic_load_n(&linux_build_rng_row_count, __ATOMIC_RELAXED) > LINUX_BUILD_RNG_ROW_CAPACITY ?
            __atomic_load_n(&linux_build_rng_row_count, __ATOMIC_RELAXED) - LINUX_BUILD_RNG_ROW_CAPACITY : 0),
        linux_build_rng_restored, linux_build_rng_stage, (long long)__atomic_load_n(&linux_build_rng_failures, __ATOMIC_RELAXED)) : 0;
    if (length > 0 && (size_t)length < sizeof(out)) p_lua_pushlstring(L, out, (size_t)length); else p_lua_pushnil(L);
    return 1;
}

/* First 128 keyed/fallback non-admitted draws; phase names the boot state.
 * Caller is the fixed scheduler continuation,
 * not its higher loader caller. Overflow remains visible in status. */
static int lua_build_rng_row(lua_State *L) {
    double requested = p_lua_tonumber(L, 1);
    if (!accept_ui() || !isfinite(requested) || requested < 0 || requested >= LINUX_BUILD_RNG_ROW_CAPACITY || floor(requested) != requested) {
        p_lua_pushnil(L);
        return 1;
    }
    unsigned index = (unsigned)requested;
    const linux_build_rng_row *row = &linux_build_rng_rows[index];
    if (!__atomic_load_n(&row->ready, __ATOMIC_ACQUIRE)) { p_lua_pushnil(L); return 1; }
    char out[384];
    int length = snprintf(out, sizeof(out),
        "index=%u phase=%u class=%08x identity=%016llx identity_class=%u fallback=%u time=%016llx seed=%016llx range=%08x draw=%08x caller=%08x",
        index, row->phase, row->class_rva, (unsigned long long)row->identity, row->identity_class, row->fallback,
        (unsigned long long)row->time_bits, (unsigned long long)row->seed, row->range_bits, row->result_bits, row->caller_rva);
    if (length > 0 && (size_t)length < sizeof(out)) p_lua_pushlstring(L, out, (size_t)length); else p_lua_pushnil(L);
    return 1;
}
