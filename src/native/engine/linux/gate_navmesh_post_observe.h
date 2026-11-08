/* Opt-in NavHelperGeneratedEvent producer call-site observation. Records a
 * post attempt, not queue admission. Runtime handles and pointers are local.
 * The immutable page, native target and buffer live for the process lifetime:
 * a thread may already have branched to the thunk when the site is restored. */
#define NAVMESH_POST_SITE 0x10a6f30u
#define NAVMESH_POST_NATIVE 0x16c36c0u
#define NAVMESH_POST_EVENT_VTABLE 0x3b28cb8u
#define NAVMESH_POST_CAPACITY 8192u
#define NAVMESH_POST_CLOSED (UINT64_C(1) << 63)

typedef void (*navmesh_post_fn)(void *, void *, double);
typedef struct {
    uint64_t epoch, runtime, source, source_type, event, event_type, due, thread;
    unsigned phase, readable;
    int ready;
} navmesh_post_row;
static navmesh_post_row navmesh_post_rows[NAVMESH_POST_CAPACITY];
static uint64_t navmesh_post_guard = NAVMESH_POST_CLOSED;
static uint64_t navmesh_post_epoch, navmesh_post_attempts, navmesh_post_count, navmesh_post_published, navmesh_post_overflow, navmesh_post_unreadable;
static uint64_t navmesh_post_first, navmesh_post_last;
static unsigned char *navmesh_post_page;
static unsigned __int128 navmesh_post_original, navmesh_post_patched;
static navmesh_post_fn navmesh_post_native;
static int navmesh_post_ever, navmesh_post_restored;
static const char *navmesh_post_stage = "idle";
static const unsigned char navmesh_post_block[16] = {
    0xe8,0x8b,0xc7,0x61,0x00,0x49,0x8d,0x7f,0x38,0xe8,0x02,0xd6,0x96,0xff,0xe9,0xb1
};

/* A single publication gives workers a coherent phase and last published gate
 * epoch. It is a sampling boundary, not a claim about exact engine timing. */
static void navmesh_post_publish(unsigned phase, uint64_t epoch) {
    __atomic_store_n(&navmesh_post_epoch, (epoch << 2) | (phase & 3), __ATOMIC_RELEASE);
}

/* Reader admission is lock-free and checks closure in the same CAS as the
 * increment. Late entrants cannot access records after closure. */
static int navmesh_post_enter(void) {
    uint64_t state = __atomic_load_n(&navmesh_post_guard, __ATOMIC_ACQUIRE);
    for (;;) {
        if ((state & NAVMESH_POST_CLOSED) || state == NAVMESH_POST_CLOSED - 1) return 0;
        if (__atomic_compare_exchange_n(&navmesh_post_guard, &state, state + 1, 1,
                                        __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) return 1;
    }
}
static void navmesh_post_leave(void) {
    __atomic_sub_fetch(&navmesh_post_guard, 1, __ATOMIC_RELEASE);
}

static void navmesh_post_capture(void *source, void *event, double due) {
    if (!navmesh_post_enter()) return;
    __atomic_add_fetch(&navmesh_post_attempts, 1, __ATOMIC_RELAXED);
    uint64_t stamp = __atomic_load_n(&navmesh_post_epoch, __ATOMIC_ACQUIRE);
    unsigned phase = (unsigned)(stamp & 3);
    uint64_t epoch = stamp >> 2;
    if (phase == 3 && epoch > 0 && (epoch < navmesh_post_first || epoch > navmesh_post_last)) {
        navmesh_post_leave();
        return;
    }
    uint64_t index = __atomic_fetch_add(&navmesh_post_count, 1, __ATOMIC_RELAXED);
    if (index >= NAVMESH_POST_CAPACITY) {
        __atomic_add_fetch(&navmesh_post_overflow, 1, __ATOMIC_RELAXED);
        navmesh_post_leave();
        return;
    }
    navmesh_post_row *row = &navmesh_post_rows[index];
    row->epoch = epoch;
    row->phase = phase;
    row->source = (uintptr_t)source;
    row->event = (uintptr_t)event;
    row->thread = (uintptr_t)pthread_self();
    memcpy(&row->due, &due, sizeof(row->due));
    row->runtime = row->source_type = row->event_type = 0;
    /* Worker threads must not touch gate_read_local's non-atomic diagnostic
     * counter or depend on the shared fault-handler lifecycle. */
    row->readable = source && event &&
        gate_read_slow(source, &row->source_type, sizeof(row->source_type)) &&
        gate_read_slow((const unsigned char *)source + 8, &row->runtime, sizeof(row->runtime)) &&
        gate_read_slow(event, &row->event_type, sizeof(row->event_type)) &&
        row->event_type == (uintptr_t)LX(NAVMESH_POST_EVENT_VTABLE);
    if (!row->readable) __atomic_add_fetch(&navmesh_post_unreadable, 1, __ATOMIC_RELAXED);
    __atomic_store_n(&row->ready, 1, __ATOMIC_RELEASE);
    __atomic_add_fetch(&navmesh_post_published, 1, __ATOMIC_RELEASE);
    navmesh_post_leave();
}

/* Called instead of the original CALL. errno belongs to the engine: restore it
 * before forwarding and never touch it after the native routine returns. */
__attribute__((noinline)) static void navmesh_post_observer(void *source, void *event, double due) {
    int prior_errno = errno;
    navmesh_post_capture(source, event, due);
    errno = prior_errno;
    navmesh_post_native(source, event, due);
}

static void navmesh_post_write_stub(unsigned char *page) {
    static const unsigned char jump[6] = {0xff,0x25,0,0,0,0};
    uintptr_t target = (uintptr_t)navmesh_post_observer;
    memcpy(page, jump, sizeof(jump));
    memcpy(page + sizeof(jump), &target, sizeof(target));
}
static int navmesh_post_owned(void) {
    if (!navmesh_post_installed) return 1;
    unsigned char expected[14], current[14];
    unsigned __int128 block;
    navmesh_post_write_stub(expected);
    return navmesh_post_page && gate_read_local(navmesh_post_page,current,sizeof(current)) &&
        !memcmp(expected,current,sizeof(current)) &&
        gate_read_local(LX(NAVMESH_POST_SITE),&block,sizeof(block)) && block==navmesh_post_patched;
}
static int navmesh_post_pins(void) {
    unsigned char body[0x60];
    if (!gate_read_local(LX(0x10a6ef0u),body,sizeof(body))) return 0;
    if (navmesh_post_installed) {
        if (!navmesh_post_owned()) return 0;
        memcpy(body+0x40,navmesh_post_block,sizeof(navmesh_post_block));
    } else if (memcmp(body+0x40,navmesh_post_block,sizeof(navmesh_post_block))) return 0;
    return hash_ok(body,sizeof(body),"03092ef66d4971b31d8a19d2ad74a1e075e4ff9645efa648b580f13a5a4321de") &&
        hash_ok(LX(NAVMESH_POST_NATIVE),0x10c,"431c42fcaaeea6b0fb501e6b8de072ba301720f303d77871efcd192d8f7cee0a");
}

#ifdef NAVMESH_POST_TEST
static int navmesh_post_test_swap_failure;
#endif
static int navmesh_post_swap(unsigned __int128 expected, unsigned __int128 desired) {
#ifdef NAVMESH_POST_TEST
    int failure = navmesh_post_test_swap_failure;
    navmesh_post_test_swap_failure = 0;
    if (failure == 2) return 0;
#endif
    int result = gate_code_swap128(LX(NAVMESH_POST_SITE), expected, desired);
#ifdef NAVMESH_POST_TEST
    if (failure == 1) return 0;
#endif
    return result;
}

static int navmesh_post_arm(uint64_t first, uint64_t last) {
    if (navmesh_post_installed) {
        return stats.active && !navmesh_post_cleanup_uncertain &&
            !(__atomic_load_n(&navmesh_post_guard, __ATOMIC_ACQUIRE) & NAVMESH_POST_CLOSED) &&
            first == navmesh_post_first && last == navmesh_post_last && navmesh_post_pins();
    }
    if (navmesh_post_ever) {
        navmesh_post_stage = "one-shot";
        return 0;
    }
    if (!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId() != ui_thread ||
        !first || first > last || last > UINT64_C(9007199254740991)) {
        navmesh_post_stage = "needs";
        return 0;
    }
    if (!navmesh_post_pins() ||
        !hash_ok(LX(0x10a5e30u), 0x1d6b, "646a09194597e066fecfe3dbfdfc9ff2b601276f06900b96445ba73aca0335bd")) {
        navmesh_post_stage = "pins";
        return 0;
    }
    navmesh_post_page = gate_near_page(LX(NAVMESH_POST_SITE));
    if (!navmesh_post_page) {
        navmesh_post_stage = "page";
        return 0;
    }
    intptr_t delta = (intptr_t)navmesh_post_page - (intptr_t)LX(NAVMESH_POST_SITE + 5);
    if (delta < INT32_MIN || delta > INT32_MAX) {
        munmap(navmesh_post_page, GATE_PAGE);
        navmesh_post_page = NULL;
        navmesh_post_stage = "page";
        return 0;
    }
    memset(navmesh_post_page, 0xcc, GATE_PAGE);
    navmesh_post_write_stub(navmesh_post_page);
    if (mprotect(navmesh_post_page, GATE_PAGE, PROT_READ | PROT_EXEC)) {
        munmap(navmesh_post_page, GATE_PAGE);
        navmesh_post_page = NULL;
        navmesh_post_stage = "page";
        return 0;
    }
    navmesh_post_native = (navmesh_post_fn)(void *)LX(NAVMESH_POST_NATIVE);
    memcpy(&navmesh_post_original, navmesh_post_block, sizeof(navmesh_post_original));
    navmesh_post_patched = navmesh_post_original;
    int32_t relative = (int32_t)delta;
    memcpy((unsigned char *)&navmesh_post_patched + 1, &relative, sizeof(relative));
    navmesh_post_first = first;
    navmesh_post_last = last;
    navmesh_post_restored = 0;
    navmesh_post_publish(0, 0);
    __atomic_store_n(&navmesh_post_guard, 0, __ATOMIC_RELEASE);
    navmesh_post_ever = 1;
    if (!navmesh_post_swap(navmesh_post_original, navmesh_post_patched)) {
        unsigned __int128 current;
        navmesh_post_cleanup_uncertain = 1;
        navmesh_post_installed = !gate_read_local(LX(NAVMESH_POST_SITE), &current, sizeof(current)) ||
            current == navmesh_post_patched;
        __atomic_fetch_or(&navmesh_post_guard, NAVMESH_POST_CLOSED, __ATOMIC_ACQ_REL);
        navmesh_post_stage = "cas";
        return 0;
    }
    navmesh_post_installed = 1;
    navmesh_post_stage = "armed";
    return 1;
}

static int navmesh_post_check(void) {
    if (!navmesh_post_pins()) {
        navmesh_post_stage = "ownership";
        return 0;
    }
    return 1;
}

static void navmesh_post_restore(void) {
    __atomic_fetch_or(&navmesh_post_guard, NAVMESH_POST_CLOSED, __ATOMIC_ACQ_REL);
    ULONGLONG deadline = GetTickCount64() + 500;
    while ((__atomic_load_n(&navmesh_post_guard, __ATOMIC_ACQUIRE) & ~NAVMESH_POST_CLOSED) &&
           GetTickCount64() < deadline) {
        sched_yield();
    }
    if (__atomic_load_n(&navmesh_post_guard, __ATOMIC_ACQUIRE) & ~NAVMESH_POST_CLOSED)
        navmesh_post_cleanup_uncertain = 1;
    if (navmesh_post_installed) {
        if (navmesh_post_owned() && navmesh_post_swap(navmesh_post_patched, navmesh_post_original)) {
            navmesh_post_installed = 0;
        } else {
            navmesh_post_cleanup_uncertain = 1;
            navmesh_post_stage = "restore";
        }
    }
    navmesh_post_restored = !navmesh_post_installed && !navmesh_post_cleanup_uncertain;
}

static int lua_navmesh_post_observe(lua_State *L) {
    double first = p_lua_tonumber(L, 1);
    double last = p_lua_tonumber(L, 2);
    int ok = isfinite(first) && isfinite(last) && first >= 1 && last <= 9007199254740991.0 &&
        floor(first) == first && floor(last) == last && first <= last &&
        navmesh_post_arm((uint64_t)first, (uint64_t)last);
    p_lua_pushnumber(L, ok);
    return 1;
}

static int lua_navmesh_post_observe_status(lua_State *L) {
    uint64_t count = __atomic_load_n(&navmesh_post_count, __ATOMIC_ACQUIRE);
    uint64_t guard = __atomic_load_n(&navmesh_post_guard, __ATOMIC_ACQUIRE);
    char out[320];
    int length = accept_ui() ? snprintf(out, sizeof(out),
        "installed=%d attempts=%llu eligible=%llu recorded=%llu overflow=%llu unreadable=%llu "
        "inflight=%llu restored=%d cleanup_uncertain=%d first=%llu last=%llu stage=%s",
        navmesh_post_installed, (unsigned long long)__atomic_load_n(&navmesh_post_attempts, __ATOMIC_RELAXED),
        (unsigned long long)count, (unsigned long long)__atomic_load_n(&navmesh_post_published, __ATOMIC_ACQUIRE),
        (unsigned long long)__atomic_load_n(&navmesh_post_overflow, __ATOMIC_RELAXED),
        (unsigned long long)__atomic_load_n(&navmesh_post_unreadable, __ATOMIC_RELAXED),
        (unsigned long long)(guard & ~NAVMESH_POST_CLOSED), navmesh_post_restored, navmesh_post_cleanup_uncertain,
        (unsigned long long)navmesh_post_first, (unsigned long long)navmesh_post_last, navmesh_post_stage) : 0;
    if (length > 0 && (size_t)length < sizeof(out))
        p_lua_pushlstring(L, out, (size_t)length);
    else
        p_lua_pushnil(L);
    return 1;
}

static int lua_navmesh_post_observe_row(lua_State *L) {
    double value = p_lua_tonumber(L, 1);
    if (!accept_ui() || !isfinite(value) || value < 0 || value >= NAVMESH_POST_CAPACITY || floor(value) != value) {
        p_lua_pushnil(L);
        return 1;
    }
    unsigned index = (unsigned)value;
    const navmesh_post_row *row = &navmesh_post_rows[index];
    if (!__atomic_load_n(&row->ready, __ATOMIC_ACQUIRE)) {
        p_lua_pushnil(L);
        return 1;
    }
    char out[320];
    int length = snprintf(out, sizeof(out),
        "index=%u epoch=%llu phase=%u runtime=%016llx source=%016llx source_type=%016llx "
        "event=%016llx event_type=%016llx due=%016llx thread=%016llx readable=%u",
        index, (unsigned long long)row->epoch, row->phase, (unsigned long long)row->runtime,
        (unsigned long long)row->source, (unsigned long long)row->source_type, (unsigned long long)row->event,
        (unsigned long long)row->event_type, (unsigned long long)row->due, (unsigned long long)row->thread, row->readable);
    if (length > 0 && (size_t)length < sizeof(out))
        p_lua_pushlstring(L, out, (size_t)length);
    else
        p_lua_pushnil(L);
    return 1;
}
