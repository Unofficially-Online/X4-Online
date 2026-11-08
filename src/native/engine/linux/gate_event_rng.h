/* Opt-in Linux event-delivery RNG scope, using the event_order redirect.
 * Native Linux vtable RVAs are used as event types and local fallback keys;
 * they are stable only for this pinned ELF, not equal to Windows RVAs. The
 * fallback is shared by objects of one class and is not a unique identity.
 * The Linux normal-return path restores the caller's word, including nested
 * deliveries. It does not promise recovery if a native call unwinds or exits. */
#define LINUX_EVENT_RNG_DOMAIN 0x58344c45564e5452ULL
#define LINUX_EVENT_RNG_CAPACITY 16384u
#define LINUX_EVENT_RNG_STEP_LIMIT 4096u
#define LINUX_EVENT_RNG_STOP_REASON 151u

enum { LINUX_EVENT_RNG_OBSERVE = 1, LINUX_EVENT_RNG_KEY = 2 };
typedef void (*linux_event_delivery)(void *, void *, double);
typedef struct {
    uint64_t tick, key, due, entry;
    uint32_t source_type, event_type, draws;
    unsigned char identity_class, keyed;
} linux_event_rng_row;

static volatile long linux_event_rng_armed;
static int linux_event_rng_mode;
static const char *linux_event_rng_stage = "idle";
static linux_event_rng_row *linux_event_rng_rows;
static uint64_t linux_event_rng_count, linux_event_rng_from, linux_event_rng_to;
static uint64_t linux_event_rng_calls, linux_event_rng_keyed, linux_event_rng_local;
static uint64_t linux_event_rng_drawn, linux_event_rng_draws, linux_event_rng_unknown;
static uint64_t linux_event_rng_boot, linux_event_rng_unreadable, linux_event_rng_foreign;
static uint64_t linux_event_rng_keyset, linux_event_rng_drawset;
static unsigned linux_event_rng_depth, linux_event_rng_max_depth;
static volatile int64_t linux_event_rng_failures;
static int erng_pins(void);
static int erng_owned(void);

static uint32_t linux_event_rng_type(uintptr_t pointer) {
    uintptr_t base = (uintptr_t)image;
    /* The image range is checked by the gate's pinned ELF identity. */
    if (pointer < base || pointer - base >= 0x77b7000u) return UINT32_MAX;
    return (uint32_t)(pointer - base);
}

static uint32_t linux_event_rng_steps(uint64_t from, uint64_t to) {
    for (uint32_t count = 0; count <= LINUX_EVENT_RNG_STEP_LIMIT; ++count) {
        if (from == to) return count;
        from = from * 0x5851f42d4c957f2dULL + 0x14057b7ef767814fULL;
        from = (from >> 30) | (from << 34);
    }
    return UINT32_MAX;
}

static uint64_t linux_event_rng_seed(uint64_t tick, uint64_t key, unsigned identity_class,
                                     uint32_t event_type, uint64_t due) {
    /* Deliberately no ordinal: identical deliveries share a seed, as on Windows. */
    return lockstep_seed(LINUX_EVENT_RNG_DOMAIN, tick, key, event_type, due, identity_class);
}

static void linux_event_rng_deliver(linux_event_delivery native, void *source, void *event, double due) {
    if (!linux_event_rng_armed || !source) {
        native(source, event, due);
        return;
    }
    if (!stats.active || boot_phase != 3) {
        ++linux_event_rng_boot;
        native(source, event, due);
        return;
    }
    if (GetCurrentThreadId() != ui_thread) {
        __atomic_add_fetch(&linux_event_rng_foreign, 1, __ATOMIC_RELAXED);
        InterlockedIncrement64(&linux_event_rng_failures);
        native(source, event, due);
        return;
    }
    uint64_t *word = gate_tls_rng(gate_tls_self());
    if (!word) {
        linux_event_rng_stage = "word";
        InterlockedIncrement64(&linux_event_rng_failures);
        native(source, event, due);
        return;
    }
    uint64_t source_vtable, event_vtable, runtime;
    if (!event || !gate_read_local(source, &source_vtable, 8) ||
        !gate_read_local((unsigned char *)source + 8, &runtime, 8) ||
        !gate_read_local(event, &event_vtable, 8)) {
        ++linux_event_rng_unreadable;
        InterlockedIncrement64(&linux_event_rng_failures);
        native(source, event, due);
        return;
    }
    uint32_t source_type = linux_event_rng_type(source_vtable);
    uint32_t event_type = linux_event_rng_type(event_vtable);
    if (source_type == UINT32_MAX || event_type == UINT32_MAX) {
        ++linux_event_rng_unreadable;
        InterlockedIncrement64(&linux_event_rng_failures);
        native(source, event, due);
        return;
    }
    uint64_t due_bits;
    memcpy(&due_bits, &due, sizeof(due_bits));
    unsigned identity_class = WORLD_KEY_NONE;
    uint64_t key = runtime ? world_identity_key(runtime, &identity_class) : 0;
    if (!key) {
        identity_class = WORLD_KEY_NONE;
        key = source_type;
        ++linux_event_rng_local;
    }
    ++linux_event_rng_calls;
    int keyed = linux_event_rng_mode == LINUX_EVENT_RNG_KEY;
    uint64_t entry = *word;
    uint64_t start = keyed ? linux_event_rng_seed(stats.issued, key, identity_class, event_type, due_bits) : entry;
    if (keyed) {
        *word = start;
        ++linux_event_rng_keyed;
    }
    ++linux_event_rng_depth;
    if (linux_event_rng_depth > linux_event_rng_max_depth) linux_event_rng_max_depth = linux_event_rng_depth;
    native(source, event, due);
    uint64_t last = *word;
    --linux_event_rng_depth;
    if (keyed) *word = entry;
    uint32_t draws = linux_event_rng_steps(start, last);
    uint64_t identity = lockstep_mix(key ^ ((uint64_t)identity_class << 62)) ^
                        lockstep_mix(((uint64_t)event_type << 32) ^ due_bits);
    linux_event_rng_keyset += identity;
    linux_event_rng_drawset += lockstep_mix(identity ^ draws);
    if (draws == UINT32_MAX) ++linux_event_rng_unknown;
    else if (draws) { ++linux_event_rng_drawn; linux_event_rng_draws += draws; }
    if (draws || (linux_event_rng_to && stats.issued >= linux_event_rng_from && stats.issued <= linux_event_rng_to)) {
        linux_event_rng_row *row = &linux_event_rng_rows[linux_event_rng_count++ % LINUX_EVENT_RNG_CAPACITY];
        *row = (linux_event_rng_row){stats.issued, key, due_bits, entry, source_type, event_type, draws,
                                      (unsigned char)identity_class, (unsigned char)keyed};
    }
}

static int linux_event_rng_arm(int mode, uint64_t from, uint64_t to) {
    if (linux_event_rng_armed)
        return stats.active && linux_event_rng_mode == mode && linux_event_rng_from == from && linux_event_rng_to == to &&
               event_order_armed && event_order_installed && event_order_owned() && !event_order_failures && !linux_event_rng_failures;
    if ((mode != LINUX_EVENT_RNG_OBSERVE && mode != LINUX_EVENT_RNG_KEY) ||
        (to && (!from || to < from || to - from > 600)) || (!to && from)) return 0;
    if (!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId() != ui_thread ||
        !world_identity_installed || !event_order_armed || !event_order_installed || !event_order_owned() || event_order_failures) {
        linux_event_rng_stage = "needs";
        return 0;
    }
    /* The existing engine-refresh pin covers the actual Linux PCG instruction
     * sequence and its owning listener; it accepts that hook when owned. */
    if (!erng_pins() || !erng_owned()) { linux_event_rng_stage = "pins"; return 0; }
    if (!linux_event_rng_rows) linux_event_rng_rows = calloc(LINUX_EVENT_RNG_CAPACITY, sizeof(*linux_event_rng_rows));
    if (!linux_event_rng_rows) { linux_event_rng_stage = "allocation"; return 0; }
    linux_event_rng_mode = mode;
    linux_event_rng_from = from;
    linux_event_rng_to = to;
    linux_event_rng_count = linux_event_rng_calls = linux_event_rng_keyed = linux_event_rng_local = 0;
    linux_event_rng_drawn = linux_event_rng_draws = linux_event_rng_unknown = 0;
    linux_event_rng_boot = linux_event_rng_unreadable = linux_event_rng_foreign = 0;
    linux_event_rng_keyset = linux_event_rng_drawset = 0;
    linux_event_rng_depth = linux_event_rng_max_depth = 0;
    linux_event_rng_failures = 0;
    linux_event_rng_stage = "armed";
    InterlockedExchange(&linux_event_rng_armed, 1);
    return 1;
}

static void linux_event_rng_stop(void) { InterlockedExchange(&linux_event_rng_armed, 0); }
static int linux_event_rng_check(void) {
    if (!linux_event_rng_armed) return 1;
    if (!event_order_armed || !event_order_installed || !event_order_owned() || event_order_failures) {
        linux_event_rng_stage = "order";
        return 0;
    }
    if (!erng_pins() || !erng_owned()) { linux_event_rng_stage = "pins"; return 0; }
    if (linux_event_rng_failures) { linux_event_rng_stage = "failure"; return 0; }
    return 1;
}

static int linux_event_rng_row_text(uint64_t index, char *out, size_t capacity) {
    if (!linux_event_rng_rows || index >= linux_event_rng_count) return 0;
    int length;
    if (linux_event_rng_count > LINUX_EVENT_RNG_CAPACITY && index < linux_event_rng_count - LINUX_EVENT_RNG_CAPACITY)
        length = snprintf(out, capacity, "index=%llu evicted=1", (unsigned long long)index);
    else {
        const linux_event_rng_row *row = &linux_event_rng_rows[index % LINUX_EVENT_RNG_CAPACITY];
        const char *classes[] = {"saved", "created", "none"};
        char draws[16];
        if (row->draws == UINT32_MAX) strcpy(draws, "-1");
        else snprintf(draws, sizeof(draws), "%u", row->draws);
        length = snprintf(out, capacity, "index=%llu tick=%llu class=%s key=%016llx stype=%x etype=%x due=%016llx entry=%016llx draws=%s keyed=%u",
            (unsigned long long)index, (unsigned long long)row->tick, classes[row->identity_class < 3 ? row->identity_class : 2],
            (unsigned long long)row->key, row->source_type, row->event_type, (unsigned long long)row->due,
            (unsigned long long)row->entry, draws, (unsigned)row->keyed);
    }
    return length > 0 && (size_t)length < capacity ? length : 0;
}

static int lua_event_rng(lua_State *L) {
    size_t length = 0;
    const char *mode = p_lua_tolstring(L, 1, &length);
    int selected = mode && length == 3 && !memcmp(mode, "key", 3) ? LINUX_EVENT_RNG_KEY :
                   mode && length == 7 && !memcmp(mode, "observe", 7) ? LINUX_EVENT_RNG_OBSERVE : 0;
    double from = p_lua_tonumber(L, 2), to = p_lua_tonumber(L, 3);
    int valid = isfinite(from) && isfinite(to) && from >= 0 && to >= 0 &&
                from < 9007199254740992.0 && to < 9007199254740992.0 && floor(from) == from && floor(to) == to;
    p_lua_pushnumber(L, selected && valid && linux_event_rng_arm(selected, (uint64_t)from, (uint64_t)to));
    return 1;
}

static int lua_event_rng_row(lua_State *L) {
    double index = p_lua_tonumber(L, 1);
    char out[320];
    int length = accept_ui() && isfinite(index) && index >= 0 && index < 9007199254740992.0 && floor(index) == index ?
                 linux_event_rng_row_text((uint64_t)index, out, sizeof(out)) : 0;
    if (length) p_lua_pushlstring(L, out, (size_t)length); else p_lua_pushnil(L);
    return 1;
}

static int lua_event_rng_status(lua_State *L) {
    char out[1024];
    int length = accept_ui() ? snprintf(out, sizeof(out),
        "armed=%ld mode=%s frame=%llu calls=%llu keyed=%llu local=%llu drawn=%llu draws=%llu unknown=%llu boot=%llu unreadable=%llu foreign=%llu max_depth=%u rows=%llu keyset=%016llx drawset=%016llx window=%llu-%llu failures=%lld stage=%s",
        linux_event_rng_armed, linux_event_rng_mode == LINUX_EVENT_RNG_KEY ? "key" : linux_event_rng_mode == LINUX_EVENT_RNG_OBSERVE ? "observe" : "off",
        (unsigned long long)stats.issued, (unsigned long long)linux_event_rng_calls, (unsigned long long)linux_event_rng_keyed,
        (unsigned long long)linux_event_rng_local, (unsigned long long)linux_event_rng_drawn, (unsigned long long)linux_event_rng_draws,
        (unsigned long long)linux_event_rng_unknown, (unsigned long long)linux_event_rng_boot, (unsigned long long)linux_event_rng_unreadable,
        (unsigned long long)linux_event_rng_foreign, linux_event_rng_max_depth, (unsigned long long)linux_event_rng_count,
        (unsigned long long)linux_event_rng_keyset, (unsigned long long)linux_event_rng_drawset, (unsigned long long)linux_event_rng_from,
        (unsigned long long)linux_event_rng_to, (long long)linux_event_rng_failures, linux_event_rng_stage) : 0;
    if (length > 0 && (size_t)length < sizeof(out)) p_lua_pushlstring(L, out, (size_t)length); else p_lua_pushnil(L);
    return 1;
}
