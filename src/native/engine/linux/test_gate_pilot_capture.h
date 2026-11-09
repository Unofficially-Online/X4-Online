/* Called only after elf_image(argv[1]) has copied the pinned ELF into the fake
 * image. Calls are injected; no game code is executed. */
static uintptr_t pilot_capture_test_object;
static int pilot_capture_test_null, pilot_capture_test_nonfinite;
static unsigned pilot_capture_test_calls;
static pthread_mutex_t pilot_test_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t pilot_test_cond = PTHREAD_COND_INITIALIZER;
static int pilot_test_entered, pilot_test_release, pilot_test_block_channel;
static int pilot_test_arm_result, pilot_test_read_result, pilot_test_disarm_done;

static void pilot_test_pause(void) {
    pthread_mutex_lock(&pilot_test_lock);
    pilot_test_entered = 1;
    pthread_cond_broadcast(&pilot_test_cond);
    while (!pilot_test_release) pthread_cond_wait(&pilot_test_cond, &pilot_test_lock);
    pthread_mutex_unlock(&pilot_test_lock);
}
static void pilot_test_wait_entered(void) {
    pthread_mutex_lock(&pilot_test_lock);
    while (!pilot_test_entered) pthread_cond_wait(&pilot_test_cond, &pilot_test_lock);
    pthread_mutex_unlock(&pilot_test_lock);
}
static void pilot_test_unblock(void) {
    pthread_mutex_lock(&pilot_test_lock);
    pilot_test_release = 1;
    pthread_cond_broadcast(&pilot_test_cond);
    pthread_mutex_unlock(&pilot_test_lock);
}
static void *pilot_test_arm_thread(void *unused) {
    (void)unused;
    ui_thread = GetCurrentThreadId();
    pilot_test_arm_result = linux_pilot_capture_arm();
    return NULL;
}
static void *pilot_test_read_thread(void *unused) {
    (void)unused;
    char output[320];
    ui_thread = GetCurrentThreadId();
    linux_pilot_capture_thread = ui_thread;
    pilot_test_read_result = linux_pilot_capture_read(output, sizeof(output));
    return NULL;
}
static void *pilot_test_stop_thread(void *unused) {
    (void)unused;
    linux_pilot_capture_disarm();
    __atomic_store_n(&pilot_test_disarm_done, 1, __ATOMIC_SEQ_CST);
    return NULL;
}

static void *pilot_capture_fake_controller(void) {
    return pilot_capture_test_null ? NULL : &pilot_capture_test_object;
}
static float pilot_capture_fake_channel(void *controller, unsigned int channel) {
    assert(controller == &pilot_capture_test_object);
    assert(channel == LINUX_PILOT_FIRST_CHANNEL + pilot_capture_test_calls);
    ++pilot_capture_test_calls;
    if (pilot_test_block_channel && channel == LINUX_PILOT_FIRST_CHANNEL) pilot_test_pause();
    return pilot_capture_test_nonfinite && channel == 0x6e ? NAN : (float)(channel - 0x6c) * 0.25f;
}
static void test_pilot_capture_elf(void) {
    char output[320];
    unsigned char *entry = LX(LINUX_PILOT_CHANNEL_VA);
    unsigned char saved = *entry;
    DWORD current = GetCurrentThreadId();
    reset_session();
    linux_pilot_capture_begin();
    linux_pilot_test_controller = pilot_capture_fake_controller;
    linux_pilot_test_channel = pilot_capture_fake_channel;
    pilot_capture_test_object = (uintptr_t)LX(LINUX_PILOT_CONTROLLER_VTABLE_VA);
    pilot_capture_test_calls = 0;
    strcpy(output, "unchanged");
    assert(!linux_pilot_capture_read(output, sizeof(output)) && !strcmp(output, "unchanged"));
    assert(!strcmp(linux_pilot_capture_get_reason(), "not_armed"));
    assert(!linux_pilot_capture_arm());
    assert(!strcmp(linux_pilot_capture_get_reason(), "session"));
    stats.active = 1;
    ui_thread = current + 1;
    assert(!linux_pilot_capture_arm());
    assert(!strcmp(linux_pilot_capture_get_reason(), "thread"));
    ui_thread = current;
    const char *exe = gate_env.exe;
    gate_env.exe = "/dev/null";
    assert(!linux_pilot_capture_arm());
    assert(!strcmp(linux_pilot_capture_get_reason(), "file"));
    gate_env.exe = exe;
    *entry ^= 1;
    assert(!linux_pilot_capture_arm());
    assert(!strcmp(linux_pilot_capture_get_reason(), "pins"));
    *entry = saved;
    assert(linux_pilot_capture_arm());
    pilot_capture_test_null = 1;
    assert(!linux_pilot_capture_read(output, sizeof(output)) && !strcmp(output, "unchanged"));
    assert(!strcmp(linux_pilot_capture_get_reason(), "controller"));
    pilot_capture_test_null = 0;
    pilot_capture_test_object = 1;
    assert(!linux_pilot_capture_read(output, sizeof(output)) && !strcmp(output, "unchanged"));
    pilot_capture_test_object = (uintptr_t)LX(LINUX_PILOT_CONTROLLER_VTABLE_VA);
    ui_thread = current + 1;
    assert(!linux_pilot_capture_read(output, sizeof(output)) && !strcmp(output, "unchanged"));
    ui_thread = current;
    *entry ^= 1;
    assert(!linux_pilot_capture_read(output, sizeof(output)) && !strcmp(output, "unchanged"));
    *entry = saved;
    pilot_capture_test_nonfinite = 1;
    pilot_capture_test_calls = 0;
    assert(!linux_pilot_capture_read(output, sizeof(output)) && !strcmp(output, "unchanged"));
    assert(pilot_capture_test_calls == 3);
    assert(!strcmp(linux_pilot_capture_get_reason(), "nonfinite"));
    pilot_capture_test_nonfinite = 0;
    pilot_capture_test_calls = 0;
    assert(!linux_pilot_capture_read(output, 2) && !strcmp(output, "unchanged"));
    assert(!strcmp(linux_pilot_capture_get_reason(), "capacity"));
    pilot_capture_test_calls = 0;
    assert(linux_pilot_capture_read(output, sizeof(output)) > 0 && pilot_capture_test_calls == 6);
    assert(!strcmp(output, "0x6c=0(0x00000000) 0x6d=0.25(0x3e800000) 0x6e=0.5(0x3f000000) "
                           "0x6f=0.75(0x3f400000) 0x70=1(0x3f800000) 0x71=1.25(0x3fa00000)"));
    /* STOP while hashing: it must prevent the in-flight arm from publishing. */
    pthread_t worker, stopper;
    linux_pilot_capture_begin();
    pilot_test_entered = pilot_test_release = pilot_test_disarm_done = 0;
    linux_pilot_test_before_hash = pilot_test_pause;
    assert(!pthread_create(&worker, NULL, pilot_test_arm_thread, NULL));
    pilot_test_wait_entered();
    assert(!pthread_create(&stopper, NULL, pilot_test_stop_thread, NULL));
    while (__atomic_load_n(&linux_pilot_capture_state, __ATOMIC_SEQ_CST) != 1) sched_yield();
    assert(!__atomic_load_n(&pilot_test_disarm_done, __ATOMIC_SEQ_CST));
    pilot_test_unblock();
    pthread_join(worker, NULL);pthread_join(stopper, NULL);
    assert(!pilot_test_arm_result && !linux_pilot_capture_armed);
    assert(!linux_pilot_capture_arm());
    linux_pilot_test_before_hash = NULL;
    /* STOP while inside a getter: it waits until the in-flight read returns. */
    linux_pilot_capture_begin();
    ui_thread = GetCurrentThreadId();
    assert(linux_pilot_capture_arm());
    pilot_capture_test_calls = 0;
    pilot_test_entered = pilot_test_release = pilot_test_disarm_done = 0;
    pilot_test_block_channel = 1;
    assert(!pthread_create(&worker, NULL, pilot_test_read_thread, NULL));
    pilot_test_wait_entered();
    assert(!pthread_create(&stopper, NULL, pilot_test_stop_thread, NULL));
    while (__atomic_load_n(&linux_pilot_capture_state, __ATOMIC_SEQ_CST) != 1) sched_yield();
    assert(!__atomic_load_n(&pilot_test_disarm_done, __ATOMIC_SEQ_CST));
    pilot_test_unblock();
    pthread_join(worker, NULL);pthread_join(stopper, NULL);
    assert(!pilot_test_read_result && pilot_capture_test_calls == 1 && !linux_pilot_capture_armed);
    assert(!linux_pilot_capture_read(output, sizeof(output)));
    pilot_test_block_channel = 0;
    stats.active = 0;
    assert(!linux_pilot_capture_read(output, sizeof(output)));
    linux_pilot_capture_begin();
    linux_pilot_test_controller = NULL;
    linux_pilot_test_channel = NULL;
    reset_session();
    printf("pilot channel diagnostic: ELF pins, thread, controller and finite all-or-none PASS\n");
}
