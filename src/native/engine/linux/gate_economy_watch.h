/* Read-only station economy deadline evidence. This observer never calls an
 * economy routine and never patches or writes engine state. Every sample uses
 * a fresh component-map lookup and rechecks the identity, class and fields.
 * These matching reads do not lock object lifetime or exclude an unseen ABA. */
#define ECONOMY_WATCH_MAX_TICKS 36000u
#define ECONOMY_WATCH_STATION_VTABLE 0x3d2ec50u
/* Pinned import block 0x18412e3 and update guard 0x1878db0 access these doubles. */
#define ECONOMY_WATCH_NEXT_OFFSET 0xa68u
#define ECONOMY_WATCH_LAST_OFFSET 0xa70u

static uint64_t economy_watch_saved, economy_watch_runtime;
static uint64_t economy_watch_first, economy_watch_last, economy_watch_tick;
static uint64_t economy_watch_samples, economy_watch_absent;
static unsigned economy_watch_failures;
static const char *economy_watch_problem = "inactive";
static char economy_watch_row[256];

static int economy_watch_configure(const char *text, size_t length, uint64_t first, uint64_t last) {
    if (economy_watch_saved || !world_identity_sealed || !world_identities || !text || length != 16 ||
        !first || last < first || last - first >= ECONOMY_WATCH_MAX_TICKS) return 0;
    uint64_t saved = 0, runtime = 0;
    for (size_t index = 0; index < length; ++index) {
        char digit = text[index];
        if (!((digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f'))) return 0;
        saved = saved * 16 + (unsigned)(digit <= '9' ? digit - '0' : digit - 'a' + 10);
    }
    if (!saved || (saved >> 63)) return 0;
    for (unsigned index = 0; index < WORLD_IDENTITY_CAPACITY; ++index) {
        const world_identity_entry *entry = &world_identities[index];
        if (entry->saved != saved) continue;
        if (!entry->runtime || runtime) return 0;
        runtime = entry->runtime;
    }
    if (!runtime) return 0;
    economy_watch_saved = saved;
    economy_watch_runtime = runtime;
    economy_watch_first = first;
    economy_watch_last = last;
    economy_watch_problem = "ready";
    return 1;
}

static int economy_watch_capture(uint64_t tick) {
    economy_watch_problem = "sequence";
    if (!economy_watch_saved || economy_watch_failures || tick < economy_watch_first || tick > economy_watch_last ||
        tick != (economy_watch_tick ? economy_watch_tick + 1 : economy_watch_first)) return 0;
    economy_watch_problem = "saved_identity";
    if (world_identity_lookup(economy_watch_runtime) != economy_watch_saved) return 0;
    void *map = NULL;
    economy_watch_problem = "component_map";
    if (!WREAD(world_component_map ? world_component_map : (void **)LX(LX_COMPONENT_MAP_VA),
               &map, sizeof(map)) || !map) return 0;
    double *time = gate_tls_time(gate_tls_self());
    uint64_t time_bits = 0;
    economy_watch_problem = "game_time";
    if (!time || !WREAD(time, &time_bits, sizeof(time_bits))) return 0;
    double game_time;
    memcpy(&game_time, &time_bits, sizeof(game_time));
    if (!isfinite(game_time)) return 0;
    lxw_find_fn find = world_find ? world_find : (lxw_find_fn)(void *)LX(LX_FIND_VA);
    uintptr_t object = (uintptr_t)find(map, economy_watch_runtime, 4, 0);
    uint64_t vtable = 0, runtime = 0, next = 0, last = 0;
    int present = object != 0;
    if (present) {
        economy_watch_problem = "object_read";
        if (!world_nav_address(object, ECONOMY_WATCH_LAST_OFFSET + 8) ||
            !WREAD(object, &vtable, 8) || !WREAD(object + LXW_OBJ_RUNTIME, &runtime, 8) ||
            !WREAD(object + ECONOMY_WATCH_NEXT_OFFSET, &next, 8) ||
            !WREAD(object + ECONOMY_WATCH_LAST_OFFSET, &last, 8)) return 0;
        double next_value, last_value;
        memcpy(&next_value, &next, sizeof(next_value));
        memcpy(&last_value, &last, sizeof(last_value));
        economy_watch_problem = "deadline_nonfinite";
        if (!isfinite(next_value) || !isfinite(last_value)) return 0;
        economy_watch_problem = "station_class";
        if (vtable != (uintptr_t)LX(ECONOMY_WATCH_STATION_VTABLE)) return 0;
        economy_watch_problem = "object_identity";
        if (runtime != economy_watch_runtime) return 0;
    }
    economy_watch_problem = "identity_recheck";
    if ((uintptr_t)find(map, economy_watch_runtime, 4, 0) != object ||
        world_identity_lookup(economy_watch_runtime) != economy_watch_saved) return 0;
    if (present) {
        uint64_t vtable_after, runtime_after, next_after, last_after;
        if (!WREAD(object, &vtable_after, 8) || !WREAD(object + LXW_OBJ_RUNTIME, &runtime_after, 8) ||
            !WREAD(object + ECONOMY_WATCH_NEXT_OFFSET, &next_after, 8) ||
            !WREAD(object + ECONOMY_WATCH_LAST_OFFSET, &last_after, 8) ||
            vtable_after != vtable || runtime_after != runtime || next_after != next || last_after != last) return 0;
    }
    economy_watch_problem = "row_truncated";
    int length;
    if (present) {
        length = snprintf(economy_watch_row, sizeof(economy_watch_row),
            "version=1 tick=%llu id=%016llx present=1 class=station last=%016llx next=%016llx time=%016llx",
            (unsigned long long)tick, (unsigned long long)economy_watch_saved,
            (unsigned long long)last, (unsigned long long)next, (unsigned long long)time_bits);
    } else {
        length = snprintf(economy_watch_row, sizeof(economy_watch_row),
            "version=1 tick=%llu id=%016llx present=0 class=- last=- next=- time=%016llx",
            (unsigned long long)tick, (unsigned long long)economy_watch_saved, (unsigned long long)time_bits);
    }
    if (length < 0 || (size_t)length >= sizeof(economy_watch_row)) return 0;
    economy_watch_tick = tick;
    ++economy_watch_samples;
    if (!present) ++economy_watch_absent;
    economy_watch_problem = "complete";
    return 1;
}

static int lua_economy_watch_config(lua_State *L) {
    size_t length = 0;
    const char *saved = p_lua_tolstring(L, 1, &length);
    double first = p_lua_tonumber(L, 2), last = p_lua_tonumber(L, 3);
    int valid = accept_ui() && stats.active && boot_mode && boot_phase == 3 && stats.issued == 1 &&
        isfinite(first) && isfinite(last) && first >= 1 && last <= 9007199254740991.0 &&
        floor(first) == first && floor(last) == last && first <= last &&
        (economy_rng_installed ? economy_rng_check() : site_ok(site_named("economy_watch_import"))) &&
        site_ok(site_named("economy_watch_guard")) &&
        economy_watch_configure(saved, length, (uint64_t)first, (uint64_t)last);
    p_lua_pushnumber(L, valid);
    return 1;
}

static int lua_economy_watch_capture(lua_State *L) {
    double tick = p_lua_tonumber(L, 1);
    if (!accept_ui() || !stats.active || !boot_mode || boot_phase != 3 || !world_identity_sealed ||
        !isfinite(tick) || tick < 1 || tick > 9007199254740991.0 || tick != (double)stats.issued ||
        !economy_watch_capture((uint64_t)tick)) {
        ++economy_watch_failures;
        p_lua_pushnil(L);
        p_lua_pushlstring(L, economy_watch_problem, strlen(economy_watch_problem));
        return 2;
    }
    p_lua_pushlstring(L, economy_watch_row, strlen(economy_watch_row));
    return 1;
}

static int lua_economy_watch_status(lua_State *L) {
    char out[200];
    int length = snprintf(out, sizeof(out), "id=%016llx first=%llu last=%llu samples=%llu absent=%llu failures=%u",
        (unsigned long long)economy_watch_saved, (unsigned long long)economy_watch_first,
        (unsigned long long)economy_watch_last, (unsigned long long)economy_watch_samples,
        (unsigned long long)economy_watch_absent, economy_watch_failures);
    if (!accept_ui() || length < 0 || (size_t)length >= sizeof(out)) p_lua_pushnil(L);
    else p_lua_pushlstring(L, out, (size_t)length);
    return 1;
}
