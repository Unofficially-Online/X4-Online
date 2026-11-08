/* Default-off diagnostic for the pinned Linux ordered pathing experiment.
 *
 * A native hierarchy call always runs exactly once and its result is returned.
 * For eligible calls in an ordered batch, the time-based live transform runs on
 * a copy of the input. It invokes engine virtual methods and can have side
 * effects: this is a diagnostic experiment, not a passive or production-safe
 * observer. It must not be combined with FR20 step-chain instrumentation.
 * Only differing eligible comparisons produce rows. Zero comparisons prove
 * nothing about excluded paths. Exhausting the row capacity fails the run.
 */
#define PATH_TRANSFORM_OBSERVER_SITES 5u
#define PATH_TRANSFORM_OBSERVER_ROWS 512u
#define PATH_TRANSFORM_OBSERVER_DEPTH 32u
#define PATH_TRANSFORM_OBSERVER_HIER 0x18a2900u
#define PATH_TRANSFORM_OBSERVER_LIVE 0x18a0090u
#define PATH_TRANSFORM_OBSERVER_MANAGER_TIME 0x390u

static const uint32_t path_transform_observer_calls[PATH_TRANSFORM_OBSERVER_SITES] = {
    0x141f77bu, 0x141f790u, 0x142ec38u, 0x1432611u, 0x1e59502u
};
static const struct { uint32_t address, length; const char *sha; } path_transform_observer_pins[] = {
    {0x18a2900u, 0x720u, "84b67e3bbbadf0c71eb55163cde35a69e95c6a7102d1d8372df5a3b3c94f93d6"},
    {0x18a0090u, 0x3e0u, "8489683612cc942bd00c117405c4b2ba6e02307198a653f05b574d2e62b08ccd"},
    {0x141f420u, 0x3c0u, "405fe793959799d69c6648edf6e57b9c6e87466db248a75be7d59b061a34eb80"},
    {0x142e210u, 0xc00u, "5b75db0f62b3262a5563f36b076315c0613ac0cb7c89fc01658d8127b71c66a5"},
    {0x1431270u, 0x16a0u, "7f4b8b6b7c9b5bff01e59be60bf79dd50ffde32f1d5b32c9f0c02e296ae63b1a"},
    {0x1e592d0u, 0x4c0u, "5614a9028a4fe7cbce27387e14b5266bf9164fb097705991892f7a1691074ad0"},
    {0x12021e0u, 0x1fu, "2ef64c84b2b3fc6c1f518dae50e6506e023c192bfdb605616e811d45f3bbf513"},
    {0x13e3700u, 0x12u, "881dce51d19dddd06dfde4028c1a291c401b670c53267a3c6dca50ad375b40cb"},
    {0x13e338cu, 0x16u, "93f1b9bb7ee5d9b55efe0d9b1f71b7ca93964e20b2bd628ad7814265bd8b9cff"}
};
typedef void *(*path_transform_observer_hierarchy_fn)(void *, float *, void *, uint64_t);
typedef void (*path_transform_observer_live_fn)(void *, float *, double, void *, unsigned);
#ifdef GATE_TLS_TEST
static path_transform_observer_hierarchy_fn path_transform_observer_test_native;
static path_transform_observer_live_fn path_transform_observer_test_live;
#endif
typedef struct {
    uint64_t tick, from_id, to_id, time_bits;
    unsigned site, from_kind, to_kind;
    float native[16], live[16];
} path_transform_observer_row;
static path_transform_observer_row path_transform_observer_rows[PATH_TRANSFORM_OBSERVER_ROWS];
static unsigned char *path_transform_observer_page;
static unsigned path_transform_observer_done, path_transform_observer_restored;
static unsigned __int128 path_transform_observer_original[PATH_TRANSFORM_OBSERVER_SITES], path_transform_observer_patched[PATH_TRANSFORM_OBSERVER_SITES];
static uint64_t path_transform_observer_from_tick, path_transform_observer_to_tick, path_transform_observer_seen, path_transform_observer_compared, path_transform_observer_dropped, path_transform_observer_row_count;
static volatile unsigned long path_transform_observer_scope_thread;
static volatile long path_transform_observer_failures;
static const char *path_transform_observer_stage = "idle";

/* There is at most one inline job at a time. The owner exists only during the
 * synchronous native callback; no other thread gets a marker from the window. */
static int path_transform_observer_scope_enter(void) {
    if (!path_transform_observer_installed) return 0;
    unsigned long expected = 0;
    unsigned long thread = (unsigned long)GetCurrentThreadId();
    if (!thread || !__atomic_compare_exchange_n(&path_transform_observer_scope_thread, &expected, thread,
                                                0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
        __atomic_add_fetch(&path_transform_observer_failures, 1, __ATOMIC_SEQ_CST);
        return 0;
    }
    return 1;
}
static void path_transform_observer_scope_leave(int entered) {
    if (!entered) return;
    unsigned long expected = (unsigned long)GetCurrentThreadId();
    if (!__atomic_compare_exchange_n(&path_transform_observer_scope_thread, &expected, 0,
                                     0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
        __atomic_add_fetch(&path_transform_observer_failures, 1, __ATOMIC_SEQ_CST);
}
static int path_transform_observer_scoped(void) {
    unsigned char *tls = gate_tls_self();
    uint32_t view = 1;
    return tls && gate_read_local(tls + GATE_TLS_LINUX_VIEW, &view, sizeof(view)) && view == 0 &&
        path_transform_observer_installed && stats.active && boot_phase == 3 &&
        !__atomic_load_n(&path_transform_observer_failures, __ATOMIC_SEQ_CST) &&
        __atomic_load_n(&path_transform_observer_scope_thread, __ATOMIC_SEQ_CST) == (unsigned long)GetCurrentThreadId() &&
        InterlockedCompareExchange(&path_pass_open, 0, 0) &&
        stats.issued >= path_transform_observer_from_tick && stats.issued <= path_transform_observer_to_tick;
}
/* Validate every link through the live parent chain. A component classified as
 * an object must have a known non-nav controller; nav getters may update banks.
 * Unknown components/controllers are skipped, never guessed. */
static int path_transform_observer_chain(const unsigned char *from, const unsigned char *to) {
    int found_object = 0;
    for (unsigned depth = 0; from && depth < PATH_TRANSFORM_OBSERVER_DEPTH; ++depth) {
        if (from == to) return found_object;
        uintptr_t vtable = 0, controller_vtable = 0;
        unsigned char *parent = NULL, *controller = NULL;
        if (!gate_read_local(from, &vtable, 8)) return 0;
        const lxw_class *component = lxw_class_of(vtable);
        if (!component || component->family != LXW_FAMILY_NONE || (component->flags & 0x80000000u)) return 0;
        if (component->flags & 4u) {
            if (!gate_read_local(from + LXW_OBJ_CONTROLLER, &controller, 8) || !controller ||
                !gate_read_local(controller, &controller_vtable, 8)) return 0;
            const lxw_class *movement = lxw_class_of(controller_vtable);
            if (!movement || movement->family == LXW_FAMILY_NONE || movement->family == LXW_FAMILY_NAV) return 0;
            found_object = 1;
        }
        if (!gate_read_local(from + LXW_OBJ_PARENT, &parent, 8)) return 0;
        from = parent;
    }
    return 0;
}
static uint64_t path_transform_observer_key(const unsigned char *object, unsigned *kind) {
    uint64_t runtime = 0;
    *kind = WORLD_KEY_NONE;
    if (!object || !gate_read_local(object + 8, &runtime, 8)) return 0;
    return world_identity_key(runtime, kind);
}
static void *path_transform_observer_call(unsigned site, void *from, float *matrix, void *to, uint64_t flag) {
    path_transform_observer_hierarchy_fn native = (path_transform_observer_hierarchy_fn)(void *)LX(PATH_TRANSFORM_OBSERVER_HIER);
    path_transform_observer_live_fn live = (path_transform_observer_live_fn)(void *)LX(PATH_TRANSFORM_OBSERVER_LIVE);
#ifdef GATE_TLS_TEST
    if (path_transform_observer_test_native) native = path_transform_observer_test_native;
    if (path_transform_observer_test_live) live = path_transform_observer_test_live;
#endif
    if (!path_transform_observer_scoped() || !from || !to || from == to || !matrix || (flag & 0xffu))
        return native(from, matrix, to, flag);
    __atomic_add_fetch(&path_transform_observer_seen, 1, __ATOMIC_RELAXED);
    if (!path_transform_observer_chain(from, to)) return native(from, matrix, to, flag);
    unsigned from_kind, to_kind;
    uint64_t from_id = path_transform_observer_key(from, &from_kind);
    uint64_t to_id = path_transform_observer_key(to, &to_kind);
    if (!from_id || !to_id) return native(from, matrix, to, flag);
    unsigned char *manager = NULL, *tls = gate_tls_self();
    double time = 0;
    float candidate[16] __attribute__((aligned(16)));
    if (!tls || !gate_read_local(LX(LX_PATH_MANAGER_VA), &manager, 8) || !manager ||
        !gate_read_local(manager + PATH_TRANSFORM_OBSERVER_MANAGER_TIME, &time, 8) || !isfinite(time) ||
        !gate_read_local(matrix, candidate, sizeof(candidate))) {
        __atomic_add_fetch(&path_transform_observer_failures, 1, __ATOMIC_SEQ_CST);
        return native(from, matrix, to, flag);
    }
    /* Capture the native result before asking for the alternative. The candidate
     * cannot change the caller's output or the four gate-managed TLS words. */
    void *result = native(from, matrix, to, flag);
    uint64_t old_time, old_previous, old_rng;
    uint32_t old_view;
    memcpy(&old_time, tls + GATE_TLS_LINUX_TIME, 8);
    memcpy(&old_previous, tls + GATE_TLS_LINUX_PREVIOUS_TIME, 8);
    memcpy(&old_rng, tls + GATE_TLS_LINUX_RNG, 8);
    memcpy(&old_view, tls + GATE_TLS_LINUX_VIEW, 4);
    live(from, candidate, time, to, 0);
    memcpy(tls + GATE_TLS_LINUX_TIME, &old_time, 8);
    memcpy(tls + GATE_TLS_LINUX_PREVIOUS_TIME, &old_previous, 8);
    memcpy(tls + GATE_TLS_LINUX_RNG, &old_rng, 8);
    memcpy(tls + GATE_TLS_LINUX_VIEW, &old_view, 4);
    ++path_transform_observer_compared;
    if (memcmp(candidate, matrix, sizeof(candidate))) {
        if (path_transform_observer_row_count >= PATH_TRANSFORM_OBSERVER_ROWS) {
            ++path_transform_observer_dropped;
            __atomic_add_fetch(&path_transform_observer_failures, 1, __ATOMIC_SEQ_CST);
            return result;
        }
        path_transform_observer_row *row = &path_transform_observer_rows[path_transform_observer_row_count];
        row->tick = stats.issued;
        row->site = site;
        row->from_id = from_id;
        row->to_id = to_id;
        row->from_kind = from_kind;
        row->to_kind = to_kind;
        memcpy(&row->time_bits, &time, 8);
        memcpy(row->native, matrix, sizeof(row->native));
        memcpy(row->live, candidate, sizeof(row->live));
        ++path_transform_observer_row_count;
    }
    return result;
}
#define PATH_TRANSFORM_OBSERVER_HOOK(n) \
    static void *path_transform_observer_hook##n(void *from, float *matrix, void *to, uint64_t flag) { \
        return path_transform_observer_call(n, from, matrix, to, flag); \
    }
PATH_TRANSFORM_OBSERVER_HOOK(0)
PATH_TRANSFORM_OBSERVER_HOOK(1)
PATH_TRANSFORM_OBSERVER_HOOK(2)
PATH_TRANSFORM_OBSERVER_HOOK(3)
PATH_TRANSFORM_OBSERVER_HOOK(4)
#undef PATH_TRANSFORM_OBSERVER_HOOK
static void *path_transform_observer_hooks[PATH_TRANSFORM_OBSERVER_SITES] = {
    (void *)path_transform_observer_hook0, (void *)path_transform_observer_hook1, (void *)path_transform_observer_hook2,
    (void *)path_transform_observer_hook3, (void *)path_transform_observer_hook4
};
static void path_transform_observer_stub(unsigned site, unsigned char output[16]) {
    memset(output, 0xcc, 16);
    output[0] = 0xff;
    output[1] = 0x25;
    memset(output + 2, 0, 4);
    memcpy(output + 6, &path_transform_observer_hooks[site], 8);
}
static int path_transform_observer_word(unsigned site, const void *target, unsigned __int128 *word) {
    unsigned char *call = LX(path_transform_observer_calls[site]);
    unsigned char *block = (unsigned char *)((uintptr_t)call & ~(uintptr_t)15);
    unsigned offset = (unsigned)(call - block);
    intptr_t distance = (intptr_t)target - (intptr_t)(call + 5);
    if (offset + 5 > 16 || distance < INT32_MIN || distance > INT32_MAX) return 0;
    memcpy(word, block, 16);
    if (((unsigned char *)word)[offset] != 0xe8) return 0;
    int32_t displacement = (int32_t)distance;
    memcpy((unsigned char *)word + offset + 1, &displacement, 4);
    return 1;
}
static int path_transform_observer_swap(unsigned site, unsigned __int128 before, unsigned __int128 after) {
    unsigned char *block = (unsigned char *)((uintptr_t)LX(path_transform_observer_calls[site]) & ~(uintptr_t)15);
    return gate_code_swap128(block, before, after);
}
static int path_transform_observer_site_is(unsigned site, const void *target) {
    unsigned char *call = LX(path_transform_observer_calls[site]);
    int32_t displacement;
    if (call[0] != 0xe8) return 0;
    memcpy(&displacement, call + 1, 4);
    return call + 5 + displacement == target;
}
static int path_transform_observer_owned(void) {
    if (!path_transform_observer_installed) return 1;
    if (!path_transform_observer_page || path_transform_observer_done != (1u << PATH_TRANSFORM_OBSERVER_SITES) - 1u) return 0;
    for (unsigned i = 0; i < PATH_TRANSFORM_OBSERVER_SITES; ++i) {
        unsigned char expected[16];
        path_transform_observer_stub(i, expected);
        unsigned __int128 actual;
        memcpy(&actual, (void *)((uintptr_t)LX(path_transform_observer_calls[i]) & ~(uintptr_t)15), 16);
        if (actual != path_transform_observer_patched[i] || !path_transform_observer_site_is(i, path_transform_observer_page + 16 * i) ||
            memcmp(path_transform_observer_page + 16 * i, expected, sizeof(expected))) return 0;
    }
    return 1;
}
static void path_transform_observer_restore(void) {
    if (!path_transform_observer_installed) return;
    if (__atomic_load_n(&path_transform_observer_scope_thread, __ATOMIC_SEQ_CST)) {
        path_transform_observer_stage = "scope_open";
        return;
    }
    for (unsigned i = 0; i < PATH_TRANSFORM_OBSERVER_SITES; ++i) {
        unsigned char expected[16];
        path_transform_observer_stub(i, expected);
        if ((path_transform_observer_done & (1u << i)) && path_transform_observer_site_is(i, path_transform_observer_page + 16 * i) &&
            !memcmp(path_transform_observer_page + 16 * i, expected, sizeof(expected)) &&
            path_transform_observer_swap(i, path_transform_observer_patched[i], path_transform_observer_original[i])) path_transform_observer_done &= ~(1u << i);
    }
    if (path_transform_observer_done) { path_transform_observer_stage = "restore_foreign"; return; }
    path_transform_observer_installed = 0;
    path_transform_observer_restored = 1;
    path_transform_observer_stage = "restored";
}
static int path_transform_observer_check(void) {
    if (__atomic_load_n(&path_transform_observer_failures, __ATOMIC_SEQ_CST) || !path_transform_observer_owned()) {
        path_transform_observer_stage = "ownership_or_scope";
        return 0;
    }
    return 1;
}
static int path_transform_observer_arm(uint64_t first_tick, uint64_t last_tick) {
    if (path_transform_observer_installed)
        return stats.active && path_transform_observer_owned() && !path_transform_observer_failures &&
            first_tick == path_transform_observer_from_tick && last_tick == path_transform_observer_to_tick;
    path_transform_observer_stage = "state";
    if (!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId() != ui_thread ||
        !travel_inline_installed || !lxt_inline_owned() || tsnap_installed || st_chain_wanted || st_chain_installed ||
        !world_identity_installed || first_tick == 0 || last_tick < first_tick || last_tick - first_tick >= 600) return 0;
    path_transform_observer_stage = "pins";
    for (unsigned i = 0; i < sizeof(path_transform_observer_pins) / sizeof(path_transform_observer_pins[0]); ++i)
        if (!hash_ok(LX(path_transform_observer_pins[i].address), path_transform_observer_pins[i].length, path_transform_observer_pins[i].sha)) return 0;
    for (unsigned i = 0; i < PATH_TRANSFORM_OBSERVER_SITES; ++i)
        if (!path_transform_observer_site_is(i, LX(PATH_TRANSFORM_OBSERVER_HIER))) return 0;
    path_transform_observer_stage = "page";
    if (!path_transform_observer_page) path_transform_observer_page = gate_near_page(LX(path_transform_observer_calls[0]));
    if (!path_transform_observer_page || mprotect(path_transform_observer_page, GATE_PAGE, PROT_READ | PROT_WRITE)) return 0;
    memset(path_transform_observer_page, 0xcc, GATE_PAGE);
    for (unsigned i = 0; i < PATH_TRANSFORM_OBSERVER_SITES; ++i) path_transform_observer_stub(i, path_transform_observer_page + 16 * i);
    if (mprotect(path_transform_observer_page, GATE_PAGE, PROT_READ | PROT_EXEC)) return 0;
    for (unsigned i = 0; i < PATH_TRANSFORM_OBSERVER_SITES; ++i) {
        unsigned char *block = (unsigned char *)((uintptr_t)LX(path_transform_observer_calls[i]) & ~(uintptr_t)15);
        memcpy(&path_transform_observer_original[i], block, 16);
        if (!path_transform_observer_word(i, path_transform_observer_page + 16 * i, &path_transform_observer_patched[i])) return 0;
    }
    path_transform_observer_from_tick = first_tick;
    path_transform_observer_to_tick = last_tick;
    path_transform_observer_seen = path_transform_observer_compared = path_transform_observer_dropped = path_transform_observer_row_count = 0;
    path_transform_observer_failures = 0;
    path_transform_observer_done = path_transform_observer_restored = 0;
    path_transform_observer_installed = 1;
    path_transform_observer_stage = "swap";
    for (unsigned i = 0; i < PATH_TRANSFORM_OBSERVER_SITES; ++i) {
        if (!path_transform_observer_swap(i, path_transform_observer_original[i], path_transform_observer_patched[i])) {
            path_transform_observer_restore();
            return 0;
        }
        path_transform_observer_done |= 1u << i;
    }
    path_transform_observer_stage = "armed";
    return 1;
}
static int lua_path_transform_observe(lua_State *L) {
    double first = p_lua_tonumber(L, 1), last = p_lua_tonumber(L, 2);
    int valid = isfinite(first) && isfinite(last) && first >= 1 && last >= first &&
        last < 9007199254740992.0 && floor(first) == first && floor(last) == last;
    p_lua_pushnumber(L, valid && path_transform_observer_arm((uint64_t)first, (uint64_t)last));
    return 1;
}
static int lua_path_transform_observe_status(lua_State *L) {
    char text[256];
    int length = accept_ui() ? snprintf(text, sizeof(text),
        "installed=%d restored=%u stage=%s seen=%llu compared=%llu rows=%llu dropped=%llu failures=%ld",
        path_transform_observer_installed, path_transform_observer_restored, path_transform_observer_stage, (unsigned long long)path_transform_observer_seen,
        (unsigned long long)path_transform_observer_compared, (unsigned long long)path_transform_observer_row_count,
        (unsigned long long)path_transform_observer_dropped, path_transform_observer_failures) : 0;
    if (length > 0 && (size_t)length < sizeof(text)) p_lua_pushlstring(L, text, (size_t)length);
    else p_lua_pushnil(L);
    return 1;
}
static int lua_path_transform_observe_row(lua_State *L) {
    double index = p_lua_tonumber(L, 1);
    if (!accept_ui() || !isfinite(index) || index < 0 || index >= path_transform_observer_row_count || floor(index) != index) {
        p_lua_pushnil(L);
        return 1;
    }
    path_transform_observer_row *row = &path_transform_observer_rows[(unsigned)index];
    char native_a[65], native_b[65], live_a[65], live_b[65], text[512];
    hex32((const unsigned char *)row->native, native_a);
    hex32((const unsigned char *)row->native + 32, native_b);
    hex32((const unsigned char *)row->live, live_a);
    hex32((const unsigned char *)row->live + 32, live_b);
    int length = snprintf(text, sizeof(text),
        "tick=%llu site=%u from=%016llx/%u to=%016llx/%u time=%016llx native=%s%s live=%s%s",
        (unsigned long long)row->tick, row->site, (unsigned long long)row->from_id, row->from_kind,
        (unsigned long long)row->to_id, row->to_kind, (unsigned long long)row->time_bits,
        native_a, native_b, live_a, live_b);
    if (length > 0 && (size_t)length < sizeof(text)) p_lua_pushlstring(L, text, (size_t)length);
    else p_lua_pushnil(L);
    return 1;
}
