/* Synthetic station objects only: no engine code or profile is touched. */
static unsigned char economy_test_station[ECONOMY_WATCH_LAST_OFFSET + 8];
static void *economy_test_map = economy_test_station;
static unsigned economy_test_find_count, economy_test_change_at;
static int economy_test_absent, economy_test_unreadable;

static void *economy_test_find(void *map, uint64_t runtime, unsigned mode, unsigned zero) {
    assert(map == economy_test_map && runtime == 100 && mode == 4 && zero == 0);
    ++economy_test_find_count;
    if (economy_test_absent || economy_test_find_count == economy_test_change_at) return NULL;
    return economy_test_station;
}

static int economy_test_read(const void *source, void *destination, size_t size) {
    if (economy_test_unreadable && source == economy_test_station + ECONOMY_WATCH_NEXT_OFFSET) return 0;
    return gate_read_local(source, destination, size);
}

static void economy_test_reset(void) {
    economy_watch_saved = economy_watch_runtime = economy_watch_first = economy_watch_last = 0;
    economy_watch_tick = economy_watch_samples = economy_watch_absent = 0;
    economy_watch_failures = 0;
    economy_test_find_count = economy_test_change_at = 0;
    economy_test_absent = economy_test_unreadable = 0;
    memset(economy_test_station, 0, sizeof(economy_test_station));
    uintptr_t vtable = (uintptr_t)LX(ECONOMY_WATCH_STATION_VTABLE);
    uint64_t runtime = 100, next = UINT64_C(0x4080e30aec33de3f), last = UINT64_C(0x4070000000000000);
    memcpy(economy_test_station, &vtable, 8);
    memcpy(economy_test_station + LXW_OBJ_RUNTIME, &runtime, 8);
    memcpy(economy_test_station + ECONOMY_WATCH_NEXT_OFFSET, &next, 8);
    memcpy(economy_test_station + ECONOMY_WATCH_LAST_OFFSET, &last, 8);
    wt_reset_identities();
    assert(world_identities && world_identity_record(100, 0x36fe9));
    world_identity_sealed = 1;
    world_component_map = &economy_test_map;
    world_find = economy_test_find;
    world_reader = economy_test_read;
    double time = 7.125;
    memcpy(gate_tls_time(gate_tls_self()), &time, sizeof(time));
}

static void test_economy_watch(void) {
    reset_session();
    economy_test_reset();
    assert(!economy_watch_configure("0000000000036fe9", 16, 0, 1));
    assert(!economy_watch_configure("0000000000036fe9", 16, 1, 36001));
    assert(!economy_watch_configure("8000000000036fe9", 16, 1, 2));
    assert(!economy_watch_configure("0000000000036fea", 16, 1, 2));
    assert(economy_watch_configure("0000000000036fe9", 16, 9, 10));
    unsigned char before[sizeof(economy_test_station)];
    memcpy(before, economy_test_station, sizeof(before));
    assert(economy_watch_capture(9));
    assert(strstr(economy_watch_row, "tick=9 id=0000000000036fe9 present=1 class=station"));
    assert(strstr(economy_watch_row, "last=4070000000000000 next=4080e30aec33de3f time=401c800000000000"));
    assert(economy_test_find_count == 2 && !memcmp(before, economy_test_station, sizeof(before)));
    economy_test_absent = 1;
    assert(economy_watch_capture(10) && strstr(economy_watch_row, "present=0 class=- last=- next=-"));
    assert(economy_watch_samples == 2 && economy_watch_absent == 1 && !economy_watch_capture(10));

    economy_test_reset();
    assert(economy_watch_configure("0000000000036fe9", 16, 1, 1));
    economy_test_change_at = 2;
    assert(!economy_watch_capture(1) && !strcmp(economy_watch_problem, "identity_recheck"));
    economy_test_reset();
    assert(economy_watch_configure("0000000000036fe9", 16, 1, 1));
    uintptr_t wrong_class = (uintptr_t)LX(0x3c76e90u);
    memcpy(economy_test_station, &wrong_class, 8);
    assert(!economy_watch_capture(1) && !strcmp(economy_watch_problem, "station_class"));
    for (unsigned field = 0; field < 2; ++field) {
        economy_test_reset();
        assert(economy_watch_configure("0000000000036fe9", 16, 1, 1));
        uint64_t nonfinite = UINT64_C(0x7ff0000000000000);
        size_t offset = field ? ECONOMY_WATCH_LAST_OFFSET : ECONOMY_WATCH_NEXT_OFFSET;
        memcpy(economy_test_station + offset, &nonfinite, sizeof(nonfinite));
        assert(!economy_watch_capture(1) && !strcmp(economy_watch_problem, "deadline_nonfinite"));
    }
    economy_test_reset();
    assert(economy_watch_configure("0000000000036fe9", 16, 1, 1));
    economy_test_unreadable = 1;
    assert(!economy_watch_capture(1) && !strcmp(economy_watch_problem, "object_read"));
    economy_test_reset();
    assert(economy_watch_configure("0000000000036fe9", 16, 1, 1));
    world_identities[world_identity_bucket(100)].saved = 0x36fea;
    assert(!economy_watch_capture(1) && !strcmp(economy_watch_problem, "saved_identity"));
    economy_test_reset();
    economy_watch_saved = 0;
    world_find = wt_find;
    world_reader = gate_read_local;
    printf("economy watch identity, fields, absence and read-only PASS\n");
}
