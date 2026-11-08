/* Owned synthetic objects only. Compare actual census domain preimages, rather
 * than duplicating the serializer in the expected result. */
static unsigned motion_test_find_calls, motion_test_lose_on_find;
static void *motion_test_find(void *map, uint64_t runtime, unsigned mode, unsigned zero) {
    ++motion_test_find_calls;
    if (motion_test_find_calls == motion_test_lose_on_find) return NULL;
    return wt_find(map, runtime, mode, zero);
}
static void motion_test_reset(void) {
    world_motion_count = world_motion_failures = 0;
    world_motion_tick = world_motion_samples = world_motion_first = world_motion_last = 0;
    motion_test_find_calls = motion_test_lose_on_find = 0;
    wscen_build(wt_linux_layout());
    wt_reset_identities();
    for (unsigned index = 0; index < WSCEN_IDENTITIES; ++index)
        assert(world_identity_record(wscen_saved_runtime[index], wscen_saved[index]));
    world_identity_sealed = 1;
    wt_map_slot = wscen_agent;
    world_component_map = &wt_map_slot;
    world_find = motion_test_find;
}
static void test_world_motion_watch(void) {
    reset_session();
    motion_test_reset();
    assert(!world_motion_configure("000000000000004d", 16, 0, 1));
    assert(!world_motion_configure("000000000000004d", 16, 1, 18001));
    assert(!world_motion_configure("800000000000004d", 16, 1, 2));
    assert(!world_motion_configure("0000000000000000", 16, 1, 2));
    assert(!world_motion_configure("000000000000004d,000000000000004d", 33, 1, 2));
    assert(!world_motion_configure("000000000000004d,", 17, 1, 2));
    assert(!world_motion_configure("0000000000000abc", 16, 1, 2));
    const char four_ids[] = "000000000000004d,000000000000004e,000000000000004f,0000000000000050";
    const char five_ids[] = "000000000000004d,000000000000004e,000000000000004f,0000000000000050,0000000000000051";
    assert(!world_motion_configure(five_ids, sizeof(five_ids) - 1, 1, 2));
    assert(world_motion_configure(four_ids, sizeof(four_ids) - 1, 1, 2) && world_motion_count == 4);
    motion_test_reset();
    assert(world_motion_configure("000000000000004d", 16, 1, 18000));
    assert(!world_motion_configure("000000000000004d", 16, 1, 2));
    /* A full census helper constructs the expected bytes before observation.
     * The observer must not touch any census state, including its digest cache. */
    world_hash_schema = 2;
    assert(world_banks_read(&world_banks_before));
    world_hash_item expected_item = {.runtime = 100, .saved = 77};
    world_hash_data expected[WORLD_HASH_DOMAINS] = {0};
    assert(world_hash_record(&expected_item, expected, wscen_object[0]));
    char expected_motion[WORLD_MOTION_ROW_BYTES], expected_timing[WORLD_MOTION_ROW_BYTES];
    world_motion_hex(expected[2].data, expected[2].length, expected_motion);
    world_motion_hex(expected[3].data, expected[3].length, expected_timing);
    uint64_t scans = world_hash_scans, failures = world_hash_failures, alive = world_hash_alive;
    uint64_t census_tick = world_hash_tick, changes = world_hash_changes;
    unsigned class_unknown = world_hash_class_unknown, motion_unknown = world_hash_motion_unknown;
    unsigned parent_unknown = world_hash_parent_unknown;
    const char *stage = world_hash_stage;
    unsigned char previous_cache[sizeof(world_leaf_digests)];
    memcpy(previous_cache, &world_leaf_digests, sizeof(previous_cache));
    assert(world_motion_capture(1) && motion_test_find_calls == 2);
    assert(strstr(world_motion_rows[0], expected_motion) && strstr(world_motion_rows[0], expected_timing));
    assert(world_hash_scans == scans && world_hash_failures == failures && world_hash_alive == alive &&
           world_hash_tick == census_tick && world_hash_changes == changes && world_hash_stage == stage &&
           world_hash_class_unknown == class_unknown && world_hash_motion_unknown == motion_unknown &&
           world_hash_parent_unknown == parent_unknown && !memcmp(previous_cache, &world_leaf_digests, sizeof(previous_cache)));
    assert(world_motion_samples == 1);
    wscen_step(2); /* physical bank swap + cursor remap preserves canonical bytes */
    assert(world_motion_capture(2) && motion_test_find_calls == 4);
    assert(strstr(world_motion_rows[0], expected_motion) && strstr(world_motion_rows[0], expected_timing));
    assert(!world_motion_capture(2)); /* duplicate tick */
    assert(!world_motion_capture(4)); /* missing tick */
    /* A fresh engine lookup on each side of the read, with no cached pointers. */
    motion_test_reset();
    assert(world_motion_configure("000000000000004d", 16, 8, 9));
    assert(!world_motion_capture(7));
    motion_test_lose_on_find = 2;
    assert(!world_motion_capture(8) && !strcmp(world_motion_problem, "identity_recheck"));
    assert(world_motion_samples == 0);
    motion_test_reset();
    assert(world_motion_configure("000000000000004d", 16, 1, 1));
    WSCEN_PUT(wscen_object[0] + LXW_OBJ_RUNTIME, uint64_t, 999);
    assert(!world_motion_capture(1));
    motion_test_reset();
    assert(world_motion_configure("000000000000004d", 16, 1, 1));
    world_identities[world_identity_bucket(100)].saved = 76;
    assert(!world_motion_capture(1) && !strcmp(world_motion_problem, "saved_identity"));
    motion_test_reset();
    assert(world_motion_configure("000000000000004e", 16, 1, 1)); /* static controller is unsupported */
    assert(!world_motion_capture(1) && world_motion_samples == 0);
    motion_test_reset();
    assert(world_motion_configure("000000000000004d", 16, 1, 1));
    WSCEN_PUT(wscen_agent + LXW_AGENT_SLOTS + 0xb0, uint64_t, 999);
    assert(!world_motion_capture(1) && !strcmp(world_motion_problem, "bank_reference_unknown"));
    motion_test_reset();
    /* Restore the diagnostic's default-off state for subsequent tests. */
    world_find = wt_find;
    printf("world motion watch PASS (census bytes, canonical banks, identity loss, bounded sequence, no census mutation)\n");
}
