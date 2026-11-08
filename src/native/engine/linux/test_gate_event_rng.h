/* Event delivery scope tests use the synthetic TLS word and guarded fake objects. */
static unsigned event_rng_test_calls;
static int event_rng_test_skip_draw;
static uint64_t event_rng_test_seen[4];
static void *event_rng_test_inner_source;
static void *event_rng_test_inner_event;
static void event_rng_test_native(void *source, void *event, double due) {
    (void)source;
    (void)event;
    uint64_t *word = gate_tls_rng(gate_tls_self());
    unsigned call = event_rng_test_calls++;
    if (call < 4) event_rng_test_seen[call] = *word;
    if (call == 0 && event_rng_test_inner_source) {
        linux_event_rng_deliver(event_rng_test_native, event_rng_test_inner_source, event_rng_test_inner_event, due + 1);
        assert(*word == event_rng_test_seen[0]);
    }
    if (!event_rng_test_skip_draw) *word = erng_lx_pcg(*word);
}

typedef struct {
    void *source;
    void *event;
    uint64_t final_word;
} event_rng_foreign_test;

static void *event_rng_foreign_thread(void *argument) {
    event_rng_foreign_test *test = argument;
    uint64_t *word = gate_tls_rng(gate_tls_self());
    *word = 0x7777;
    linux_event_rng_deliver(event_rng_test_native, test->source, test->event, 4.0);
    test->final_word = *word;
    return NULL;
}

static void test_event_rng_linux(void) {
    reset_session();
    stats.active = 1;
    boot_mode = 1;
    boot_phase = 3;
    stats.issued = 20;
    ui_thread = GetCurrentThreadId();
    world_identities = calloc(WORLD_IDENTITY_CAPACITY, sizeof(*world_identities));
    assert(world_identities);
    world_identity_installed = 1;
    world_identity_count = world_identity_sealed = 0;
    assert(world_identity_record(7001, 0x123));
    assert(world_identity_record(7002, 0x456));
    unsigned char source[16], inner_source[16], event[16];
    uintptr_t source_type = (uintptr_t)image + 0x1234, event_type = (uintptr_t)image + 0x5678;
    uint64_t runtime = 7001;
    memcpy(source, &source_type, 8); memcpy(source + 8, &runtime, 8);
    runtime = 7002;
    memcpy(inner_source, &source_type, 8); memcpy(inner_source + 8, &runtime, 8);
    memcpy(event, &event_type, 8);
    linux_event_rng_rows = calloc(LINUX_EVENT_RNG_CAPACITY, sizeof(*linux_event_rng_rows));
    assert(linux_event_rng_rows);
    linux_event_rng_count = linux_event_rng_calls = linux_event_rng_failures = 0;
    linux_event_rng_armed = 1;
    linux_event_rng_mode = LINUX_EVENT_RNG_KEY;
    linux_event_rng_from = 20;
    linux_event_rng_to = 20;
    event_rng_test_calls = 0;
    event_rng_test_inner_source = inner_source;
    event_rng_test_inner_event = event;
    uint64_t *word = gate_tls_rng(gate_tls_self());
    *word = 0x9876;
    linux_event_rng_deliver(event_rng_test_native, source, event, 4.0);
    uint64_t due_bits;
    double due = 4.0;
    memcpy(&due_bits, &due, 8);
    assert(event_rng_test_calls == 2 && *word == 0x9876 && linux_event_rng_max_depth == 2);
    assert(event_rng_test_seen[0] == linux_event_rng_seed(20, 0x123, WORLD_KEY_SAVED, 0x5678, due_bits));
    assert(event_rng_test_seen[0] != event_rng_test_seen[1]);
    assert(linux_event_rng_seed(20, 0x123, WORLD_KEY_SAVED, 0x5678, due_bits) == event_rng_test_seen[0]);
    assert(linux_event_rng_seed(21, 0x123, WORLD_KEY_SAVED, 0x5678, due_bits) != event_rng_test_seen[0]);
    assert(linux_event_rng_seed(20, 0x124, WORLD_KEY_SAVED, 0x5678, due_bits) != event_rng_test_seen[0]);
    assert(linux_event_rng_seed(20, 0x123, WORLD_KEY_SAVED, 0x5679, due_bits) != event_rng_test_seen[0]);
    assert(linux_event_rng_seed(20, 0x123, WORLD_KEY_SAVED, 0x5678, due_bits + 1) != event_rng_test_seen[0]);
    assert(linux_event_rng_count == 2 && linux_event_rng_steps(0x9876, erng_lx_pcg(0x9876)) == 1);
    char text[320];
    assert(linux_event_rng_row_text(0, text, sizeof(text)) && strstr(text, "class=saved"));
    linux_event_rng_count = LINUX_EVENT_RNG_CAPACITY + 1;
    assert(linux_event_rng_row_text(0, text, sizeof(text)) && strstr(text, "evicted=1"));
    linux_event_rng_count = 0;
    event_rng_test_inner_source = NULL;
    event_rng_test_skip_draw = 1;
    event_rng_test_calls = 0;
    linux_event_rng_deliver(event_rng_test_native, source, event, due);
    assert(linux_event_rng_count == 1 && linux_event_rng_row_text(0, text, sizeof(text)) && strstr(text, "draws=0"));
    event_rng_test_skip_draw = 0;
    event_rng_test_calls = 0;
    linux_event_rng_mode = LINUX_EVENT_RNG_OBSERVE;
    *word = 0x42;
    linux_event_rng_deliver(event_rng_test_native, source, event, due);
    assert(event_rng_test_calls == 1 && *word == erng_lx_pcg(0x42));
    /* Distinct unmapped runtime IDs of one class deliberately share a fallback. */
    linux_event_rng_mode = LINUX_EVENT_RNG_KEY;
    runtime = 8001;
    memcpy(source + 8, &runtime, 8);
    runtime = 8002;
    memcpy(inner_source + 8, &runtime, 8);
    uint64_t local_before = linux_event_rng_local;
    event_rng_test_calls = 0;
    linux_event_rng_deliver(event_rng_test_native, source, event, due);
    linux_event_rng_deliver(event_rng_test_native, inner_source, event, due);
    assert(event_rng_test_calls == 2 && linux_event_rng_local == local_before + 2);
    assert(event_rng_test_seen[0] == event_rng_test_seen[1]);
    assert(event_rng_test_seen[0] == linux_event_rng_seed(20, 0x1234, WORLD_KEY_NONE, 0x5678, due_bits));

    /* A genuinely different thread must forward exactly once on its own word. */
    event_rng_foreign_test foreign_test = {source, event, 0};
    pthread_t foreign_thread;
    uint64_t foreign_before = linux_event_rng_foreign;
    int64_t failures_before = linux_event_rng_failures;
    event_rng_test_calls = 0;
    assert(!pthread_create(&foreign_thread, NULL, event_rng_foreign_thread, &foreign_test));
    assert(!pthread_join(foreign_thread, NULL));
    assert(event_rng_test_calls == 1 && event_rng_test_seen[0] == 0x7777);
    assert(foreign_test.final_word == erng_lx_pcg(0x7777));
    assert(linux_event_rng_foreign == foreign_before + 1 && linux_event_rng_failures == failures_before + 1);

    /* A readable but foreign vtable also forwards once without a keyed word. */
    uintptr_t foreign_vtable = 1;
    memcpy(event, &foreign_vtable, 8);
    failures_before = linux_event_rng_failures;
    uint64_t unreadable_before = linux_event_rng_unreadable;
    event_rng_test_calls = 0;
    *word = 0x2468;
    linux_event_rng_deliver(event_rng_test_native, source, event, due);
    assert(event_rng_test_calls == 1 && event_rng_test_seen[0] == 0x2468 && *word == erng_lx_pcg(0x2468));
    assert(linux_event_rng_unreadable == unreadable_before + 1 && linux_event_rng_failures == failures_before + 1);
    memcpy(event, &event_type, 8);

    event_rng_test_calls = 0;
    boot_phase = 2;
    linux_event_rng_deliver(event_rng_test_native, source, event, due);
    assert(event_rng_test_calls == 1 && linux_event_rng_boot);
    boot_phase = 3;
    event_rng_test_calls = 0;
    linux_event_rng_deliver(event_rng_test_native, source, NULL, due);
    assert(event_rng_test_calls == 1 && linux_event_rng_unreadable && linux_event_rng_failures);
    event_order_armed = 0;
    assert(!linux_event_rng_check());
    linux_event_rng_stop();
    free(linux_event_rng_rows); linux_event_rng_rows = NULL;
    free(world_identities); world_identities = NULL;
    world_identity_installed = 0; world_identity_count = 0;
    printf("event RNG PASS\n");
}

/* The real pinned image supplies the PCG and event-order code bytes; X4 is not run. */
static void test_event_rng_elf(void) {
    reset_session();
    stats.active = 1;
    boot_mode = 1;
    boot_phase = 0;
    ui_thread = GetCurrentThreadId();
    world_identity_installed = 1;
    assert(!linux_event_rng_arm(LINUX_EVENT_RNG_KEY, 1, 2));
    assert(event_order_arm());
    assert(!linux_event_rng_arm(LINUX_EVENT_RNG_KEY, 0, 2));
    assert(!linux_event_rng_arm(LINUX_EVENT_RNG_KEY, 1, 602));
    assert(linux_event_rng_arm(LINUX_EVENT_RNG_KEY, 1, 2));
    assert(linux_event_rng_arm(LINUX_EVENT_RNG_KEY, 1, 2));
    assert(!linux_event_rng_arm(LINUX_EVENT_RNG_OBSERVE, 1, 2));
    assert(linux_event_rng_check());
    uint64_t foreign = event_order_patched_word ^ 1;
    tl_write(LX(LX_EVENT_CALL_WORD_VA), &foreign, sizeof(foreign));
    assert(!linux_event_rng_check());
    assert(!linux_event_rng_arm(LINUX_EVENT_RNG_KEY, 1, 2));
    tl_write(LX(LX_EVENT_CALL_WORD_VA), &event_order_patched_word, sizeof(event_order_patched_word));
    linux_event_rng_stop();
    event_order_restore();
    assert(!event_order_installed && event_order_restored);
    event_order_armed = 0;
    free(linux_event_rng_rows); linux_event_rng_rows = NULL;
    world_identity_installed = 0;
    printf("event RNG ELF ownership and pins PASS\n");
}
