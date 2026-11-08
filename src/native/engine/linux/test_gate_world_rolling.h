/* Synthetic rolling census checks; no game code is executed. */
static uint64_t rolling_missing_runtime, rolling_flip_runtime;
static unsigned char rolling_created_objects[4][0x400];
static double rolling_lua_tick, rolling_lua_slices;
static double rolling_lua_number(lua_State *state, int index) {
    (void)state;
    return index == 1 ? rolling_lua_tick : rolling_lua_slices;
}
static void *rolling_test_find(void *map, uint64_t runtime, unsigned mode, unsigned zero) {
    if (runtime == rolling_flip_runtime) {
        *wscen_l->bank_read = 1;
        *wscen_l->bank_write = 0;
    }
    if (runtime == rolling_missing_runtime) return NULL;
    if (runtime >= 9001 && runtime <= 9004) return rolling_created_objects[runtime - 9001];
    return wt_find(map, runtime, mode, zero);
}
static void rolling_expected_roots(unsigned begin, unsigned end, char out[260]) {
    lockstep_sha256_ctx roots[WORLD_HASH_DOMAINS];
    for (unsigned domain = 0; domain < WORLD_HASH_DOMAINS; ++domain)
        lockstep_sha256_init(&roots[domain]);
    for (unsigned index = begin; index < end; ++index) {
        for (unsigned domain = 0; domain < WORLD_HASH_DOMAINS; ++domain) {
            lockstep_sha256_update(&roots[domain], &world_hash_items[index].saved, 8);
            lockstep_sha256_update(&roots[domain], world_hash_items[index].hash[domain], 32);
        }
    }
    char hex[WORLD_HASH_DOMAINS][65];
    unsigned char digest[32];
    for (unsigned domain = 0; domain < WORLD_HASH_DOMAINS; domain++) {
        lockstep_sha256_final(&roots[domain], digest);
        hex32(digest, hex[domain]);
    }
    assert(snprintf(out, 260, "%s:%s:%s:%s", hex[0], hex[1], hex[2], hex[3]) == 259);
}
static void rolling_reset_counts(void) {
    world_hash_scans = world_hash_tick = world_hash_failures = 0;
    world_rolling_slices = world_rolling_total = world_rolling_created = 0;
    world_hash_watch_count = 0;
    rolling_missing_runtime = rolling_flip_runtime = 0;
}
static void test_world_rolling(void) {
    reset_session();
    wt_lua();
    wt_reset_identities();
    wscen_build(wt_linux_layout());
    for (unsigned index = 0; index < WSCEN_IDENTITIES; ++index)
        assert(world_identity_record(wscen_saved_runtime[index], wscen_saved[index]));
    world_identity_sealed = 1;
    free(world_hash_items);
    free(world_hash_changed);
    world_hash_items = NULL;
    world_hash_changed = NULL;
    world_hash_count = 0;
    world_hash_created = NULL;
    world_hash_created_count = 0;
    rolling_reset_counts();
    wt_map_slot = wscen_agent;
    world_component_map = &wt_map_slot;
    world_find = rolling_test_find;
    /* Full schema-2 census and one-slice rolling hash the same ordered leaves. */
    assert(world_hash_capture(1, 2));
    char full_roots[260];
    memcpy(full_roots, world_hash_state + strlen(world_hash_state) - 259, 260);
    assert(!world_hash_capture_rolling(2, 1)); /* mode mixing */
    rolling_reset_counts();
    assert(!world_hash_capture_rolling(2, 1) && !world_hash_capture_rolling(1, 0) && !world_hash_capture_rolling(1, 18001));
    world_hash_watch_count = 1;
    assert(!world_hash_capture_rolling(1, 1));
    world_hash_watch_count = 0;
    assert(world_hash_capture_rolling(1, 1));
    assert(!strcmp(full_roots, world_hash_state + strlen(world_hash_state) - 259));
    assert(!world_hash_capture(2, 2)); /* mode mixing in the other direction */
    /* More slices than IDs: every range is disjoint, empty roots are SHA-256
     * of empty input, and the ordered concatenated slice leaves match full. */
    rolling_reset_counts();
    unsigned previous_end = 0;
    unsigned empty_slices = 0;
    for (unsigned tick = 1; tick <= 16; tick++) {
        unsigned slice = tick - 1;
        unsigned begin = (unsigned)((uint64_t)WSCEN_IDENTITIES * slice / 16);
        unsigned end = (unsigned)((uint64_t)WSCEN_IDENTITIES * (slice + 1) / 16);
        char expected_roots[260];
        char expected_prefix[256];
        rolling_expected_roots(begin, end, expected_roots);
        assert(begin == previous_end);
        previous_end = end;
        if (begin == end) ++empty_slices;
        assert(world_hash_capture_rolling(tick, 16));
        assert(!strcmp(expected_roots, world_hash_state + strlen(world_hash_state) - 259));
        snprintf(expected_prefix, sizeof(expected_prefix), "tick=%u cycle=1 slice=%u slices=16 begin=%u end=%u objects=%u created=0 ",
                 tick, slice, begin, end, (unsigned)WSCEN_IDENTITIES);
        assert(!strncmp(world_hash_summary, expected_prefix, strlen(expected_prefix)));
        assert(!world_hash_capture_rolling(tick, 16)); /* repeated */
        assert(!world_hash_capture_rolling(tick + 2, 16)); /* skipped */
        assert(!world_hash_capture_rolling(tick + 1, 15)); /* changed partition */
    }
    assert(previous_end == WSCEN_IDENTITIES && empty_slices > 0);
    /* A changed hull affects the slice only when the first object is sampled. */
    assert(world_hash_capture_rolling(17, 16)); /* empty first slice */
    char prior_slice_roots[260];
    rolling_expected_roots(0, 1, prior_slice_roots);
    WSCEN_PUT(wscen_object[0] + LXW_OBJ_HULL, double, 951.0);
    assert(world_hash_capture_rolling(18, 16));
    assert(strcmp(prior_slice_roots, world_hash_state + strlen(world_hash_state) - 259));
    /* Failed bank and identity checks cannot publish or advance the tick. */
    uint64_t last_tick = world_hash_tick;
    char last_summary[768];
    strcpy(last_summary, world_hash_summary);
    *wscen_l->bank_write = *wscen_l->bank_read;
    assert(!world_hash_capture_rolling(19, 16) && world_hash_tick == last_tick && !strcmp(last_summary, world_hash_summary));
    *wscen_l->bank_write = 1 - *wscen_l->bank_read;
    /* Reset and put the first object into a single slice with a mismatched ID. */
    rolling_reset_counts();
    WSCEN_PUT(wscen_object[0] + LXW_OBJ_RUNTIME, uint64_t, 9999);
    assert(!world_hash_capture_rolling(1, 1) && !strcmp(world_hash_stage, "topology"));
    WSCEN_PUT(wscen_object[0] + LXW_OBJ_RUNTIME, uint64_t, 100);
    rolling_flip_runtime = 102; /* last ID, after all live objects were read */
    assert(!world_hash_capture_rolling(1, 1) && !strcmp(world_hash_stage, "bank_phase_after"));
    assert(world_hash_tick == 0 && world_hash_scans == 0);
    rolling_flip_runtime = 0;
    *wscen_l->bank_read = 0;
    *wscen_l->bank_write = 1;
    assert(world_hash_capture_rolling(1, 1));
    /* A destroyed object remains in the cohort, freshly resolved as absent. */
    rolling_missing_runtime = 100;
    assert(world_hash_capture_rolling(2, 1));
    assert(world_hash_items[0].saved == 77 && world_hash_items[0].flags == 0);
    rolling_missing_runtime = 0;
    /* Created rows are frozen until the next cycle and sorted by portable ID,
     * even when the objects were created in a different order. */
    rolling_reset_counts();
    world_hash_created = calloc(4, sizeof(*world_hash_created));
    assert(world_hash_created);
    created_live = calloc(4, sizeof(*created_live));
    assert(created_live);
    created_live_count = 0;
    world_hash_created_count = 0;
    memset(rolling_created_objects, 0, sizeof(rolling_created_objects));
    for (unsigned index = 0; index < 4; index++) {
        uint64_t runtime = 9001 + index;
        memcpy(rolling_created_objects[index], &wscen_l->zone_vtable, sizeof(void *));
        memcpy(rolling_created_objects[index] + LXW_OBJ_RUNTIME, &runtime, sizeof(runtime));
    }
    assert(world_hash_capture_rolling(1, 2) && world_rolling_created == 0);
    created_live[0] = (created_live_t){.runtime = 9001, .created = UINT64_C(0x8000000000000009)};
    created_live[1] = (created_live_t){.runtime = 9002, .created = UINT64_C(0x8000000000000001)};
    created_live[2] = (created_live_t){.runtime = 9003, .created = UINT64_C(0x8000000000000005)};
    created_live_count = 3;
    assert(world_hash_capture_rolling(2, 2) && world_rolling_created == 0 && world_hash_created_count == 0);
    assert(world_hash_capture_rolling(3, 2) && world_rolling_created == 3 && world_hash_created_count == 3);
    assert(world_hash_created[0].runtime == 9002 && world_hash_created[0].saved == UINT64_C(0x8000000000000001));
    assert(world_hash_created[1].runtime == 9003 && world_hash_created[1].saved == UINT64_C(0x8000000000000005));
    assert(world_hash_created[2].runtime == 9001 && world_hash_created[2].saved == UINT64_C(0x8000000000000009));
    created_live[3] = (created_live_t){.runtime = 9004, .created = UINT64_C(0x8000000000000004)};
    created_live_count = 4;
    assert(world_hash_capture_rolling(4, 2) && world_rolling_created == 3);
    /* This newborn disappears before the boundary: the cohort never sees it.
     * Its creation event must be covered independently by the creation digest. */
    rolling_missing_runtime = 9004;
    assert(world_hash_capture_rolling(5, 2) && world_rolling_created == 3 && world_hash_created_count == 3);
    assert(!created_live[3].captured);
    free(world_hash_created);
    world_hash_created = NULL;
    world_hash_created_count = 0;
    free(created_live);
    created_live = NULL;
    created_live_count = 0;
    /* Exercise the Lua wrapper with a live synthetic session. Rolling pages,
     * details and health are refused, while the bank receipt remains usable. */
    rolling_reset_counts();
    p_lua_tonumber = rolling_lua_number;
    rolling_lua_tick = 1;
    rolling_lua_slices = 2;
    stats.active = 1;
    stats.issued = 1;
    boot_mode = 1;
    boot_phase = 3;
    world_identity_installed = 1;
    assert(lua_world_capture_rolling(NULL) == 1 && strstr(wt_exported, "WORLD_ROLLING_1:1:1:0:2:"));
    rolling_lua_tick = 0;
    assert(lua_world_page(NULL) == 1 && !strcmp(wt_exported, "nil"));
    assert(lua_world_detail(NULL) == 1 && !strcmp(wt_exported, "nil"));
    assert(lua_world_health(NULL) == 1 && !strcmp(wt_exported, "nil"));
    assert(lua_world_banks(NULL) == 1 && strstr(wt_exported, "tick=1 "));
    rolling_lua_tick = 1;
    char published_summary[sizeof(world_hash_summary)];
    char published_state[sizeof(world_hash_state)];
    strcpy(published_summary, world_hash_summary);
    strcpy(published_state, world_hash_state);
    assert(lua_world_capture_rolling(NULL) == 2 && world_hash_failures == 1);
    rolling_lua_tick = 2;
    stats.issued = 2;
    assert(lua_world_capture_rolling(NULL) == 2 && world_hash_failures == 2);
    assert(world_hash_tick == 1 && !strcmp(published_summary, world_hash_summary));
    assert(!strcmp(published_state, world_hash_state));
    world_identity_installed = 0;
    reset_session();
    wt_lua();
    rolling_reset_counts();
    world_find = NULL;
    world_component_map = NULL;
    printf("world rolling census PASS\n");
}
